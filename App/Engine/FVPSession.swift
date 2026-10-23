import AVFAudio
import Foundation
import QuartzCore
import RFVPRuntime
import UIKit
import VNCore

enum FVPSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "FVP failed to start.") : String(localized: "FVP failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "FVP stopped.") : String(localized: "FVP stopped: \(message)")
        }
    }
}

/// Runs FVP games with rfvp (RFVPRuntime). Like siglus_rs it isn't an SDL
/// program: it renders with wgpu straight into a CAMetalLayer, so the
/// session gives it its own landscape window and steps it from a display
/// link. The orientation, lifecycle and menu overlay are the same as the
/// SDL engines'.
@MainActor
final class NativeFVPSession: NSObject, KRKRSession {
    private static let rendererLabel = "rfvp"

    private(set) var state: KRKRSessionState = .idle {
        didSet {
            AppDiagnostics.shared.event("session", "state.changed", ["from": String(describing: oldValue), "to": String(describing: state)])
        }
    }
    var onFinished: ((Result<Void, Error>) -> Void)?
    var onWarning: ((Error) -> Void)?
    var onReturningToLibrary: (() -> Void)?
    var onMarkFinished: (() -> Void)?

    private let presentation = SDLHostPresentation()
    private let frameDriver = SDLHostFrameDriver()
    private var lifecycleObserver: SDLHostLifecycleObserver?
    private var engineWindow: UIWindow?
    private var controller: FVPViewController?
    private var handle: UnsafeMutableRawPointer?
    private var metricsTimer: Timer?
    private var overlayCoordinator: KRKROverlayCoordinator?
    private var isForeground = true
    private var requestedForeground = true
    private var elapsedSeconds = 0
    private var performanceEnabled = false
    private var framesThisSecond = 0
    private var lastStepTime: CFTimeInterval?
    private static var fontsRegistered = false

