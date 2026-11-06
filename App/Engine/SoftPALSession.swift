import AVFAudio
import AVFoundation
import Foundation
import QuartzCore
import UIKit
import VNCore
import SoftPALRuntime

enum SoftPALSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "SoftPAL failed to start.") : String(localized: "SoftPAL failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "SoftPAL stopped.") : String(localized: "SoftPAL stopped: \(message)")
        }
    }
}

/// Runs SoftPAL games (PAL engine) with MikageSoftPAL (SoftPALRuntime),
/// through its C interface (sena_mobile.h). As with MikageCMVS, the
/// runtime draws with wgpu into the CAMetalLayer of a view, so the session
/// gives it its own landscape window and steps it from a display link, and
/// it has no audio output of its own: the session pulls its mixer from an
/// AVAudioEngine source node. Button reactions and voice waits finish as
/// their sound plays, so it is pulled whenever the game runs. The
/// orientation, lifecycle and menu overlay are the SDL engines'.
@MainActor
final class NativeSoftPALSession: NSObject, KRKRSession {
    private static let rendererLabel = "MikageSoftPAL"

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
    private var controller: SoftPALViewController?
    private var handle: OpaquePointer?
    private var audio: SoftPALAudioOutput?
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
        AppDiagnostics.shared.beginGame(["engine": EngineID.softpal.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
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
        // Storage can measure and export them. The runtime copies the
        // original's save folder into it the first time.
        let saveDirectory = gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true)

        // Automatic: what a translation patch in the folder implies, else
        // Shift-JIS.
        let encoding: UInt32
        switch configuration.textEncoding {
        case .shiftJIS: encoding = 1 // SENA_MOBILE_ENCODING_SHIFT_JIS
        case .gbk: encoding = 2 // SENA_MOBILE_ENCODING_GBK
        case .utf8: encoding = 3 // SENA_MOBILE_ENCODING_UTF8
        case nil: encoding = 0 // SENA_MOBILE_ENCODING_AUTOMATIC
        }
        AppDiagnostics.shared.event("bridge", "softpal.open", ["encoding": String(encoding)])
        // Opening boots the game's script (data.pac). The session belongs to
        // the thread that opened it, so it opens here on the main thread.
        // The language is automatic: the Chinese text of the multi-language
        // releases when the folder has their archives, a choice made in the
        // game's own settings first.
        let session = gameDirectory.path.withCString { game in
            saveDirectory.path.withCString { save in
                var config = SenaMobileConfig(
                    struct_size: UInt32(MemoryLayout<SenaMobileConfig>.size),
                    game_dir: game,
                    save_dir: save,
                    encoding: encoding,
                    language: 0) // SENA_MOBILE_LANGUAGE_AUTOMATIC
                return sena_mobile_open(&config)
            }
        }
        guard let created = session else {
            let message = String(cString: sena_mobile_last_error())
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw SoftPALSessionError.runtime(message)
        }

        let controller = SoftPALViewController()
        controller.loadViewIfNeeded()
        let engineWindow = UIWindow(windowScene: scene)
        engineWindow.backgroundColor = .black
        engineWindow.rootViewController = controller
        engineWindow.frame = scene.coordinateSpace.bounds
        engineWindow.isHidden = false
        // Laid out now but kept clear; showEngineWindow fades it in over the
        // launch stage.
        engineWindow.alpha = 0
        engineWindow.layoutIfNeeded()

        var width: UInt32 = 0
        var height: UInt32 = 0
        sena_mobile_screen_size(created, &width, &height)
        controller.attach(handle: created, gameSize: CGSize(width: Int(width), height: Int(height)))
        let scale = scene.screen.scale
        guard controller.layoutEngine(scale: scale) else {
            let message = String(cString: sena_mobile_last_error())
            sena_mobile_close(created)
            engineWindow.isHidden = true
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw SoftPALSessionError.runtime(message)
        }
        handle = created
        self.controller = controller
        self.engineWindow = engineWindow
        controller.onMenuGesture = configuration.threeFingerMenu ? { [weak self] in self?.showMenuOverlay() } : nil
        AppDiagnostics.shared.event("bridge", "softpal.created", ["screen": "\(width)x\(height)"])

        let output = SoftPALAudioOutput(session: created)
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
            engineName: EngineID.softpal.displayName,
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
                // The game saves its settings as they change; Mikage closes
                // it itself.
                self?.requestStop()
            },
            onBack: { [weak self] in
                // SoftPAL games hide the text window, open their menu and
                // close screens on a right click.
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
        if let handle { sena_mobile_set_focused(handle, foreground ? 1 : 0) }
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

    /// The game's last frame, drawn on the CPU by the runtime (the Metal
    /// layer can't be captured from UIKit), fitted into the window with
    /// black bars like the game, under the overlay.
    func snapshot() -> UIImage? {
        guard let window = engineWindow, !window.bounds.isEmpty else { return nil }
        let frame = handle.flatMap { Self.frameImage($0) }
        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
        return renderer.image { _ in
            UIColor.black.setFill()
            UIRectFill(window.bounds)
            if let frame {
                frame.draw(in: AVMakeRect(aspectRatio: frame.size, insideRect: window.bounds))
            } else {
                controller?.metalView.drawHierarchy(in: window.bounds, afterScreenUpdates: false)
            }
            overlayCoordinator?.drawScreenshotOverlay(in: window)
        }
    }

    private static func frameImage(_ handle: OpaquePointer) -> UIImage? {
        var width: UInt32 = 0
        var height: UInt32 = 0
        guard let pixels = sena_mobile_snapshot(handle, &width, &height), width > 0, height > 0 else { return nil }
        let data = Data(bytes: pixels, count: Int(width) * Int(height) * 4)
        guard let provider = CGDataProvider(data: data as CFData),
              let image = CGImage(width: Int(width), height: Int(height), bitsPerComponent: 8, bitsPerPixel: 32,
                                  bytesPerRow: Int(width) * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
        else { return nil }
        return UIImage(cgImage: image)
    }

    private func finish(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "softpal.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        audio?.stop()
        audio = nil
        if let handle {
            controller?.detach()
            sena_mobile_close(handle)
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
            completion?(.failure(SoftPALSessionError.stopped(detail)))
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
        let status = sena_mobile_step(handle, UInt32((elapsed * 1000).rounded()))
        framesThisSecond += 1
        switch status {
        case 0: // SENA_MOBILE_RUNNING
            break
        case 1: // SENA_MOBILE_ENDED
            state = .stopping
            Task { @MainActor in await finish(success: true, message: nil) }
        default:
            let message = String(cString: sena_mobile_last_error())
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

    /// The runtime bundles no font: it draws what the game's own bitmap
    /// font (DEFAULT_FONT.DAT) lacks, and the system font types, with the
    /// last face registered. As for CMVS, that is PingFang (Traditional on
    /// Traditional Chinese devices); Hiragino Sans goes first, so it stays
    /// when PingFang can't be loaded.
    private static func registerSystemFonts() async {
        let main = Locale.preferredLanguages.first.map(SystemFontExport.prefersTraditionalChinese) == true
            ? "PingFangTC-Regular" : "PingFangSC-Regular"
        let names = ["HiraginoSans-W3", main]
        // Rebuilding a face takes a few seconds the first time.
        let files = await Task.detached(priority: .userInitiated) {
            names.map { ($0, SystemFontExport.fontFiles($0)) }
        }.value
        for (name, faces) in files {
            for (path, face) in faces {
                let loaded = path.withCString { path in face.withCString { sena_mobile_register_font(path, $0) } }
                AppDiagnostics.shared.event("bridge", "softpal.font", ["name": name, "path": path, "loaded": String(loaded != 0)])
            }
        }
    }
}

/// Plays the runtime's mixer: an AVAudioEngine source node pulls it at its
/// own rate (48 kHz stereo) on the audio thread.
final class SoftPALAudioOutput {
    private static let chunk = 1024
    private let engine = AVAudioEngine()
    private let audio: OpaquePointer?
    private var node: AVAudioSourceNode?
    /// Interleaved samples from the runtime, split into the node's two
    /// channel buffers. Allocated here, outside the audio thread.
    private let scratch = UnsafeMutablePointer<Float>.allocate(capacity: chunk * 2)

    init(session: OpaquePointer) {
        audio = sena_mobile_audio(session)
    }

    deinit {
        engine.stop()
        if let audio { sena_mobile_audio_release(audio) }
        scratch.deallocate()
    }

    func start() throws {
        if node == nil, let audio {
            let rate = Double(sena_mobile_audio_sample_rate())
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
                    sena_mobile_audio_render(audio, scratch, UInt32(n))
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
final class SoftPALMetalView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
}

@MainActor
final class SoftPALViewController: UIViewController {
    let metalView = SoftPALMetalView(frame: .zero)
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
        if let handle { sena_mobile_detach_surface(handle) }
        handle = nil
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutEngine(scale: view.window?.screen.scale ?? UIScreen.main.scale)
    }

    /// Gives the runtime the view at its current size. False when the
    /// runtime could not draw into it (see sena_mobile_last_error).
    @discardableResult
    func layoutEngine(scale: CGFloat) -> Bool {
        guard let handle, view.bounds.width > 0, view.bounds.height > 0 else { return false }
        metalView.contentScaleFactor = scale
        let pixels = drawablePixels(scale: scale)
        guard pixels.width != lastDrawablePixels.0 || pixels.height != lastDrawablePixels.1 else { return true }
        lastDrawablePixels = (pixels.width, pixels.height)
        (metalView.layer as? CAMetalLayer)?.drawableSize = CGSize(width: Int(pixels.width), height: Int(pixels.height))
        let view = Unmanaged.passUnretained(metalView).toOpaque()
        let status = sena_mobile_set_surface(handle, view, pixels.width, pixels.height)
        if status != 0 {
            AppDiagnostics.shared.event("bridge", "softpal.surfaceFailed", ["error": String(cString: sena_mobile_last_error())])
        }
        return status == 0
    }

    /// The game menu's and quick actions' commands. SoftPAL games draw their
    /// own menus, save screens and auto mode in script, so only the backlog
    /// (the mouse wheel up) has a way in.
    func runCommand(_ command: GameCommand) -> Bool {
        guard let handle else { return false }
        switch command {
        case .backlog:
            sena_mobile_wheel(handle, 1)
            return true
        default:
            return false
        }
    }

    func controlState() -> UInt32 {
        handle == nil ? 0 : GameControlState.canBacklog.rawValue
    }

    /// Ctrl held, which SoftPAL games read as skip.
    func setControlSkip(_ held: Bool) -> Bool {
        guard let handle else { return false }
        sena_mobile_key(handle, 0x11, held ? 1 : 0)
        return true
    }

    /// The trackpad cursor, from a point in this view.
    func sendPointer(_ phase: PointerPhase, at point: CGPoint) -> Bool {
        guard let handle else { return false }
        let pixel = pixels(point)
        sena_mobile_pointer(handle, phase.rawValue, pixel.x, pixel.y)
        return true
    }

    func sendSecondaryClickAtCenter() {
        secondaryClick(at: CGPoint(x: view.bounds.midX, y: view.bounds.midY))
    }

    private func secondaryClick(at point: CGPoint) {
        guard let handle else { return }
        let pixel = pixels(point)
        sena_mobile_pointer(handle, PointerPhase.secondaryDown.rawValue, pixel.x, pixel.y)
        sena_mobile_pointer(handle, PointerPhase.secondaryUp.rawValue, pixel.x, pixel.y)
    }

    private func pixels(_ point: CGPoint) -> (x: Float, y: Float) {
        let scale = metalView.contentScaleFactor
        return (Float(point.x * scale), Float(point.y * scale))
    }

    private func send(_ phase: PointerPhase, _ touch: UITouch) {
        guard let handle else { return }
        let pixel = pixels(touch.location(in: view))
        sena_mobile_pointer(handle, phase.rawValue, pixel.x, pixel.y)
    }

    /// Lets go of the left button away from the game, so a touch that
    /// turned into a gesture clicks nothing.
    private func cancelActiveTouch() {
        guard activeTouch != nil, let handle else { return }
        activeTouch = nil
        sena_mobile_pointer(handle, PointerPhase.move.rawValue, -100_000, -100_000)
        sena_mobile_pointer(handle, PointerPhase.up.rawValue, -100_000, -100_000)
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
            sena_mobile_wheel(handle, Int32(steps))
        default:
            wheelRemainder = 0
        }
    }
}
