import AVFAudio
import AVFoundation
import Foundation
import QuartzCore
import UIKit
import VNCore
import YurisRuntime

enum YurisSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "YU-RIS failed to start.") : String(localized: "YU-RIS failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "YU-RIS stopped.") : String(localized: "YU-RIS stopped: \(message)")
        }
    }
}

/// Runs YU-RIS games with MikageYURIS (YurisRuntime), through its C
/// interface (yuris_mobile.h). Like rfvp and siglus_rs it isn't an SDL
/// program: it draws with wgpu into the CAMetalLayer of a view, so the
/// session gives it its own landscape window and steps it from a display
/// link. Unlike them it has no audio output of its own: the session pulls
/// its mixer from an AVAudioEngine source node. The orientation, lifecycle
/// and menu overlay are the SDL engines'.
@MainActor
final class NativeYurisSession: NSObject, KRKRSession {
    private static let rendererLabel = "MikageYURIS"

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
    private var controller: YurisViewController?
    private var handle: OpaquePointer?
    private var audio: YurisAudioOutput?
    private var metricsTimer: Timer?
    private var overlayCoordinator: KRKROverlayCoordinator?
    private var isForeground = true
    private var requestedForeground = true
    private var elapsedSeconds = 0
    private var performanceEnabled = false
    private var framesThisSecond = 0
    private var lastStepTime: CFTimeInterval?

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
        // A translation patch packed into an executable (erewhon_chs.exe):
        // its files win over the game's.
        let launcher = configuration.targetKind == .archive
            && configuration.entryPoint.pathExtension.lowercased() == "exe"
            ? configuration.entryPoint.path : nil

        AppDiagnostics.shared.beginGame(["engine": EngineID.yuris.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
                                         "launcher": launcher.map { ($0 as NSString).lastPathComponent } ?? "",
                                         "encoding": configuration.textEncoding?.rawValue ?? "auto",
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

        // Saves go to the game's savedata folder, Mikage's saves folder, so
        // Storage can measure and export them.
        let saveDirectory = gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true)
        try? FileManager.default.createDirectory(at: saveDirectory, withIntermediateDirectories: true)

        // Booting reads every script once; keep it off the main thread.
        let encoding: UInt32
        switch configuration.textEncoding {
        case .shiftJIS: encoding = 0
        case .gbk: encoding = 1
        case .utf8: encoding = 2
        case nil: encoding = 3
        }
        let offset = Int64(TimeZone.current.secondsFromGMT()) * 1000
        let gamePath = gameDirectory.path
        let savePath = saveDirectory.path
        AppDiagnostics.shared.event("bridge", "yuris.open", ["encoding": String(encoding)])
        let opened: (UInt?, String) = await Task.detached(priority: .userInitiated) {
            let session = gamePath.withCString { game in
                savePath.withCString { save in
                    withOptionalCString(launcher) { launcher in
                        var config = YurisConfig(
                            struct_size: UInt32(MemoryLayout<YurisConfig>.size),
                            game_dir: game,
                            launcher: launcher,
                            save_dir: save,
                            encoding: encoding,
                            utc_offset_ms: offset)
                        return yuris_open(&config)
                    }
                }
            }
            let bits = session.map { UInt(bitPattern: UnsafeRawPointer($0)) }
            return (bits, bits == nil ? String(cString: yuris_last_error()) : "")
        }.value
        guard let bits = opened.0, let created = OpaquePointer(bitPattern: bits) else {
            state = .failed(opened.1)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw YurisSessionError.runtime(opened.1)
        }

        let controller = YurisViewController()
        controller.loadViewIfNeeded()
        let engineWindow = UIWindow(windowScene: scene)
        engineWindow.backgroundColor = .black
        engineWindow.rootViewController = controller
        engineWindow.frame = scene.coordinateSpace.bounds
        engineWindow.isHidden = false
        engineWindow.layoutIfNeeded()

        var width: UInt32 = 0
        var height: UInt32 = 0
        yuris_screen_size(created, &width, &height)
        controller.attach(handle: created, gameSize: CGSize(width: Int(width), height: Int(height)))
        let scale = scene.screen.scale
        guard controller.layoutEngine(scale: scale) else {
            let message = String(cString: yuris_last_error())
            yuris_close(created)
            engineWindow.isHidden = true
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw YurisSessionError.runtime(message)
        }
        handle = created
        self.controller = controller
        self.engineWindow = engineWindow
        controller.onMenuGesture = configuration.threeFingerMenu ? { [weak self] in self?.showMenuOverlay() } : nil
        AppDiagnostics.shared.event("bridge", "yuris.created", ["screen": "\(width)x\(height)"])

        let output = YurisAudioOutput(session: created)
        do {
            try output.start()
        } catch {
            onWarning?(KRKRSessionError.audioSession(error.localizedDescription))
        }
        audio = output

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            engineName: EngineID.yuris.displayName,
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
                // YU-RIS games open their menu and close windows on a
                // right click.
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
            isForeground = false
            audio?.pause()
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// The game writes its saves as it goes; leaving just drops it.
    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        Task { @MainActor in await finish(success: true, message: nil) }
    }