    override init() {
        super.init()
        lifecycleObserver = SDLHostLifecycleObserver { [weak self] active in
            self?.setForeground(active)
        }
    }

    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) async throws {
        guard state == .idle || isFailed else { throw KRKRSessionError.alreadyRunning }
        requestedForeground = UIApplication.shared.applicationState == .active
        let gameDirectory = configuration.gameDirectory.standardizedFileURL.resolvingSymlinksInPath()
        var isDirectory = ObjCBool(false)
        guard FileManager.default.fileExists(atPath: gameDirectory.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            throw KRKRSessionError.invalidGameDirectory
        }
        guard let window = viewController.viewIfLoaded?.window,
              let scene = window.windowScene else {
            throw KRKRSessionError.missingHostWindow
        }

        AppDiagnostics.shared.beginGame(["engine": EngineID.fvp.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
                                         "script": configuration.entryPoint.lastPathComponent,
                                         "encoding": configuration.textEncoding.rawValue,
                                         "performanceHUD": String(configuration.performance),
                                         "floatingButton": String(configuration.floatingButton),
                                         "threeFingerMenu": String(configuration.threeFingerMenu),
                                         "respectSilentMode": String(configuration.respectSilentMode)])
        state = .preparingOrientation
        presentation.begin(hostWindow: window, scene: scene)
        do {
            try await presentation.prepareLandscape(scene: scene, window: window, controller: viewController)
        } catch {
            state = .failed(error.localizedDescription)
            await presentation.restorePreviousOrientation()
            throw error
        }

        state = .starting
        await Self.registerSystemFonts()
        configureAudioSession(respectSilentMode: configuration.respectSilentMode)

        // rfvp keeps saves in the game's savedata folder (instead of its own
        // save/), Mikage's saves folder, so Storage can measure and export them.
        try? FileManager.default.createDirectory(
            at: gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true),
            withIntermediateDirectories: true
        )
        LibraryRepository.savesFolderName.withCString { rfvp_mikage_set_save_dir($0) }
        // The script picked as the startup file: a translation patch (.bch)
        // or one of several .hcb files.
        let script = configuration.targetKind == .archive ? configuration.entryPoint.lastPathComponent : nil
        withOptionalCString(script) { rfvp_mikage_set_script($0) }

        let controller = FVPViewController()
        controller.loadViewIfNeeded()
        let engineWindow = UIWindow(windowScene: scene)
        engineWindow.backgroundColor = .black
        engineWindow.rootViewController = controller
        engineWindow.frame = scene.coordinateSpace.bounds
        engineWindow.isHidden = false
        engineWindow.layoutIfNeeded()

        let metalView = controller.metalView
        let scale = scene.screen.scale
        let drawable = controller.drawablePixels(scale: scale)
        let viewPointer = Unmanaged.passUnretained(metalView).toOpaque()
        AppDiagnostics.shared.event("bridge", "rfvp.create", ["drawable": "\(drawable.width)x\(drawable.height)", "scale": String(Double(scale))])
        let created = gameDirectory.path.withCString { game in
            configuration.textEncoding.rawValue.withCString { nls in
                rfvp_ios_create(viewPointer, drawable.width, drawable.height, Double(scale), game, nls)
            }
        }
        guard let created else {
            let message = String(cString: rfvp_mikage_last_error())
            engineWindow.isHidden = true
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw FVPSessionError.runtime(message)
        }
        handle = created
        self.controller = controller
        self.engineWindow = engineWindow

        var logicalWidth: UInt32 = 0
        var logicalHeight: UInt32 = 0
        rfvp_mikage_logical_size(created, &logicalWidth, &logicalHeight)
        controller.attach(handle: created, logicalSize: CGSize(width: Int(logicalWidth), height: Int(logicalHeight)))
        controller.layoutEngine(scale: scale)
        controller.onMenuGesture = configuration.threeFingerMenu ? { [weak self] in self?.showMenuOverlay() } : nil
        AppDiagnostics.shared.event("bridge", "rfvp.created", ["logical": "\(logicalWidth)x\(logicalHeight)"])

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            engineName: EngineID.fvp.displayName,
            performanceVisible: configuration.performance,
            performanceDetail: configuration.performanceDetail,
            renderer: Self.rendererLabel
        )
        overlay.install(
            in: engineWindow,
            options: GameOverlayOptions(configuration: configuration),
            controls: GameControls(
                run: { [weak self] in self?.controller?.runCommand($0) ?? false },
                state: { [weak self] in GameControlState(rawValue: self?.controller?.controlState() ?? 0) },
                setControlSkip: { [weak self] in self?.controller?.setControlSkip($0) ?? false },
                sendPointer: { [weak self] in self?.controller?.sendPointer($0, at: $1) ?? false }
            ),
            onExit: { [weak self] in
                self?.overlayCoordinator?.hideMenu()
                self?.requestStop()
            },
            onBack: { [weak self] in
                // FVP games open their menu, close windows and step back on
                // a right click.
                self?.controller?.sendSecondaryClickAtCenter()
                AppDiagnostics.shared.event("session", "back.requested")
            },
            onMarkFinished: { [weak self] in
                AppDiagnostics.shared.event("session", "markFinished.requested")
                self?.onMarkFinished?()
                self?.overlayCoordinator?.hideMenu()
                self?.requestStop()
            },
            onScreenshot: { [weak self] in self?.snapshot() },
            onSetPerformance: { [weak self] in self?.setPerformanceHUD($0) }
        )
        overlayCoordinator = overlay

        lastStepTime = nil
        frameDriver.start(preferredFramesPerSecond: 60) { [weak self] in self?.step() }
        startMetricsTimer()
        state = .running
        isForeground = true
        if !requestedForeground {
            rfvp_mikage_set_foreground(created, 0)
            isForeground = false
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// Leaving drops the engine, which writes the global save first.
    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        Task { @MainActor in await finish(success: true, message: nil) }
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running, let handle else { return }
        guard isForeground != foreground else { return }
        isForeground = foreground
        lastStepTime = nil
        if foreground {
            try? AVAudioSession.sharedInstance().setActive(true)
        }
        rfvp_mikage_set_foreground(handle, foreground ? 1 : 0)
    }

    func showMenuOverlay() {
        guard state == .running || state == .stopping else { return }
        overlayCoordinator?.showMenu()
    }

    func hideMenuOverlay() {
        guard state == .running else { return }
        overlayCoordinator?.hideMenu()
    }

    func snapshot() -> UIImage? {
        guard let window = engineWindow, !window.bounds.isEmpty else { return nil }
        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
        return renderer.image { _ in
            UIColor.black.setFill()
            UIRectFill(window.bounds)
            controller?.metalView.drawHierarchy(in: window.bounds, afterScreenUpdates: false)
            overlayCoordinator?.drawScreenshotOverlay(in: window)
        }
    }

    private func finish(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "rfvp.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        if let handle {
            controller?.detach()
            rfvp_ios_destroy(handle)
        }
        handle = nil
        overlayCoordinator?.remove()
        overlayCoordinator = nil

        state = .restoringOrientation
        onReturningToLibrary?()
        presentation.showHostWindow()
        engineWindow?.isHidden = true
        engineWindow = nil
        controller = nil
        await presentation.restorePreviousOrientation()
        AppDiagnostics.shared.windows("return.windowsRestored")
        AppDiagnostics.shared.endGame()

        let completion = onFinished
        onFinished = nil
        onWarning = nil
        onReturningToLibrary = nil
        onMarkFinished = nil
        if success {
            state = .idle
            completion?(.success(()))
        } else {
            let detail = message ?? ""
            state = .failed(detail)
            completion?(.failure(FVPSessionError.stopped(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func step() {
        guard state == .running, isForeground, let handle else { return }
        let now = CACurrentMediaTime()
        let elapsed = lastStepTime.map { min(max(now - $0, 0), 0.2) } ?? (1.0 / 60.0)
        lastStepTime = now
        let status = rfvp_mikage_step(handle, UInt32((elapsed * 1000).rounded()))
        framesThisSecond += 1
        switch status {
        case 0:
            break
        case 1:
            state = .stopping
            Task { @MainActor in await finish(success: true, message: nil) }
        default:
            let message = String(cString: rfvp_mikage_last_error())
            state = .stopping
            Task { @MainActor in await finish(success: false, message: message) }
        }
    }

    private func setPerformanceHUD(_ enabled: Bool) {
        performanceEnabled = enabled
        overlayCoordinator?.setPerformanceVisible(performanceEnabled)
    }

    private func startMetricsTimer() {
        metricsTimer?.invalidate()
        framesThisSecond = 0
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.updateMetrics() }
        }
        RunLoop.main.add(timer, forMode: .common)
        metricsTimer = timer
    }

    private func updateMetrics() {
        guard state == .running else { return }
        if isForeground {
            elapsedSeconds += 1
        }
        let frames = framesThisSecond
        framesThisSecond = 0
        let resident = processResidentMemoryBytes()
        if AppDiagnostics.shared.isEnabled {
            AppDiagnostics.shared.event("session", "heartbeat", [
                "foreground": String(isForeground),
                "steps": String(frames),
                "residentBytes": String(resident),
                "thermalState": String(ProcessInfo.processInfo.thermalState.rawValue)
            ])
        }
        guard performanceEnabled else {
            // The floating buttons still settle in the bars beside the game.
            overlayCoordinator?.update(
                KRKRPerformanceSnapshot(
                    renderer: Self.rendererLabel,
                    elapsedSeconds: elapsedSeconds,
                    gameSize: controller?.gameSize ?? .zero
                )
            )
            return
        }
        let drawable = controller?.lastDrawablePixels ?? (0, 0)
        var snapshot = KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds)
        snapshot.framesPerSecond = Double(frames)
        snapshot.frameTimeMilliseconds = frames > 0 ? 1000.0 / Double(frames) : 0
        snapshot.drawableWidth = Int32(drawable.0)
        snapshot.drawableHeight = Int32(drawable.1)
        snapshot.residentMemoryBytes = resident
        snapshot.showsLayerCompositing = false
        snapshot.gpuMemoryBytes = metalAllocatedBytes()
        snapshot.gameSize = controller?.gameSize ?? .zero
        overlayCoordinator?.update(snapshot)
    }

    private func configureAudioSession(respectSilentMode: Bool) {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(respectSilentMode ? .ambient : .playback, mode: .default)
            try session.setActive(true)
        } catch {
            onWarning?(KRKRSessionError.audioSession(error.localizedDescription))
        }
    }

    /// Games ask for MS Gothic, MS Mincho, MS PGothic and MS PMincho; as for
    /// Siglus, PingFang stands in for all of them (one face for Chinese
    /// translations and Japanese originals alike), with a per-character
    /// fallback chain behind it. rfvp keeps them for the whole process.
    private static func registerSystemFonts() async {
        guard !fontsRegistered else { return }
        let main = Locale.preferredLanguages.first.map(SystemFontExport.prefersTraditionalChinese) == true
            ? "PingFangTC-Regular" : "PingFangSC-Regular"
        // Characters the main face lacks (Hangul...) come from these, in order.
        let fallbacks = ["PingFangSC-Regular", "PingFangTC-Regular", "HiraginoSans-W3", "AppleSDGothicNeo-Regular"]
            .filter { $0 != main }
        // Rebuilding a face takes a few seconds the first time.
        let files = await Task.detached(priority: .userInitiated) {
            (main: SystemFontExport.fontFiles(main), fallbacks: fallbacks.map { ($0, SystemFontExport.fontFiles($0)) })
        }.value
        for (path, face) in files.main {
            for slot: Int32 in 0..<4 {
                let loaded = path.withCString { path in face.withCString { rfvp_mikage_register_font_face(path, slot, $0) } }
                AppDiagnostics.shared.event("bridge", "rfvp.font", ["name": main, "face": String(slot), "path": path, "loaded": String(loaded != 0)])
                if loaded != 0 { fontsRegistered = true }
            }
        }
        for (name, faces) in files.fallbacks {
            for (path, face) in faces {
                let loaded = path.withCString { path in face.withCString { rfvp_mikage_register_fallback_font(path, $0) } }
                AppDiagnostics.shared.event("bridge", "rfvp.fallbackFont", ["name": name, "path": path, "loaded": String(loaded != 0)])
            }
        }
    }
}

