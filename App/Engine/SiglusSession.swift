import AVFAudio
import CoreText
import Foundation
import QuartzCore
import SiglusRuntime
import UIKit
import VNCore

enum SiglusSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "SiglusEngine failed to start.") : String(localized: "SiglusEngine failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "SiglusEngine stopped.") : String(localized: "SiglusEngine stopped: \(message)")
        }
    }
}

/// Runs SiglusEngine games with siglus_rs (SiglusRuntime). It isn't an SDL
/// program: it renders with wgpu straight into a CAMetalLayer, so the session
/// gives it its own landscape window and steps it from a display link. The
/// orientation, lifecycle and menu overlay are the same as the SDL engines'.
@MainActor
final class NativeSiglusSession: NSObject, KRKRSession {
    private static let rendererLabel = "siglus_rs"

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
    private var controller: SiglusViewController?
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

        AppDiagnostics.shared.beginGame(["engine": EngineID.siglus.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
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
        SiglusMovieDecoder.register()
        configureAudioSession(respectSilentMode: configuration.respectSilentMode)

        // siglus_rs keeps saves in the game's savedata folder, which is
        // Mikage's saves folder too, so Storage can measure and export them.
        try? FileManager.default.createDirectory(
            at: gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true),
            withIntermediateDirectories: true
        )

        // A patched executable picked as the startup file (SiglusEngineCHS.exe
        // and the like) loads renamed copies of Scene.pck and Gameexe.dat.
        let names = configuration.targetKind == .archive
            ? GameScanner.siglusResourceNames(of: configuration.entryPoint, in: gameDirectory)
            : SiglusResourceNames()
        AppDiagnostics.shared.event("bridge", "siglus.resources", ["scene": names.scene ?? "Scene.pck", "gameexe": names.gameexe ?? "Gameexe.dat"])
        gameDirectory.path.withCString { root in
            withOptionalCString(names.scene) { scene in
                withOptionalCString(names.gameexe) { gameexe in
                    siglus_mikage_set_resource_names(root, scene, gameexe)
                }
            }
        }

        let controller = SiglusViewController()
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
        AppDiagnostics.shared.event("bridge", "siglus.create", ["drawable": "\(drawable.width)x\(drawable.height)", "scale": String(Double(scale))])
        let created = gameDirectory.path.withCString { game in
            siglus_ios_create(viewPointer, drawable.width, drawable.height, Double(scale), game)
        }
        guard let created else {
            let message = String(cString: siglus_mikage_last_error())
            engineWindow.isHidden = true
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw SiglusSessionError.runtime(message)
        }
        handle = created
        self.controller = controller
        self.engineWindow = engineWindow

        var logicalWidth: UInt32 = 0
        var logicalHeight: UInt32 = 0
        siglus_ios_logical_size(created, &logicalWidth, &logicalHeight)
        controller.attach(handle: created, logicalSize: CGSize(width: Int(logicalWidth), height: Int(logicalHeight)))
        controller.layoutEngine(scale: scale)
        controller.onMenuGesture = configuration.threeFingerMenu ? { [weak self] in self?.showMenuOverlay() } : nil
        let context = Unmanaged.passUnretained(self).toOpaque()
        siglus_ios_set_native_messagebox_callback(created, siglusMessageBoxCallback, context)
        AppDiagnostics.shared.event("bridge", "siglus.created", ["logical": "\(logicalWidth)x\(logicalHeight)"])

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            engineName: EngineID.siglus.displayName,
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
                // Siglus games open their system menu, close windows and
                // step back on a right click.
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
            siglus_mikage_set_foreground(created, 0)
            isForeground = false
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// Siglus has no shutdown of its own: the global save is written, then
    /// the engine is dropped.
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
        siglus_mikage_set_foreground(handle, foreground ? 1 : 0)
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

    fileprivate func presentMessageBox(requestID: UInt64, kind: Int32, title: String, message: String) {
        guard let controller, let handle else { return }
        let alert = UIAlertController(title: title.isEmpty ? "SiglusEngine" : title, message: message, preferredStyle: .alert)
        func add(_ title: String, _ style: UIAlertAction.Style, _ value: Int64) {
            alert.addAction(UIAlertAction(title: title, style: style) { _ in
                siglus_ios_submit_messagebox_result(handle, requestID, value)
            })
        }
        // The same kinds and results as the Win32 MessageBox calls they stand for.
        switch kind {
        case 1:
            add(String(localized: "OK"), .default, 0)
            add(String(localized: "Cancel"), .cancel, 1)
        case 2:
            add(String(localized: "Yes"), .default, 0)
            add(String(localized: "No"), .cancel, 1)
        case 3:
            add(String(localized: "Yes"), .default, 0)
            add(String(localized: "No"), .default, 1)
            add(String(localized: "Cancel"), .cancel, 2)
        default:
            add(String(localized: "OK"), .default, 0)
        }
        controller.present(alert, animated: true)
    }

    private func finish(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "siglus.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        if let handle {
            siglus_mikage_set_foreground(handle, 0)
            siglus_ios_set_native_messagebox_callback(handle, nil, nil)
            controller?.detach()
            siglus_ios_destroy(handle)
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
            completion?(.failure(SiglusSessionError.stopped(detail)))
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
        let status = siglus_mikage_step(handle, UInt32((elapsed * 1000).rounded()))
        framesThisSecond += 1
        switch status {
        case 0:
            break
        case 1:
            state = .stopping
            Task { @MainActor in await finish(success: true, message: nil) }
        default:
            let message = String(cString: siglus_mikage_last_error())
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

    /// Games ask for Windows faces (MS Gothic, MS Mincho); PingFang stands in
    /// for both (Mashiro's choice: one face for Chinese translations and
    /// Japanese originals alike), with a per-character fallback chain behind it.
    private static func registerSystemFonts() async {
        guard !fontsRegistered else { return }
        fontsRegistered = true
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
            for family: Int32 in [0, 1] {
                AppDiagnostics.shared.event("bridge", "siglus.font", ["name": main, "family": String(family), "path": path])
                path.withCString { path in face.withCString { siglus_mikage_register_font_face(path, family, $0) } }
            }
        }
        for (name, faces) in files.fallbacks {
            for (path, face) in faces {
                AppDiagnostics.shared.event("bridge", "siglus.fallbackFont", ["name": name, "path": path])
                path.withCString { path in face.withCString { siglus_mikage_register_fallback_font(path, $0) } }
            }
        }
    }
}

private func siglusMessageBoxCallback(
    _ context: UnsafeMutableRawPointer?,
    _ requestID: UInt64,
    _ kind: Int32,
    _ title: UnsafePointer<CChar>?,
    _ message: UnsafePointer<CChar>?
) {
    guard let context else { return }
    let session = Unmanaged<NativeSiglusSession>.fromOpaque(context).takeUnretainedValue()
    let titleText = title.map { String(cString: $0) } ?? ""
    let messageText = message.map { String(cString: $0) } ?? ""
    Task { @MainActor in
        session.presentMessageBox(requestID: requestID, kind: kind, title: titleText, message: messageText)
    }
}

/// The CAMetalLayer view wgpu renders into, and touch input mapped from the
/// letterboxed viewport to the game's own coordinates.
final class SiglusMetalView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
}

@MainActor
final class SiglusViewController: UIViewController {
    let metalView = SiglusMetalView(frame: .zero)
    var onMenuGesture: (() -> Void)?
    private(set) var lastDrawablePixels: (UInt32, UInt32) = (0, 0)
    private var handle: UnsafeMutableRawPointer?
    private var logicalSize = CGSize(width: 1280, height: 720)
    /// The game's own resolution, letterboxed into the view.
    var gameSize: CGSize { logicalSize }
    private var viewportPoints = CGRect(x: 0, y: 0, width: 1, height: 1)
    private var activeTouch: UITouch?

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
        (metalView.layer as? CAMetalLayer)?.drawableSize = CGSize(width: Int(pixels.width), height: Int(pixels.height))
        let logicalWidth = Double(logicalSize.width)
        let logicalHeight = Double(logicalSize.height)
        let fit = min(Double(pixels.width) / logicalWidth, Double(pixels.height) / logicalHeight)
        let width = UInt32(max(1, min(Double(pixels.width), (logicalWidth * fit).rounded())))
        let height = UInt32(max(1, min(Double(pixels.height), (logicalHeight * fit).rounded())))
        let x = (pixels.width - width) / 2
        let y = (pixels.height - height) / 2
        viewportPoints = CGRect(x: CGFloat(x) / scale, y: CGFloat(y) / scale,
                                width: CGFloat(width) / scale, height: CGFloat(height) / scale)
        guard pixels.width != lastDrawablePixels.0 || pixels.height != lastDrawablePixels.1 else { return }
        lastDrawablePixels = (pixels.width, pixels.height)
        siglus_ios_resize_viewport(handle, pixels.width, pixels.height, x, y, width, height)
    }

