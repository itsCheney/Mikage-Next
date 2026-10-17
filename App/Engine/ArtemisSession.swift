import Foundation
import UIKit
import ArtemisRuntime
import VNCore

enum ArtemisSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "openArtemis failed to start.") : String(localized: "openArtemis failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "openArtemis stopped.") : String(localized: "openArtemis stopped: \(message)")
        }
    }
}

/// Runs openArtemis (ArtemisRuntime) in its own landscape window. It is an
/// SDL3 main-callback engine like KiriKiri, so it runs on the shared SDL host
/// with the same overlay, lifecycle and orientation handling.
@MainActor
final class NativeArtemisSession: NSObject, KRKRSession {
    private static let rendererLabel = "openArtemis"

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

        AppDiagnostics.shared.beginGame(["engine": EngineID.artemis.rawValue,
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

        // Saves stay in the game's own savedata folder, as for KiriKiri, so
        // Storage can measure, export and keep them.
        let saveDirectory = gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true)
        try FileManager.default.createDirectory(at: saveDirectory, withIntermediateDirectories: true)

        state = .starting
        let context = Unmanaged.passUnretained(self).toOpaque()
        let gamePath = gameDirectory.path
        let savePath = saveDirectory.path
        let respectSilentMode = configuration.respectSilentMode
        let menuGesture = configuration.threeFingerMenu
        let scenePointer = Unmanaged.passUnretained(scene).toOpaque()
        AppDiagnostics.shared.event("bridge", "artemis.start.scheduledOnRunLoop")
        let started = await SDLHostMainRunLoop.perform {
            gamePath.withCString { game in
                savePath.withCString { saves in
                    var options = MikageArtemisLaunchOptions(
                        gamePath: game,
                        saveDirectory: saves,
                        windowScene: scenePointer,
                        respectSilentMode: respectSilentMode,
                        menuGestureEnabled: menuGesture
                    )
                    return MikageArtemisStart(&options, mikageArtemisMenuCallback, mikageArtemisCompletionCallback, context)
                }
            }
        }
        AppDiagnostics.shared.event("bridge", "artemis.start.returned", [
            "success": String(started), "platform": String(cString: MikageArtemisPlatform())
        ])
        guard started else {
            let message = String(cString: MikageArtemisLastError())
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw ArtemisSessionError.runtime(message)
        }
        let startupWarning = String(cString: MikageArtemisLastError())
        if !startupWarning.isEmpty {
            onWarning?(KRKRSessionError.audioSession(startupWarning))
        }

        if let nativeWindow = MikageArtemisNativeWindow() {
            engineWindow = Unmanaged<UIWindow>.fromOpaque(nativeWindow).takeUnretainedValue()
        }
        guard let engineWindow else {
            AppDiagnostics.shared.event("UIKit", "SDL.window.missing")
            // Wind the engine down; its completion finds the session failed
            // and leaves the return to the library to this error.
            MikageArtemisRequestStop()
            while MikageArtemisIsRunning() { _ = MikageArtemisStep() }
            let message = String(localized: "openArtemis didn’t create a usable window.")
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw ArtemisSessionError.runtime(message)
        }

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            performanceVisible: configuration.performance,
            performanceDetail: configuration.performanceDetail,
            renderer: Self.rendererLabel
        )
        overlay.install(
            in: engineWindow,
            floatingButtonEnabled: configuration.floatingButton,
            floatingButtonOpacity: configuration.idleOpacity,
            onExit: { [weak self] in
                self?.overlayCoordinator?.hideMenu()
                self?.requestNativeExit()
            },
            onBack: {
                let sent = MikageArtemisSendBack()
                AppDiagnostics.shared.event("session", "back.requested", ["sent": String(sent)])
            },
            onMarkFinished: { [weak self] in
                AppDiagnostics.shared.event("session", "markFinished.requested")
                self?.onMarkFinished?()
                self?.overlayCoordinator?.hideMenu()
                self?.requestNativeExit()
            },
            onScreenshot: { [weak self] in self?.snapshot() },
            onTogglePerformance: { [weak self] in self?.togglePerformanceHUD() }
        )
        overlayCoordinator = overlay

