import Foundation
import UIKit
import RenPyRuntime
import VNCore

enum RenPySessionError: LocalizedError {
    case runtime(String)
    case stopped(String)
    case restartRequired

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "Ren’Py failed to start.") : String(localized: "Ren’Py failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "Ren’Py stopped.") : String(localized: "Ren’Py stopped: \(message)")
        case .restartRequired:
            return String(localized: "Ren’Py can run one game each time Mikage opens. Close Mikage from the app switcher and open it again to play another Ren’Py game.")
        }
    }
}

/// Runs Ren'Py (RenPyRuntime: CPython and Ren'Py on the shared SDL3, with
/// OpenGL ES on MetalANGLE) in its own landscape window. Ren'Py keeps its own
/// main loop, which the runtime runs on a separate stack and resumes once per
/// display refresh, so it uses the shared SDL host's presentation, overlay and
/// lifecycle handling like the other engines.
@MainActor
final class NativeRenPySession: NSObject, KRKRSession {
    private static let rendererLabel = "Ren’Py"

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
    private weak var engineWindow: UIWindow?
    private var metricsTimer: Timer?
    private var overlayCoordinator: KRKROverlayCoordinator?
    private var isForeground = true
    private var requestedForeground = true
    private var elapsedSeconds = 0
    private var performanceEnabled = false
    private let threadCPU = ThreadCPUSampler()