    /// The game menu's and quick actions' commands; quick save and load use
    /// the game's first quick slot, backlog, save and load open the game's
    /// own screens.
    func runCommand(_ command: GameCommand) -> Bool {
        guard let handle else { return false }
        return siglus_mikage_command(handle, command.rawValue) != 0
    }

    func controlState() -> UInt32 {
        handle.map { siglus_mikage_controls($0) } ?? 0
    }

    /// Ctrl held, which skips read and unread text alike.
    func setControlSkip(_ held: Bool) -> Bool {
        guard let handle else { return false }
        siglus_mikage_key(handle, 0x11, held ? 1 : 0)
        return true
    }

    /// The trackpad cursor, from a point in this view.
    func sendPointer(_ phase: PointerPhase, at point: CGPoint) -> Bool {
        guard let handle else { return false }
        let game = CGPoint(
            x: (point.x - viewportPoints.minX) * logicalSize.width / max(viewportPoints.width, 1),
            y: (point.y - viewportPoints.minY) * logicalSize.height / max(viewportPoints.height, 1)
        )
        siglus_mikage_pointer(handle, phase.rawValue, Double(game.x), Double(game.y))
        return true
    }

    func sendSecondaryClickAtCenter() {
        guard let handle else { return }
        siglus_mikage_secondary_click(handle, Double(logicalSize.width) / 2, Double(logicalSize.height) / 2)
    }

