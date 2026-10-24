import AVFAudio
import CoreText
import EthornellRuntime
import Foundation
import QuartzCore
import UIKit
import VNCore

enum BGISessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "BGI failed to start.") : String(localized: "BGI failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "BGI stopped.") : String(localized: "BGI stopped: \(message)")
        }
    }
}

/// Runs BGI (Ethornell) games with ethornell-rs (EthornellRuntime), through
/// its host ABI (ethornell_ios.h). Like siglus_rs and rfvp it isn't an SDL
/// program: it renders with wgpu straight into the CAMetalLayer of a view, so
/// the session gives it its own landscape window and steps it from a display
/// link. The orientation, lifecycle and menu overlay are the SDL engines'.
/// Every engine call stays on the main thread: the ABI's handles belong to
/// the thread that created them.
@MainActor
final class NativeBGISession: NSObject, KRKRSession {
    private static let rendererLabel = "ethornell-rs"

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
    private var controller: BGIViewController?
    private var handle: UInt64 = 0
    private var metricsTimer: Timer?
    private var overlayCoordinator: KRKROverlayCoordinator?
    private var isForeground = true
    private var requestedForeground = true
    private var elapsedSeconds = 0
    private var performanceEnabled = false
    private var framesThisSecond = 0
    /// When Mikage asked the game to close: the game may confirm first with
    /// a message box, which Mikage answers itself, as the player already
    /// confirmed in Mikage's menu.
    private var closeRequestedAt: CFTimeInterval?

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