    override init() {
        super.init()
        lifecycleObserver = SDLHostLifecycleObserver { [weak self] active in
            self?.setForeground(active)
        }
    }

    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) async throws {
        guard state == .idle || isFailed else { throw KRKRSessionError.alreadyRunning }
        // Python can't start twice in one process.
        guard MikageRenPyCanStart() else { throw RenPySessionError.restartRequired }
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

        AppDiagnostics.shared.beginGame(["engine": EngineID.renpy.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
                                         "performanceHUD": String(configuration.performance),
                                         "floatingButton": String(configuration.floatingButton),
                                         "threeFingerMenu": String(configuration.threeFingerMenu),
                                         "respectSilentMode": String(configuration.respectSilentMode)])
        AppDiagnostics.shared.windows("launch.hostWindow")
        state = .preparingOrientation
        presentation.begin(hostWindow: window, scene: scene)
        do {
            try await presentation.prepareLandscape(scene: scene, window: window, controller: viewController)
        } catch {
            state = .failed(error.localizedDescription)
            await presentation.restorePreviousOrientation()
            throw error
        }

        // Saves stay in the game's own savedata folder (Ren'Py's --savedir),
        // as for KiriKiri, so Storage can measure, export and keep them.
        let saveDirectory = gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true)
        try FileManager.default.createDirectory(at: saveDirectory, withIntermediateDirectories: true)

        state = .starting
        let context = Unmanaged.passUnretained(self).toOpaque()
        let gamePath = gameDirectory.path
        let savePath = saveDirectory.path
        let respectSilentMode = configuration.respectSilentMode
        let menuGesture = configuration.threeFingerMenu
        AppDiagnostics.shared.event("bridge", "renpy.start.scheduledOnRunLoop")
        let started = await SDLHostMainRunLoop.perform {
            gamePath.withCString { game in
                savePath.withCString { saves in
                    var options = MikageRenPyLaunchOptions(
                        gamePath: game,
                        saveDirectory: saves,
                        respectSilentMode: respectSilentMode,
                        menuGestureEnabled: menuGesture
                    )
                    return MikageRenPyStart(&options, mikageRenPyMenuCallback, mikageRenPyCompletionCallback, context)
                }
            }
        }
        AppDiagnostics.shared.event("bridge", "renpy.start.returned", [
            "success": String(started), "version": String(cString: MikageRenPyVersion())
        ])
        guard started else {
            let message = String(cString: MikageRenPyLastError())
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw RenPySessionError.runtime(message)
        }
        let startupWarning = String(cString: MikageRenPyLastError())
        if !startupWarning.isEmpty {
            onWarning?(KRKRSessionError.audioSession(startupWarning))
        }

        if let nativeWindow = MikageRenPyNativeWindow() {
            engineWindow = Unmanaged<UIWindow>.fromOpaque(nativeWindow).takeUnretainedValue()
        }
        guard let engineWindow else {
            AppDiagnostics.shared.event("UIKit", "SDL.window.missing")
            // Wind the engine down; its completion finds the session failed
            // and leaves the return to the library to this error.
            MikageRenPyRequestStop()
            while MikageRenPyIsRunning() { _ = MikageRenPyStep() }
            let message = String(localized: "Ren’Py didn’t create a usable window.")
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw RenPySessionError.runtime(message)
        }

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            engineName: EngineID.renpy.displayName,
            performanceVisible: configuration.performance,
            performanceDetail: configuration.performanceDetail,
            renderer: Self.rendererLabel
        )
        overlay.install(
            in: engineWindow,
            options: GameOverlayOptions(configuration: configuration),
            controls: GameControls(
                run: { MikageRenPyRunCommand($0.rawValue) },
                state: { GameControlState(rawValue: MikageRenPyGameControls()) },
                setControlSkip: { MikageRenPySetControlSkip($0) },
                sendPointer: { MikageRenPySendPointer($0.rawValue, Float($1.x), Float($1.y)) }
            ),
            onExit: { [weak self] in
                self?.overlayCoordinator?.hideMenu()
                self?.requestNativeExit()
            },
            onBack: {
                // Escape opens Ren'Py's game menu, or steps back out of one.
                let sent = MikageRenPySendBack()
                AppDiagnostics.shared.event("session", "back.requested", ["sent": String(sent)])
            },
            onMarkFinished: { [weak self] in
                AppDiagnostics.shared.event("session", "markFinished.requested")
                self?.onMarkFinished?()
                self?.overlayCoordinator?.hideMenu()
                self?.requestNativeExit()
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
            _ = MikageRenPySetForeground(false)
            isForeground = false
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// Ren'Py saves its persistent data as it quits, so leaving the game asks
    /// it to quit (without its own "Are you sure?" prompt).
    private func requestNativeExit() {
        guard state == .running else { return }
        AppDiagnostics.shared.event("session", "nativeExit.requested")
        requestStop()
    }

    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        MikageRenPyRequestStop()
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running || state == .stopping else { return }
        guard isForeground != foreground else { return }
        let changed = MikageRenPySetForeground(foreground)
        if !foreground || changed {
            isForeground = foreground
        }
        if !changed {
            let detail = String(cString: MikageRenPyLastError())
            if !detail.isEmpty {
                onWarning?(KRKRSessionError.audioSession(detail))
            }
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

    /// The engine draws with OpenGL ES on MetalANGLE, which UIKit snapshots
    /// can't read back; screenshots of Ren'Py games aren't available yet.
    func snapshot() -> UIImage? {
        nil
    }

    fileprivate func runtimeRequestedMenu() {
        guard state == .running else { return }
        showMenuOverlay()
    }

    fileprivate func runtimeFinished(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "renpy.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        overlayCoordinator?.remove()
        overlayCoordinator = nil
        engineWindow = nil

        state = .restoringOrientation
        onReturningToLibrary?()
        presentation.showHostWindow()
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
            completion?(.failure(RenPySessionError.stopped(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func step() {
        guard state == .running || state == .stopping else { return }
        _ = MikageRenPyStep()
    }

    private func setPerformanceHUD(_ enabled: Bool) {
        performanceEnabled = enabled
        overlayCoordinator?.setPerformanceVisible(performanceEnabled)
    }

    private func startMetricsTimer() {
        metricsTimer?.invalidate()
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.updateMetrics() }
        }
        RunLoop.main.add(timer, forMode: .common)
        metricsTimer = timer
        updateMetrics()
    }

    private func updateMetrics() {
        guard state == .running || state == .stopping else { return }
        if isForeground && state == .running {
            elapsedSeconds += 1
        }
        var raw = MikageRenPyStats()
        let hasStats = MikageRenPyGetStats(&raw)
        // Ren'Py fits the game into the window keeping its shape, so the
        // stats and floating buttons can settle in the bars beside it.
        let gameSize = hasStats ? CGSize(width: Int(raw.gameWidth), height: Int(raw.gameHeight)) : .zero
        guard performanceEnabled, hasStats else {
            overlayCoordinator?.update(
                KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds, gameSize: gameSize)
            )
            return
        }
        let cpuSample = threadCPU.sample()
        let backend = withUnsafePointer(to: &raw.renderer) { pointer in
            pointer.withMemoryRebound(to: CChar.self, capacity: 32) { String(cString: $0) }
        }
        overlayCoordinator?.update(
            KRKRPerformanceSnapshot(
                framesPerSecond: raw.framesPerSecond,
                frameTimeMilliseconds: raw.frameTimeMilliseconds,
                cpuFrameTimeMilliseconds: raw.cpuFrameTimeMilliseconds,
                maxCpuFrameTimeMilliseconds: raw.maxCpuFrameTimeMilliseconds,
                drawableWidth: raw.drawableWidth,
                drawableHeight: raw.drawableHeight,
                renderer: backend.isEmpty ? Self.rendererLabel : "\(Self.rendererLabel) · \(backend)",
                residentMemoryBytes: processResidentMemoryBytes(),
                processCPUPercent: cpuSample?.processPercent ?? -1,
                busiestThreads: Array(cpuSample?.groups.prefix(2) ?? []),
                elapsedSeconds: elapsedSeconds,
                showsLayerCompositing: false,
                gpuMemoryBytes: metalAllocatedBytes(),
                gameSize: gameSize
            )
        )
    }
}

private func mikageRenPyMenuCallback(_ context: UnsafeMutableRawPointer?) {
    guard let context else { return }
    let session = Unmanaged<NativeRenPySession>.fromOpaque(context).takeUnretainedValue()
    Task { @MainActor in session.runtimeRequestedMenu() }
}

private func mikageRenPyCompletionCallback(
    _ success: Bool,
    _ message: UnsafePointer<CChar>?,
    _ context: UnsafeMutableRawPointer?
) {
    guard let context else { return }
    let session = Unmanaged<NativeRenPySession>.fromOpaque(context).takeUnretainedValue()
    let detail = message.map { String(cString: $0) }
    Task { @MainActor in await session.runtimeFinished(success: success, message: detail) }
}