    /// In the background the game stops: no steps (its clock stands
    /// still) and no sound.
    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running, handle != nil else { return }
        guard isForeground != foreground else { return }
        isForeground = foreground
        lastStepTime = nil
        if foreground {
            try? AVAudioSession.sharedInstance().setActive(true)
            try? audio?.start()
        } else {
            audio?.pause()
        }
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
        AppDiagnostics.shared.event("bridge", "yuris.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        audio?.stop()
        audio = nil
        if let handle {
            controller?.detach()
            yuris_close(handle)
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
            completion?(.failure(YurisSessionError.stopped(detail)))
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
        let status = yuris_step(handle, UInt32((elapsed * 1000).rounded()))
        framesThisSecond += 1
        switch status {
        case 0: // YURIS_RUNNING
            break
        case 1: // YURIS_ENDED
            state = .stopping
            Task { @MainActor in await finish(success: true, message: nil) }
        default:
            let message = String(cString: yuris_last_error())
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

    /// Games ask for MS Gothic and the like; as for Siglus and FVP,
    /// PingFang (Traditional on Traditional Chinese devices) stands in for
    /// all of them, Hiragino Sans if PingFang can't be loaded. The runtime
    /// draws a face the game names but nobody registered with the first
    /// one registered.
    private static func registerSystemFonts() async {
        let main = Locale.preferredLanguages.first.map(SystemFontExport.prefersTraditionalChinese) == true
            ? "PingFangTC-Regular" : "PingFangSC-Regular"
        let names = [main, "HiraginoSans-W3"]
        // Rebuilding a face takes a few seconds the first time.
        let files = await Task.detached(priority: .userInitiated) {
            names.map { ($0, SystemFontExport.fontFiles($0)) }
        }.value
        yuris_clear_fonts()
        for (name, faces) in files {
            for (path, face) in faces {
                let loaded = path.withCString { path in face.withCString { yuris_register_font(path, $0) } }
                AppDiagnostics.shared.event("bridge", "yuris.font", ["name": name, "path": path, "loaded": String(loaded != 0)])
            }
        }
    }
}

/// Plays the runtime's mixer: an AVAudioEngine source node pulls it at its
/// own rate (44.1 kHz stereo) on the audio thread.
final class YurisAudioOutput {
    private static let chunk = 1024
    private let engine = AVAudioEngine()
    private let audio: OpaquePointer?
    private var node: AVAudioSourceNode?
    /// Interleaved samples from the runtime, split into the node's two
    /// channel buffers. Allocated here, outside the audio thread.
    private let scratch = UnsafeMutablePointer<Float>.allocate(capacity: chunk * 2)

    init(session: OpaquePointer) {
        audio = yuris_audio(session)
    }

    deinit {
        engine.stop()
        if let audio { yuris_audio_release(audio) }
        scratch.deallocate()
    }

    func start() throws {
        if node == nil, let audio {
            let rate = Double(yuris_audio_sample_rate())
            guard let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2) else { return }
            let chunk = Self.chunk
            let pointer = UInt(bitPattern: UnsafeRawPointer(audio))
            let buffer = UInt(bitPattern: UnsafeRawPointer(scratch))
            let node = AVAudioSourceNode(format: format) { _, _, frameCount, bufferList -> OSStatus in
                let buffers = UnsafeMutableAudioBufferListPointer(bufferList)
                guard buffers.count >= 2,
                      let left = buffers[0].mData?.assumingMemoryBound(to: Float.self),
                      let right = buffers[1].mData?.assumingMemoryBound(to: Float.self),
                      let audio = OpaquePointer(bitPattern: pointer),
                      let scratch = UnsafeMutablePointer<Float>(bitPattern: buffer) else { return noErr }
                var done = 0
                let total = Int(frameCount)
                while done < total {
                    let n = min(chunk, total - done)
                    yuris_audio_render(audio, scratch, UInt32(n))
                    for i in 0..<n {
                        left[done + i] = scratch[2 * i]
                        right[done + i] = scratch[2 * i + 1]
                    }
                    done += n
                }
                return noErr
            }
            engine.attach(node)
            engine.connect(node, to: engine.mainMixerNode, format: format)
            self.node = node
        }
        if !engine.isRunning {
            try engine.start()
        }
    }

    func pause() {
        engine.pause()
    }

    func stop() {
        engine.stop()
    }
}

/// The CAMetalLayer view wgpu draws into. The runtime fits the game into
/// it with black bars and maps touches (in drawable pixels) back.
final class YurisMetalView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
}

@MainActor
final class YurisViewController: UIViewController {
    let metalView = YurisMetalView(frame: .zero)
    var onMenuGesture: (() -> Void)?
    private(set) var lastDrawablePixels: (UInt32, UInt32) = (0, 0)
    private var handle: OpaquePointer?
    private var screenSize = CGSize(width: 1280, height: 720)
    /// The game's own resolution, fitted into the view.
    var gameSize: CGSize { screenSize }
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
        // Two fingers dragged up and down turn the mouse wheel, which opens
        // and scrolls the backlog: down (pulling earlier text in) is up.
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