        AppDiagnostics.shared.beginGame(["engine": EngineID.bgi.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
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
        let gbk = configuration.textEncoding == .gbk
        let font = await Self.textFont()
        configureAudioSession(respectSilentMode: configuration.respectSilentMode)

        // The game writes its saves (BGI.gdb, UserData...) to the game's
        // savedata folder, Mikage's saves folder, so Storage can measure and
        // export them. The engine hides that folder from the game's own
        // files.
        let userRoot = gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true)
        try? FileManager.default.createDirectory(at: userRoot, withIntermediateDirectories: true)

        // A patch's launcher (BGI-CHS.exe): the engine reads the translated
        // files packed into it ahead of the archives.
        let launcher = configuration.targetKind == .archive
            && configuration.entryPoint.pathExtension.lowercased() == "exe"
            ? configuration.entryPoint.lastPathComponent : ""

        // The game's own font (font.ttf) draws its text, PingFang what it
        // lacks; Chinese patches re-code their text into codes only their
        // font draws. Should the engine refuse it, start with PingFang alone.
        let gameFont = GameScanner.bgiGameFont(in: gameDirectory)
        var attempts: [(path: String, face: UInt32, fallback: (path: String, face: UInt32)?)] = []
        if let gameFont { attempts.append((gameFont.path, 0, font)) }
        attempts.append((font?.path ?? "", font?.face ?? 0, nil))

        var created: UInt64 = 0
        var status: Int32 = -1
        for attempt in attempts {
            AppDiagnostics.shared.event("bridge", "ethornell.create", [
                "font": attempt.path, "face": String(attempt.face),
                "fallbackFont": attempt.fallback?.path ?? "", "gbk": String(gbk), "launcher": launcher,
            ])
            _ = ethornell_mikage_set_launcher(launcher)
            _ = ethornell_mikage_set_fallback_font(attempt.fallback?.path ?? "", attempt.fallback?.face ?? 0)
            status = withStringV1(gameDirectory.path) { resource in
                withStringV1(userRoot.path) { user in
                    withStringV1(attempt.path) { fontPath in
                        var config = EthornellConfigV1(
                            struct_size: UInt32(MemoryLayout<EthornellConfigV1>.size),
                            abi_version: ETHORNELL_IOS_ABI_VERSION,
                            resource_root: resource,
                            user_root: user,
                            font_path: fontPath,
                            text_encoding: gbk ? 1 : 0,
                            font_face_index: attempt.face,
                            backend: 0,
                            flags: 0,
                            reserved: (0, 0, 0, 0))
                        return ethornell_ios_create(&config, &created)
                    }
                }
            }
            if status == 0, created != 0 { break }
            AppDiagnostics.shared.event("bridge", "ethornell.create.failed", ["status": String(status), "error": Self.lastError()])
        }
        guard status == 0, created != 0 else {
            let message = Self.lastError()
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw BGISessionError.runtime(message)
        }
        handle = created

        let controller = BGIViewController()
        controller.loadViewIfNeeded()
        let engineWindow = UIWindow(windowScene: scene)
        engineWindow.backgroundColor = .black
        engineWindow.rootViewController = controller
        engineWindow.frame = scene.coordinateSpace.bounds
        engineWindow.isHidden = false
        engineWindow.layoutIfNeeded()
        self.controller = controller
        self.engineWindow = engineWindow

        controller.onSurfaceFailure = { [weak self] message in
            self?.stop(success: false, message: message)
        }
        controller.attach(handle: created)
        controller.layoutEngine(scale: scene.screen.scale)
        controller.onMenuGesture = configuration.threeFingerMenu ? { [weak self] in self?.showMenuOverlay() } : nil
        AppDiagnostics.shared.event("bridge", "ethornell.created", ["drawable": "\(controller.lastDrawablePixels.0)x\(controller.lastDrawablePixels.1)"])

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            engineName: EngineID.bgi.displayName,
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
                // BGI games open their menu and close windows on a right click.
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

        frameDriver.start(preferredFramesPerSecond: 60) { [weak self] in self?.step() }
        startMetricsTimer()
        state = .running
        isForeground = true
        if !requestedForeground {
            ethornell_ios_set_paused(created, 1)
            isForeground = false
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// Leaving runs the game's own exit (WM_CLOSE), which writes its global
    /// data; the session ends when the engine reports the game closed. Some
    /// games ask in a window of their own, which the player answers; asking
    /// to leave again a few seconds later quits without the game.
    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        let now = CACurrentMediaTime()
        if handle == 0 || closeRequestedAt.map({ now - $0 > 3 }) == true {
            stop(success: true, message: nil)
            return
        }
        closeRequestedAt = now
        if ethornell_ios_request_close(handle) != 0 {
            stop(success: true, message: nil)
        }
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running, handle != 0 else { return }
        guard isForeground != foreground else { return }
        isForeground = foreground
        if foreground {
            try? AVAudioSession.sharedInstance().setActive(true)
        }
        // Bit 0, app inactive: no narrative ticks, sounds paused.
        ethornell_ios_set_paused(handle, foreground ? 0 : 1)
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

    private func stop(success: Bool, message: String?) {
        guard state == .running || state == .starting else { return }
        state = .stopping
        Task { @MainActor in await finish(success: success, message: message) }
    }

    private func finish(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "ethornell.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        controller?.dismissDialogs()
        if handle != 0 {
            controller?.detach()
            ethornell_ios_destroy(handle)
        }
        handle = 0
        closeRequestedAt = nil
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
            completion?(.failure(BGISessionError.stopped(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    /// One display refresh: the engine runs its ~16 ms ticks itself, then
    /// hands over what the game asked of the host.
    private func step() {
        guard state == .running || state == .stopping, handle != 0 else { return }
        if isForeground {
            if ethornell_ios_step(handle, clock_gettime_nsec_np(CLOCK_UPTIME_RAW)) < 0 {
                stop(success: false, message: Self.lastError())
                return
            }
            framesThisSecond += 1
        }
        var event = EthornellEventV1()
        event.struct_size = UInt32(MemoryLayout<EthornellEventV1>.size)
        while handle != 0, ethornell_ios_poll_event(handle, &event) == 0 {
            let length = min(Int(event.text_len), 2048)
            let text = withUnsafeBytes(of: &event.text) { String(decoding: $0.prefix(length), as: UTF8.self) }
            switch event.kind {
            case 1:
                AppDiagnostics.shared.runtime(source: "BGI.diagnostic", level: 2, message: text)
            case 2:
                if let asked = closeRequestedAt, CACurrentMediaTime() - asked < 2 {
                    // The game's "quit?" box: the player already said yes.
                    AppDiagnostics.shared.event("bridge", "ethornell.closeConfirmed")
                    complete(event.request_id, result: event.code != 0 ? 1 : 0)
                } else {
                    let id = event.request_id
                    controller?.showMessage(text, asks: event.code != 0) { [weak self] result in
                        self?.complete(id, result: result)
                    }
                }
            case 3:
                stop(success: true, message: nil)
                return
            case 4:
                AppDiagnostics.shared.event("bridge", "ethornell.title", ["title": text])
            case 5:
                let id = event.request_id
                guard let request = requestPayload(id) else {
                    completeDialog(id, answer: nil)
                    continue
                }
                controller?.showDialog(request) { [weak self] answer in
                    self?.completeDialog(id, answer: answer)
                }
            default:
                break
            }
        }
    }

    private func complete(_ id: UInt64, result: Int32) {
        guard handle != 0 else { return }
        var response = EthornellResponseV1(
            struct_size: UInt32(MemoryLayout<EthornellResponseV1>.size),
            result: result,
            text: EthornellStringV1(ptr: nil, len: 0, reserved: 0))
        if ethornell_ios_complete_request(handle, id, &response) != 0 {
            AppDiagnostics.shared.event("bridge", "ethornell.completeFailed", ["error": Self.lastError()])
        }
    }

    /// A dialog's JSON description (ABI.md, "Input dialogs").
    private func requestPayload(_ id: UInt64) -> [String: Any]? {
        let size = ethornell_ios_request_payload(handle, id, nil, 0)
        guard size > 1 else { return nil }
        var buffer = [CChar](repeating: 0, count: Int(size))
        guard ethornell_ios_request_payload(handle, id, &buffer, UInt32(size)) == size else { return nil }
        let data = Data(String(cString: buffer).utf8)
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }

    /// Answers a dialog; nil cancels it.
    private func completeDialog(_ id: UInt64, answer: [String: Any]?) {
        guard handle != 0 else { return }
        var object = answer ?? [:]
        object["accepted"] = answer != nil
        guard let data = try? JSONSerialization.data(withJSONObject: object),
              let json = String(data: data, encoding: .utf8) else { return }
        let status = withStringV1(json) { text in
            var response = EthornellResponseV1(
                struct_size: UInt32(MemoryLayout<EthornellResponseV1>.size),
                result: 0,
                text: text)
            return ethornell_ios_complete_request(handle, id, &response)
        }
        if status != 0 {
            AppDiagnostics.shared.event("bridge", "ethornell.completeFailed", ["error": Self.lastError()])
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

    /// The one face ethornell-rs draws all text with. As for Siglus and FVP,
    /// PingFang (Traditional on Traditional Chinese devices) stands in for the
    /// MS fonts the games ask for, for Chinese translations and Japanese
    /// originals alike; Hiragino Sans if PingFang can't be loaded.
    private static func textFont() async -> (path: String, face: UInt32)? {
        let main = Locale.preferredLanguages.first.map(SystemFontExport.prefersTraditionalChinese) == true
            ? "PingFangTC-Regular" : "PingFangSC-Regular"
        // Rebuilding a face takes a few seconds the first time.
        return await Task.detached(priority: .userInitiated) { () -> (path: String, face: UInt32)? in
            for name in [main, "HiraginoSans-W3"] {
                guard let file = SystemFontExport.fontFiles(name).first else { continue }
                let face = faceIndex(of: file.face, in: file.path)
                AppDiagnostics.shared.event("bridge", "ethornell.font", ["name": name, "path": file.path, "face": String(face)])
                return (file.path, face)
            }
            return nil
        }.value
    }

    /// The index of the face named `postScriptName` in a font collection;
    /// 0 for a single font or an empty name (a rebuilt copy).
    nonisolated private static func faceIndex(of postScriptName: String, in path: String) -> UInt32 {
        guard !postScriptName.isEmpty,
              let faces = CTFontManagerCreateFontDescriptorsFromURL(URL(fileURLWithPath: path) as CFURL) as? [CTFontDescriptor] else {
            return 0
        }
        let index = faces.firstIndex {
            CTFontDescriptorCopyAttribute($0, kCTFontNameAttribute) as? String == postScriptName
        }
        return UInt32(index ?? 0)
    }

    static func lastError() -> String {
        let size = ethornell_ios_last_error(nil, 0)
        guard size > 1 else { return "" }
        var buffer = [CChar](repeating: 0, count: Int(size))
        _ = ethornell_ios_last_error(&buffer, UInt32(size))
        return String(cString: buffer)
    }
}

/// The view wgpu renders into. ethornell-rs letterboxes the game in it and
/// takes input in its drawable pixels.
final class BGIMetalView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
}

@MainActor
final class BGIViewController: UIViewController {
    let metalView = BGIMetalView(frame: .zero)
    var onMenuGesture: (() -> Void)?
    /// A new surface size the engine refused.
    var onSurfaceFailure: ((String) -> Void)?
    private(set) var lastDrawablePixels: (UInt32, UInt32) = (0, 0)
    private var handle: UInt64 = 0
    private var generation: UInt64 = 0
    /// The game's own resolution, letterboxed into the view.
    var gameSize: CGSize {
        var width: UInt32 = 0
        var height: UInt32 = 0
        guard handle != 0, ethornell_mikage_screen_size(handle, &width, &height) == 0 else { return .zero }
        return CGSize(width: Int(width), height: Int(height))
    }
    private var activeTouch: UITouch?
    private var wheelRemainder: CGFloat = 0
    /// Points of two-finger travel per wheel step.
    private let wheelStep: CGFloat = 40
    private var dialogs: [UIAlertController] = []

    private static let leftButton: Int32 = 1
    private static let rightButton: Int32 = 2
    private static let controlKey: Int32 = 0x11

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
        // Touch is the left button; a long press stands for the right click
        // games use for their menus.
        let longPress = UILongPressGestureRecognizer(target: self, action: #selector(longPress(_:)))
        longPress.minimumPressDuration = 0.5
        longPress.cancelsTouchesInView = true
        view.addGestureRecognizer(longPress)
        // Two fingers dragged up and down turn the mouse wheel, which BGI
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
    override var canBecomeFirstResponder: Bool { true }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        becomeFirstResponder()
    }

    func attach(handle: UInt64) {
        self.handle = handle
        lastDrawablePixels = (0, 0)
    }

    /// Before the engine is destroyed: its last surface goes first.
    func detach() {
        if handle != 0, generation != 0 {
            ethornell_ios_detach_surface(handle, generation)
        }
        handle = 0
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutEngine(scale: view.window?.screen.scale ?? UIScreen.main.scale)
    }

    /// A new size is a new surface for the engine, with a higher generation.
    func layoutEngine(scale: CGFloat) {
        guard handle != 0, view.bounds.width > 0, view.bounds.height > 0 else { return }
        metalView.contentScaleFactor = scale
        let pixels = (UInt32((view.bounds.width * scale).rounded()), UInt32((view.bounds.height * scale).rounded()))
        guard pixels != lastDrawablePixels else { return }
        lastDrawablePixels = pixels
        generation += 1
        let viewPointer = Unmanaged.passUnretained(metalView).toOpaque()
        if ethornell_ios_set_surface(handle, viewPointer, pixels.0, pixels.1, generation) != 0 {
            onSurfaceFailure?(NativeBGISession.lastError())
        }
    }

    // MARK: Commands

    /// The game menu's and quick actions' commands. BGI games draw their own
    /// menus, save screens and auto mode in script: the backlog is the mouse
    /// wheel, and save/load go through the game's icon bar where the engine
    /// knows it (it clicks the icon, so the game's own screens follow).
    func runCommand(_ command: GameCommand) -> Bool {
        guard handle != 0 else { return false }
        switch command {
        case .backlog:
            input(6, value: 1)
            return true
        case .save, .load, .quickSave, .quickLoad:
            guard let id = Self.gameCommandID(command) else { return false }
            return ethornell_mikage_run_game_command(handle, id) == 1
        default:
            return false
        }
    }

    func controlState() -> UInt32 {
        guard handle != 0 else { return 0 }
        var state = GameControlState.canBacklog
        var enabled: UInt32 = 0
        if ethornell_mikage_game_commands(handle, &enabled) == 0 {
            for (command, flag) in [(GameCommand.save, GameControlState.canSave),
                                    (.load, .canLoad),
                                    (.quickSave, .canQuickSave),
                                    (.quickLoad, .canQuickLoad)] {
                if let id = Self.gameCommandID(command), enabled & (1 << (id - 1)) != 0 {
                    state.insert(flag)
                }
            }
        }
        return state.rawValue
    }

    /// ethornell_mikage_run_game_command's ids.
    private static func gameCommandID(_ command: GameCommand) -> UInt32? {
        switch command {
        case .save: return 1
        case .load: return 2
        case .quickSave: return 3
        case .quickLoad: return 4
        default: return nil
        }
    }

    /// Ctrl held, which BGI games read as skip.
    func setControlSkip(_ held: Bool) -> Bool {
        guard handle != 0 else { return false }
        input(held ? 4 : 5, code: Self.controlKey)
        return true
    }

    /// The trackpad cursor, from a point in this view.
    func sendPointer(_ phase: PointerPhase, at point: CGPoint) -> Bool {
        guard handle != 0 else { return false }
        input(1, at: point)
        switch phase {
        case .move: break
        case .down: input(2, at: point, code: Self.leftButton)
        case .up: input(3, at: point, code: Self.leftButton)
        case .secondaryDown: input(2, at: point, code: Self.rightButton)
        case .secondaryUp: input(3, at: point, code: Self.rightButton)
        }
        return true
    }

    func sendSecondaryClickAtCenter() {
        secondaryClick(at: CGPoint(x: view.bounds.midX, y: view.bounds.midY))
    }

    private func secondaryClick(at point: CGPoint) {
        guard handle != 0 else { return }
        input(1, at: point)
        input(2, at: point, code: Self.rightButton)
        input(3, at: point, code: Self.rightButton)
    }

    /// kind: 1 move, 2 button down, 3 button up, 4 key down, 5 key up,
    /// 6 wheel, 7 cancel. Points become drawable pixels.
    private func input(_ kind: UInt32, at point: CGPoint = .zero, code: Int32 = 0, value: Float = 0) {
        guard handle != 0 else { return }
        let scale = metalView.contentScaleFactor
        var event = EthornellInputV1(
            struct_size: UInt32(MemoryLayout<EthornellInputV1>.size),
            kind: kind,
            x: Float(point.x * scale), y: Float(point.y * scale),
            code: code, value: value, reserved: (0, 0))
        ethornell_ios_input(handle, &event)
    }

    // MARK: The game's message boxes and dialogs

    /// A MessageBox: yes/no (1/0) when `asks`, else a message with OK.
    func showMessage(_ text: String, asks: Bool, answer: @escaping (Int32) -> Void) {
        let alert = UIAlertController(title: nil, message: text, preferredStyle: .alert)
        if asks {
            alert.addAction(dialogAction(String(localized: "Yes")) { answer(1) })
            alert.addAction(dialogAction(String(localized: "No"), style: .cancel) { answer(0) })
        } else {
            alert.addAction(dialogAction(String(localized: "OK")) { answer(0) })
        }
        enqueue(alert)
    }

    /// An input dialog (ABI.md): text fields, a list or a date. The engine
    /// filters and limits the answers itself; nil cancels.
    func showDialog(_ request: [String: Any], answer: @escaping ([String: Any]?) -> Void) {
        let type = request["type"] as? String ?? ""
        let alert = UIAlertController(title: request["title"] as? String, message: request["prompt"] as? String, preferredStyle: .alert)
        let texts = { [weak alert] in (alert?.textFields ?? []).map { $0.text ?? "" } }
        let addField = { (placeholder: String, initial: String, numeric: Bool) in
            alert.addTextField { field in
                field.placeholder = placeholder
                field.text = initial
                field.keyboardType = numeric ? .numbersAndPunctuation : .default
            }
        }
        switch type {
        case "input":
            for field in request["fields"] as? [[String: Any]] ?? [] {
                addField(field["label"] as? String ?? "", field["initial"] as? String ?? "", field["numeric"] as? Bool ?? false)
            }
            alert.addAction(dialogAction(String(localized: "OK")) { answer(["texts": texts()]) })
        case "segmented":
            let count = request["segments"] as? Int ?? 0
            let numeric = request["numeric"] as? Bool ?? false
            for index in 0..<count {
                addField("\(index + 1)/\(count)", "", numeric)
            }
            alert.addAction(dialogAction(String(localized: "OK")) { answer(["texts": texts()]) })
        case "select":
            for (index, option) in (request["options"] as? [String] ?? []).enumerated() {
                alert.addAction(dialogAction(option) { answer(["index": index]) })
            }
        case "date":
            let fields = request["fields"] as? [String] ?? []
            for index in 0..<4 {
                addField(String(localized: "Field \(index + 1)"), index < fields.count ? fields[index] : "", false)
            }
            addField(String(localized: "Month (1-12)"), String((request["month"] as? Int ?? 0) + 1), true)
            addField(String(localized: "Day"), String((request["day"] as? Int ?? 0) + 1), true)
            alert.addAction(dialogAction(String(localized: "OK")) {
                let values = texts()
                let number = { (index: Int) in index < values.count ? Int(values[index].trimmingCharacters(in: .whitespaces)) ?? 1 : 1 }
                answer(["texts": Array(values.prefix(4)), "month": number(4) - 1, "day": number(5) - 1])
            })
        default:
            answer(nil)
            return
        }
        alert.addAction(dialogAction(String(localized: "Cancel"), style: .cancel) { answer(nil) })
        enqueue(alert)
    }

    /// An alert button that answers, then shows the next waiting alert.
    private func dialogAction(_ title: String, style: UIAlertAction.Style = .default, _ handler: @escaping () -> Void) -> UIAlertAction {
        UIAlertAction(title: title, style: style) { [weak self] _ in
            handler()
            guard let self, !self.dialogs.isEmpty else { return }
            self.dialogs.removeFirst()
            // After this alert's dismissal has finished.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { [weak self] in self?.presentNextDialog() }
        }
    }

    /// Alerts wait their turn: the engine can ask again before one closes.
    private func enqueue(_ alert: UIAlertController) {
        dialogs.append(alert)
        if dialogs.count == 1 { presentNextDialog() }
    }

    private func presentNextDialog() {
        guard let next = dialogs.first, presentedViewController == nil else { return }
        present(next, animated: true)
    }

    func dismissDialogs() {
        dialogs.removeAll()
        presentedViewController?.dismiss(animated: false)
    }

    // MARK: Touches

    private func cancelActiveTouch() {
        guard activeTouch != nil else { return }
        activeTouch = nil
        input(7)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // One finger plays; more fingers are the wheel or the menu gesture.
        guard (event?.allTouches?.count ?? 1) == 1 else {
            cancelActiveTouch()
            return
        }
        guard activeTouch == nil, let touch = touches.first else { return }
        activeTouch = touch
        let point = touch.location(in: view)
        input(1, at: point)
        input(2, at: point, code: Self.leftButton)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        input(1, at: touch.location(in: view))
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        let point = touch.location(in: view)
        input(1, at: point)
        input(3, at: point, code: Self.leftButton)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
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
            guard abs(translation.y) >= abs(translation.x) else { return }
            wheelRemainder += translation.y
            let steps = Int(wheelRemainder / wheelStep)
            guard steps != 0 else { return }
            wheelRemainder -= CGFloat(steps) * wheelStep
            input(1, at: recognizer.location(in: view))
            input(6, value: Float(steps))
        default:
            wheelRemainder = 0
        }
    }

    // MARK: Hardware keyboards

    /// Windows virtual-key codes, which the scripts bind input classes to.
    private static func virtualKey(_ key: UIKey) -> Int32? {
        switch key.keyCode {
        case .keyboardReturnOrEnter, .keypadEnter: return 0x0D
        case .keyboardEscape: return 0x1B
        case .keyboardSpacebar: return 0x20
        case .keyboardTab: return 0x09
        case .keyboardDeleteOrBackspace: return 0x08
        case .keyboardLeftShift, .keyboardRightShift: return 0x10
        case .keyboardLeftControl, .keyboardRightControl: return 0x11
        case .keyboardPageUp: return 0x21
        case .keyboardPageDown: return 0x22
        case .keyboardLeftArrow: return 0x25
        case .keyboardUpArrow: return 0x26
        case .keyboardRightArrow: return 0x27
        case .keyboardDownArrow: return 0x28
        default:
            let code = key.keyCode.rawValue
            if (UIKeyboardHIDUsage.keyboardF1.rawValue...UIKeyboardHIDUsage.keyboardF12.rawValue).contains(code) {
                return 0x70 + Int32(code - UIKeyboardHIDUsage.keyboardF1.rawValue)
            }
            if (UIKeyboardHIDUsage.keyboardA.rawValue...UIKeyboardHIDUsage.keyboardZ.rawValue).contains(code) {
                return 0x41 + Int32(code - UIKeyboardHIDUsage.keyboardA.rawValue)
            }
            return nil
        }
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var unhandled = Set<UIPress>()
        for press in presses {
            if let key = press.key, let code = Self.virtualKey(key), handle != 0 {
                input(4, code: code)
            } else {
                unhandled.insert(press)
            }
        }
        if !unhandled.isEmpty { super.pressesBegan(unhandled, with: event) }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var unhandled = Set<UIPress>()
        for press in presses {
            if let key = press.key, let code = Self.virtualKey(key), handle != 0 {
                input(5, code: code)
            } else {
                unhandled.insert(press)
            }
        }
        if !unhandled.isEmpty { super.pressesEnded(unhandled, with: event) }
    }
}

/// The ABI's borrowed UTF-8 string, valid for the duration of `body`.
private func withStringV1<Result>(_ text: String, _ body: (EthornellStringV1) -> Result) -> Result {
    var bytes = Array(text.utf8)
    return bytes.withUnsafeMutableBufferPointer { buffer in
        body(EthornellStringV1(ptr: UnsafePointer(buffer.baseAddress), len: UInt32(buffer.count), reserved: 0))
    }
}