/// The CAMetalLayer view wgpu renders into. rfvp letterboxes the game in it
/// and maps touches from view points to the game itself.
final class FVPMetalView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
}

@MainActor
final class FVPViewController: UIViewController {
    let metalView = FVPMetalView(frame: .zero)
    var onMenuGesture: (() -> Void)?
    private(set) var lastDrawablePixels: (UInt32, UInt32) = (0, 0)
    private var handle: UnsafeMutableRawPointer?
    private var logicalSize = CGSize(width: 800, height: 600)
    /// The game's own resolution, letterboxed into the view.
    var gameSize: CGSize { logicalSize }
    private var activeTouch: UITouch?
    private var wheelRemainder: CGFloat = 0
    /// Points of two-finger travel per wheel step.
    private let wheelStep: CGFloat = 40

    override func loadView() {
        metalView.backgroundColor = .black
        metalView.isOpaque = true
        metalView.isMultipleTouchEnabled = true
        view = metalView
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        let menu = UITapGestureRecognizer(target: self, action: #selector(menuGesture))
        menu.numberOfTouchesRequired = 3
        menu.cancelsTouchesInView = false
        view.addGestureRecognizer(menu)
        // Touch is a left click; a long press stands for the right click
        // games use for their menus.
        let longPress = UILongPressGestureRecognizer(target: self, action: #selector(longPress(_:)))
        longPress.minimumPressDuration = 0.5
        longPress.cancelsTouchesInView = true
        view.addGestureRecognizer(longPress)
        // Two fingers dragged up and down turn the mouse wheel, which FVP
        // games use for their backlog: down (pulling earlier text in) is up.
        let wheel = UIPanGestureRecognizer(target: self, action: #selector(wheelPan(_:)))
        wheel.minimumNumberOfTouches = 2
        wheel.maximumNumberOfTouches = 2
        wheel.cancelsTouchesInView = true
        view.addGestureRecognizer(wheel)
    }

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }

    func drawablePixels(scale: CGFloat) -> (width: UInt32, height: UInt32) {
        let size = view.bounds.size
        return (UInt32(max(1, (size.width * scale).rounded())), UInt32(max(1, (size.height * scale).rounded())))
    }

    func attach(handle: UnsafeMutableRawPointer, logicalSize: CGSize) {
        self.handle = handle
        if logicalSize.width > 0 && logicalSize.height > 0 {
            self.logicalSize = logicalSize
        }
        lastDrawablePixels = (0, 0)
    }

    func detach() {
        handle = nil
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutEngine(scale: view.window?.screen.scale ?? UIScreen.main.scale)
    }

    func layoutEngine(scale: CGFloat) {
        guard let handle, view.bounds.width > 0, view.bounds.height > 0 else { return }
        metalView.contentScaleFactor = scale
        let pixels = drawablePixels(scale: scale)
        guard pixels.width != lastDrawablePixels.0 || pixels.height != lastDrawablePixels.1 else { return }
        lastDrawablePixels = (pixels.width, pixels.height)
        (metalView.layer as? CAMetalLayer)?.drawableSize = CGSize(width: Int(pixels.width), height: Int(pixels.height))
        rfvp_ios_resize(handle, pixels.width, pixels.height)
    }

    /// The game menu's and quick actions' commands. FVP games draw their own
    /// menus, save screens and auto mode in script, so only the backlog (the
    /// mouse wheel) has a way in.
    func runCommand(_ command: GameCommand) -> Bool {
        guard let handle else { return false }
        switch command {
        case .backlog:
            rfvp_ios_mouse_wheel(handle, 1, Double(view.bounds.midX), Double(view.bounds.midY))
            return true
        default:
            return false
        }
    }

    func controlState() -> UInt32 {
        handle == nil ? 0 : GameControlState.canBacklog.rawValue
    }

    /// Ctrl held, which FVP games read as skip.
    func setControlSkip(_ held: Bool) -> Bool {
        guard let handle else { return false }
        rfvp_mikage_key(handle, 0x11, held ? 1 : 0)
        return true
    }

    /// The trackpad cursor, from a point in this view.
    func sendPointer(_ phase: PointerPhase, at point: CGPoint) -> Bool {
        guard let handle else { return false }
        rfvp_mikage_pointer(handle, phase.rawValue, Double(point.x), Double(point.y))
        return true
    }

    func sendSecondaryClickAtCenter() {
        guard let handle else { return }
        rfvp_mikage_secondary_click(handle, Double(view.bounds.midX), Double(view.bounds.midY))
    }

    private func send(_ phase: Int32, _ touch: UITouch) {
        guard let handle else { return }
        let point = touch.location(in: view)
        rfvp_ios_touch(handle, phase, Double(point.x), Double(point.y))
    }

    private func cancelActiveTouch() {
        guard let touch = activeTouch else { return }
        activeTouch = nil
        send(3, touch)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // One finger plays; more fingers are the wheel or the menu gesture.
        guard (event?.allTouches?.count ?? 1) == 1 else {
            cancelActiveTouch()
            return
        }
        guard activeTouch == nil, let touch = touches.first else { return }
        activeTouch = touch
        send(0, touch)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        send(1, touch)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        send(2, touch)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        send(3, touch)
    }

    @objc private func menuGesture() {
        onMenuGesture?()
    }

    @objc private func longPress(_ recognizer: UILongPressGestureRecognizer) {
        guard recognizer.state == .began, let handle else { return }
        let point = recognizer.location(in: view)
        rfvp_mikage_secondary_click(handle, Double(point.x), Double(point.y))
    }

    @objc private func wheelPan(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            cancelActiveTouch()
            wheelRemainder = 0
        case .changed:
            let translation = recognizer.translation(in: view)
            recognizer.setTranslation(.zero, in: view)
            guard abs(translation.y) >= abs(translation.x), let handle else { return }
            wheelRemainder += translation.y
            let steps = Int(wheelRemainder / wheelStep)
            guard steps != 0 else { return }
            wheelRemainder -= CGFloat(steps) * wheelStep
            let point = recognizer.location(in: view)
            rfvp_ios_mouse_wheel(handle, Int32(steps), Double(point.x), Double(point.y))
        default:
            wheelRemainder = 0
        }
    }
}

private func withOptionalCString<Result>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> Result) -> Result {
    guard let string else { return body(nil) }
    return string.withCString { body($0) }
}