    private func gamePoint(_ touch: UITouch) -> CGPoint {
        let point = touch.location(in: view)
        return CGPoint(
            x: (point.x - viewportPoints.minX) * logicalSize.width / max(viewportPoints.width, 1),
            y: (point.y - viewportPoints.minY) * logicalSize.height / max(viewportPoints.height, 1)
        )
    }

    private func send(_ phase: Int32, _ touch: UITouch) {
        guard let handle else { return }
        let point = gamePoint(touch)
        siglus_ios_touch(handle, phase, Double(point.x), Double(point.y))
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // A touch whose end never reached this view (a gesture or the system
        // took it) would otherwise block every later touch.
        if let stale = activeTouch, stale.phase == .ended || stale.phase == .cancelled {
            activeTouch = nil
            send(3, stale)
        }
        // One finger plays; more fingers are the menu gesture.
        guard activeTouch == nil, (event?.allTouches?.count ?? 1) == 1, let touch = touches.first else { return }
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
        let game = CGPoint(
            x: (point.x - viewportPoints.minX) * logicalSize.width / max(viewportPoints.width, 1),
            y: (point.y - viewportPoints.minY) * logicalSize.height / max(viewportPoints.height, 1)
        )
        siglus_mikage_secondary_click(handle, Double(game.x), Double(game.y))
    }
}

private func withOptionalCString<Result>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> Result) -> Result {
    guard let string else { return body(nil) }
    return string.withCString { body($0) }
}