    func attach(handle: OpaquePointer, gameSize: CGSize) {
        self.handle = handle
        if gameSize.width > 0 && gameSize.height > 0 {
            screenSize = gameSize
        }
        lastDrawablePixels = (0, 0)
    }

    func detach() {
        if let handle { yuris_detach_surface(handle) }
        handle = nil
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutEngine(scale: view.window?.screen.scale ?? UIScreen.main.scale)
    }

    /// Gives the runtime the view at its current size. False when the
    /// runtime could not draw into it (see yuris_last_error).
    @discardableResult
    func layoutEngine(scale: CGFloat) -> Bool {
        guard let handle, view.bounds.width > 0, view.bounds.height > 0 else { return false }
        metalView.contentScaleFactor = scale
        let pixels = drawablePixels(scale: scale)
        guard pixels.width != lastDrawablePixels.0 || pixels.height != lastDrawablePixels.1 else { return true }
        lastDrawablePixels = (pixels.width, pixels.height)
        (metalView.layer as? CAMetalLayer)?.drawableSize = CGSize(width: Int(pixels.width), height: Int(pixels.height))
        let view = Unmanaged.passUnretained(metalView).toOpaque()
        let status = yuris_set_surface(handle, view, pixels.width, pixels.height)
        if status != 0 {
            AppDiagnostics.shared.event("bridge", "yuris.surfaceFailed", ["error": String(cString: yuris_last_error())])
        }
        return status == 0
    }

    /// The game menu's and quick actions' commands. YU-RIS games draw their
    /// own menus, save screens and auto mode in script, so only the backlog
    /// (the mouse wheel) has a way in.
    func runCommand(_ command: GameCommand) -> Bool {
        guard let handle else { return false }
        switch command {
        case .backlog:
            yuris_wheel(handle, 1)
            return true
        default:
            return false
        }
    }

    func controlState() -> UInt32 {
        handle == nil ? 0 : GameControlState.canBacklog.rawValue
    }

    /// Ctrl held, which YU-RIS games read as skip.
    func setControlSkip(_ held: Bool) -> Bool {
        guard let handle else { return false }
        yuris_key(handle, 0x11, held ? 1 : 0)
        return true
    }

    /// The trackpad cursor, from a point in this view.
    func sendPointer(_ phase: PointerPhase, at point: CGPoint) -> Bool {
        guard let handle else { return false }
        let pixel = pixels(point)
        yuris_pointer(handle, phase.rawValue, pixel.x, pixel.y)
        return true
    }

    func sendSecondaryClickAtCenter() {
        secondaryClick(at: CGPoint(x: view.bounds.midX, y: view.bounds.midY))
    }

    private func secondaryClick(at point: CGPoint) {
        guard let handle else { return }
        let pixel = pixels(point)
        yuris_pointer(handle, PointerPhase.secondaryDown.rawValue, pixel.x, pixel.y)
        yuris_pointer(handle, PointerPhase.secondaryUp.rawValue, pixel.x, pixel.y)
    }

    private func pixels(_ point: CGPoint) -> (x: Float, y: Float) {
        let scale = metalView.contentScaleFactor
        return (Float(point.x * scale), Float(point.y * scale))
    }

    private func send(_ phase: PointerPhase, _ touch: UITouch) {
        guard let handle else { return }
        let pixel = pixels(touch.location(in: view))
        yuris_pointer(handle, phase.rawValue, pixel.x, pixel.y)
    }

    /// Lets go of the left button away from the game, so a touch that
    /// turned into a gesture clicks nothing.
    private func cancelActiveTouch() {
        guard activeTouch != nil, let handle else { return }
        activeTouch = nil
        yuris_pointer(handle, PointerPhase.move.rawValue, -100_000, -100_000)
        yuris_pointer(handle, PointerPhase.up.rawValue, -100_000, -100_000)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // One finger plays; more fingers are the wheel or the menu gesture.
        guard (event?.allTouches?.count ?? 1) == 1 else {
            cancelActiveTouch()
            return
        }
        guard activeTouch == nil, let touch = touches.first else { return }
        activeTouch = touch
        send(.move, touch)
        send(.down, touch)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        send(.move, touch)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        send(.up, touch)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        _ = touch
        cancelActiveTouch()
    }

    @objc private func menuGesture() {
        onMenuGesture?()
    }

    @objc private func longPress(_ recognizer: UILongPressGestureRecognizer) {
        guard recognizer.state == .began else { return }
        cancelActiveTouch()
        secondaryClick(at: recognizer.location(in: view))
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
            yuris_wheel(handle, Int32(steps))
        default:
            wheelRemainder = 0
        }
    }
}

private func withOptionalCString<Result>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> Result) -> Result {
    guard let string else { return body(nil) }
    return string.withCString { body($0) }
}