        frameDriver.start(preferredFramesPerSecond: 60) { [weak self] in self?.step() }
        startMetricsTimer()
        state = .running
        isForeground = true
        if !requestedForeground {
            _ = MikageArtemisSetForeground(false)
            isForeground = false
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// Artemis saves as it goes, so leaving the game simply ends it.
    private func requestNativeExit() {
        guard state == .running else { return }
        AppDiagnostics.shared.event("session", "nativeExit.requested")
        requestStop()
    }

    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        MikageArtemisRequestStop()
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running || state == .stopping else { return }
        guard isForeground != foreground else { return }
        let changed = MikageArtemisSetForeground(foreground)
        if !foreground || changed {
            isForeground = foreground
        }
        if !changed {
            let detail = String(cString: MikageArtemisLastError())
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

    func snapshot() -> UIImage? {
        guard let window = engineWindow, !window.bounds.isEmpty else { return nil }
        var frame = MikageArtemisCapturedFrame()
        guard MikageArtemisCaptureFrame(&frame) else { return nil }
        defer { MikageArtemisFreeCapturedFrame(&frame) }
        guard let pixels = frame.pixels, frame.width > 0, frame.height > 0 else { return nil }
        let data = Data(bytes: pixels, count: Int(frame.pitch) * Int(frame.height))
        guard let provider = CGDataProvider(data: data as CFData),
              let image = CGImage(
                width: Int(frame.width), height: Int(frame.height),
                bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(frame.pitch),
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue)
                    .union(.byteOrder32Big),
                provider: provider, decode: nil, shouldInterpolate: true,
                intent: .defaultIntent
              ) else { return nil }
        // The stage is letterboxed to its own aspect ratio, as on screen.
        let bounds = window.bounds
        let scale = min(bounds.width / CGFloat(frame.width), bounds.height / CGFloat(frame.height))
        let size = CGSize(width: CGFloat(frame.width) * scale, height: CGFloat(frame.height) * scale)
        let rect = CGRect(x: (bounds.width - size.width) / 2, y: (bounds.height - size.height) / 2,
                          width: size.width, height: size.height)
        let renderer = UIGraphicsImageRenderer(bounds: bounds)
        return renderer.image { _ in
            UIColor.black.setFill()
            UIRectFill(bounds)
            UIImage(cgImage: image).draw(in: rect)
            overlayCoordinator?.drawScreenshotOverlay(in: window)
        }
    }

    fileprivate func runtimeRequestedMenu() {
        guard state == .running else { return }
        showMenuOverlay()
    }

    fileprivate func runtimeFinished(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "artemis.completion", ["success": String(success), "message": message ?? ""])
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
            completion?(.failure(ArtemisSessionError.stopped(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func step() {
        guard state == .running || state == .stopping else { return }
        _ = MikageArtemisStep()
    }

    private func togglePerformanceHUD() {
        performanceEnabled.toggle()
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
        let diagnosticsEnabled = AppDiagnostics.shared.isEnabled
        var raw = MikageArtemisStats()
        // Reading the stats resets the engine's stage profile, so read them
        // once per tick for both the HUD and the diagnostics log.
        guard performanceEnabled || diagnosticsEnabled, MikageArtemisGetStats(&raw) else {
            overlayCoordinator?.update(
                KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds)
            )
            return
        }
        let cpuSample = threadCPU.sample()
        let backend = withUnsafePointer(to: &raw.renderer) { pointer in
            pointer.withMemoryRebound(to: CChar.self, capacity: 32) { String(cString: $0) }
        }
        if diagnosticsEnabled {
            let stageProfile = withUnsafePointer(to: &raw.stageProfile) { pointer in
                pointer.withMemoryRebound(to: CChar.self, capacity: 640) { String(cString: $0) }
            }
            if let cpuSample {
                AppDiagnostics.shared.event("session", "threadCPU", [
                    "processCPUPercent": String(format: "%.1f", cpuSample.processPercent),
                    "threadCount": String(cpuSample.threadCount),
                    "threads": cpuSample.summary(limit: 16),
                    "foreground": String(isForeground),
                    "thermalState": String(ProcessInfo.processInfo.thermalState.rawValue)
                ])
            }
            AppDiagnostics.shared.event("session", "heartbeat", [
                "foreground": String(isForeground),
                "fps": String(raw.framesPerSecond),
                "frameTimeMS": String(raw.frameTimeMilliseconds),
                "cpuFrameTimeMS": String(raw.cpuFrameTimeMilliseconds),
                "maxCpuFrameTimeMS": String(raw.maxCpuFrameTimeMilliseconds),
                "drawable": "\(raw.drawableWidth)x\(raw.drawableHeight)",
                "actualRenderer": backend,
                "drawCallsPerFrame": String(raw.drawCallsPerFrame),
                "stageProfile": stageProfile,
                "residentBytes": String(processResidentMemoryBytes()),
                "thermalState": String(ProcessInfo.processInfo.thermalState.rawValue)
            ])
        }
        guard performanceEnabled else {
            overlayCoordinator?.update(
                KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds)
            )
            return
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
                busiestThreads: cpuSample?.summary(limit: 2) ?? "",
                elapsedSeconds: elapsedSeconds,
                showsLayerCompositing: false,
                drawCallsPerFrame: raw.drawCallsPerFrame,
                gpuMemoryBytes: metalAllocatedBytes()
            )
        )
    }
}

private func mikageArtemisMenuCallback(_ context: UnsafeMutableRawPointer?) {
    guard let context else { return }
    let session = Unmanaged<NativeArtemisSession>.fromOpaque(context).takeUnretainedValue()
    Task { @MainActor in session.runtimeRequestedMenu() }
}

private func mikageArtemisCompletionCallback(
    _ success: Bool,
    _ message: UnsafePointer<CChar>?,
    _ context: UnsafeMutableRawPointer?
) {
    guard let context else { return }
    let session = Unmanaged<NativeArtemisSession>.fromOpaque(context).takeUnretainedValue()
    let detail = message.map { String(cString: $0) }
    Task { @MainActor in await session.runtimeFinished(success: success, message: detail) }
}
