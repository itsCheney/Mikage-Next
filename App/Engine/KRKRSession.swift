import Foundation
import UIKit
import Darwin
import KRKRRuntime
import VNCore

@MainActor
protocol KRKRSession: AnyObject {
    var state: KRKRSessionState { get }
    var onFinished: ((Result<Void, Error>) -> Void)? { get set }
    var onWarning: ((Error) -> Void)? { get set }
    var onReturningToLibrary: (() -> Void)? { get set }
    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) async throws
    func requestStop()
    func setForeground(_ foreground: Bool)
    func showMenuOverlay()
    func hideMenuOverlay()
    func snapshot() -> UIImage?
}

struct KRKRLaunchConfiguration {
    let gameDirectory: URL
    let entryPoint: URL
    let targetKind: LaunchTargetKind
    let renderer: String
    let floatingButton: Bool
    let idleOpacity: Double
    let threeFingerMenu: Bool
    let performance: Bool
    let gameTitle: String
}

enum KRKRSessionState: Equatable {
    case idle, preparingOrientation, starting, running, stopping, restoringOrientation, failed(String)
}

enum KRKRSessionError: LocalizedError {
    case alreadyRunning
    case missingHostWindow
    case invalidGameDirectory
    case orientationUnavailable
    case audioSession(String)
    case runtime(String)

    var errorDescription: String? {
        switch self {
        case .alreadyRunning:
            return "已有 KRKR 游戏正在运行。"
        case .missingHostWindow:
            return "无法取得当前 iOS 窗口，KRKR 未启动。"
        case .invalidGameDirectory:
            return "游戏目录或启动入口已经不存在。"
        case .orientationUnavailable:
            return "无法切换到横屏。请确认方向锁定允许横屏，然后旋转设备后重试。"
        case .audioSession(let message):
            return "音频会话切换失败，游戏将保持静音：\(message)"
        case .runtime(let message):
            return message.isEmpty ? "KRKR 启动失败。" : "KRKR 启动失败：\(message)"
        }
    }
}

@MainActor
final class NativeKRKRSession: NSObject, KRKRSession {
    @MainActor
    private final class DisplayLinkTarget: NSObject {
        weak var owner: NativeKRKRSession?
        init(owner: NativeKRKRSession) { self.owner = owner }
        @objc func tick() { owner?.step() }
    }

    private(set) var state: KRKRSessionState = .idle {
        didSet {
            AppDiagnostics.shared.event("session", "state.changed", ["from": String(describing: oldValue), "to": String(describing: state)])
        }
    }
    var onFinished: ((Result<Void, Error>) -> Void)?
    var onWarning: ((Error) -> Void)?
    var onReturningToLibrary: (() -> Void)?

    private weak var hostWindow: UIWindow?
    private weak var engineWindow: UIWindow?
    private weak var windowScene: UIWindowScene?
    private var previousOrientation: UIInterfaceOrientation = .unknown
    private var displayLink: CADisplayLink?
    private var displayLinkTarget: DisplayLinkTarget?
    private var metricsTimer: Timer?
    private var overlayCoordinator: KRKROverlayCoordinator?
    private var observers: [NSObjectProtocol] = []
    private var isForeground = true
    private var requestedForeground = true
    private var elapsedSeconds = 0
    private var rendererLabel = "Metal"
    private var performanceEnabled = false
    private var displayTicks = 0
    private var lastStepResult = "none"
    private var isStepping = false

    override init() {
        super.init()
        let center = NotificationCenter.default
        observers.append(
            center.addObserver(
                forName: UIApplication.willResignActiveNotification,
                object: nil,
                queue: .main
            ) { [weak self] _ in
                Task { @MainActor in self?.setForeground(false) }
            }
        )
        observers.append(
            center.addObserver(
                forName: UIApplication.didBecomeActiveNotification,
                object: nil,
                queue: .main
            ) { [weak self] _ in
                Task { @MainActor in self?.setForeground(true) }
            }
        )
    }

    deinit {
        for observer in observers {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    func start(
        configuration: KRKRLaunchConfiguration,
        in viewController: UIViewController
    ) async throws {
        guard state == .idle || isFailed else { throw KRKRSessionError.alreadyRunning }
        requestedForeground = UIApplication.shared.applicationState == .active
        guard FileManager.default.fileExists(atPath: configuration.gameDirectory.path),
              FileManager.default.fileExists(atPath: configuration.entryPoint.path) else {
            throw KRKRSessionError.invalidGameDirectory
        }
        let rawTarget = configuration.entryPoint.standardizedFileURL
        let root = configuration.gameDirectory.standardizedFileURL.resolvingSymlinksInPath()
        let resolvedTarget = rawTarget.resolvingSymlinksInPath()
        guard resolvedTarget == root || resolvedTarget.path.hasPrefix(root.path + "/"),
              (try? rawTarget.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) != true else {
            throw KRKRSessionError.invalidGameDirectory
        }
        guard let window = viewController.viewIfLoaded?.window,
              let scene = window.windowScene else {
            throw KRKRSessionError.missingHostWindow
        }

        AppDiagnostics.shared.beginGame(["folder": configuration.gameDirectory.lastPathComponent,
                                         "entryPoint": configuration.entryPoint.lastPathComponent,
                                         "targetKind": String(describing: configuration.targetKind),
                                         "requestedRenderer": configuration.renderer,
                                         "performanceHUD": String(configuration.performance),
                                         "floatingButton": String(configuration.floatingButton),
                                         "threeFingerMenu": String(configuration.threeFingerMenu)])
        displayTicks = 0
        lastStepResult = "none"
        AppDiagnostics.shared.windows("launch.hostWindow")
        state = .preparingOrientation
        hostWindow = window
        windowScene = scene
        previousOrientation = scene.interfaceOrientation
        do {
            try await prepareLandscape(scene: scene, window: window, controller: viewController)
        } catch {
            state = .failed(error.localizedDescription)
            await restorePreviousOrientation()
            throw error
        }

        try FileManager.default.createDirectory(
            at: configuration.gameDirectory.appendingPathComponent("savedata", isDirectory: true),
            withIntermediateDirectories: true
        )

        state = .starting
        let context = Unmanaged.passUnretained(self).toOpaque()
        let scenePointer = Unmanaged.passUnretained(scene).toOpaque()
        var runtimePath = resolvedTarget.path
        if configuration.targetKind == .directory && !runtimePath.hasSuffix("/") {
            runtimePath.append("/")
        }
        AppDiagnostics.shared.event("bridge", "start.scheduledOnRunLoop")
        let started = await KRKRMainRunLoop.perform {
            AppDiagnostics.shared.event("bridge", "start.enteredFromRunLoop")
            return runtimePath.withCString { gamePath in
                configuration.renderer.withCString { renderer in
                    MikageKRKRStart(
                        gamePath,
                        renderer,
                        scenePointer,
                        configuration.threeFingerMenu,
                        mikageKRKRMenuCallback,
                        mikageKRKRCompletionCallback,
                        context
                    )
                }
            }
        }
        AppDiagnostics.shared.event("bridge", "start.returned", ["success": String(started)])
        guard started else {
            let message = String(cString: MikageKRKRLastError())
            AppDiagnostics.shared.event("bridge", "start.failed", ["message": message])
            state = .failed(message)
            await restorePreviousOrientation()
            throw KRKRSessionError.runtime(message)
        }
        let startupWarning = String(cString: MikageKRKRLastError())
        if !startupWarning.isEmpty {
            onWarning?(KRKRSessionError.audioSession(startupWarning))
        }

        if let nativeWindow = MikageKRKRNativeWindow() {
            engineWindow = Unmanaged<UIWindow>.fromOpaque(nativeWindow).takeUnretainedValue()
        }
        guard let engineWindow else {
            AppDiagnostics.shared.event("UIKit", "SDL.window.missing")
            MikageKRKRRequestStop()
            state = .failed("KRKR 没有创建可用的 SDL window。")
            await restorePreviousOrientation()
            throw KRKRSessionError.runtime("KRKR 没有创建可用的 SDL window。")
        }

        switch configuration.renderer {
        case "opengl": rendererLabel = "OpenGL ES"
        case "software-metal": rendererLabel = "软件合成 · Metal"
        default: rendererLabel = "Metal"
        }
        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            performanceVisible: configuration.performance,
            renderer: rendererLabel
        )
        overlay.install(
            in: engineWindow,
            floatingButtonEnabled: configuration.floatingButton,
            floatingButtonOpacity: configuration.idleOpacity,
            onExit: { [weak self] in
                self?.overlayCoordinator?.hideMenu()
                self?.requestStop()
            },
            onScreenshot: { [weak self] in self?.snapshot() }
        )
        overlayCoordinator = overlay
        AppDiagnostics.shared.windows("overlay.installed")

        let target = DisplayLinkTarget(owner: self)
        let link = CADisplayLink(target: target, selector: #selector(DisplayLinkTarget.tick))
        link.add(to: .main, forMode: .common)
        displayLinkTarget = target
        displayLink = link
        startMetricsTimer()
        state = .running
        isForeground = true
        if !requestedForeground {
            _ = MikageKRKRSetForeground(false)
            isForeground = false
        }

        engineWindow.makeKeyAndVisible()
        hostWindow?.isHidden = true
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        _ = MikageKRKRSetForeground(false)
        MikageKRKRRequestStop()
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running || state == .stopping else { return }
        guard isForeground != foreground else { return }
        let changed = MikageKRKRSetForeground(foreground)
        AppDiagnostics.shared.event("audio", "foreground.result", ["active": String(foreground), "success": String(changed), "error": String(cString: MikageKRKRLastError())])
        if !foreground || changed {
            isForeground = foreground
        }
        if !changed {
            let detail = String(cString: MikageKRKRLastError())
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
        var frame = MikageKRKRCapturedFrame()
        if MikageKRKRCaptureFrame(&frame) {
            defer { MikageKRKRFreeCapturedFrame(&frame) }
            guard let pixels = frame.pixels else { return nil }
            let data = Data(bytes: pixels, count: Int(frame.pitch) * Int(frame.height))
            guard let provider = CGDataProvider(data: data as CFData),
                  let image = CGImage(
                    width: Int(frame.width), height: Int(frame.height),
                    bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(frame.pitch),
                    space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue)
                        .union(.byteOrder32Big),
                    provider: provider, decode: nil, shouldInterpolate: false,
                    intent: .defaultIntent
                  ) else { return nil }
            let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
            return renderer.image { _ in
                UIColor.black.setFill()
                UIRectFill(window.bounds)
                UIImage(cgImage: image).draw(in: window.bounds)
                overlayCoordinator?.drawScreenshotOverlay(in: window)
            }
        }
        var stats = MikageKRKRStats()
        if MikageKRKRGetStats(&stats), rendererName(from: &stats) == "Metal" {
            AppDiagnostics.shared.event("session", "screenshot.native.failed")
            return nil
        }
        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
        return renderer.image { _ in
            window.drawHierarchy(in: window.bounds, afterScreenUpdates: true)
        }
    }

    fileprivate func runtimeRequestedMenu() {
        guard state == .running else { return }
        showMenuOverlay()
    }

    fileprivate func runtimeFinished(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "completion", ["success": String(success), "message": message ?? ""])
        displayLink?.invalidate()
        displayLink = nil
        displayLinkTarget = nil
        metricsTimer?.invalidate()
        metricsTimer = nil
        overlayCoordinator?.remove()
        overlayCoordinator = nil
        engineWindow = nil

        state = .restoringOrientation
        onReturningToLibrary?()
        hostWindow?.isHidden = false
        hostWindow?.makeKeyAndVisible()
        await restorePreviousOrientation()
        AppDiagnostics.shared.windows("return.windowsRestored")
        AppDiagnostics.shared.endGame()

        let completion = onFinished
        onFinished = nil
        onWarning = nil
        onReturningToLibrary = nil
        if success {
            state = .idle
            completion?(.success(()))
        } else {
            let detail = message ?? "KRKR runtime 已停止。"
            state = .failed(detail)
            completion?(.failure(KRKRSessionError.runtime(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func prepareLandscape(
        scene: UIWindowScene,
        window: UIWindow,
        controller: UIViewController
    ) async throws {
        controller.setNeedsUpdateOfSupportedInterfaceOrientations()
        window.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
        let preferences = UIWindowScene.GeometryPreferences.iOS(
            interfaceOrientations: [.landscapeLeft, .landscapeRight]
        )
        AppDiagnostics.shared.event("UIKit", "geometry.landscape.request")
        scene.requestGeometryUpdate(preferences) { error in
            AppDiagnostics.shared.event("UIKit", "geometry.landscape.rejected", ["error": error.localizedDescription])
        }
        UIViewController.attemptRotationToDeviceOrientation()

        for _ in 0..<80 {
            let sceneLandscape = scene.interfaceOrientation.isLandscape
            let sceneBounds = scene.coordinateSpace.bounds
            let windowBounds = window.bounds
            if sceneLandscape,
               sceneBounds.width > sceneBounds.height,
               windowBounds.width > windowBounds.height {
                AppDiagnostics.shared.windows("geometry.landscape.ready")
                await Task.yield()
                return
            }
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        AppDiagnostics.shared.windows("geometry.landscape.timeout")
        throw KRKRSessionError.orientationUnavailable
    }

    private func restorePreviousOrientation() async {
        guard let scene = windowScene else { return }
        let targetOrientation = previousOrientation
        let mask: UIInterfaceOrientationMask
        switch targetOrientation {
        case .portrait:
            mask = .portrait
        case .portraitUpsideDown:
            mask = .portraitUpsideDown
        case .landscapeLeft:
            mask = .landscapeLeft
        case .landscapeRight:
            mask = .landscapeRight
        default:
            mask = UIDevice.current.userInterfaceIdiom == .pad ? .all : .portrait
        }
        hostWindow?.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
        let preferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: mask)
        AppDiagnostics.shared.event("UIKit", "geometry.restore.request", ["orientation": String(targetOrientation.rawValue)])
        scene.requestGeometryUpdate(preferences) { error in
            AppDiagnostics.shared.event("UIKit", "geometry.restore.rejected", ["error": error.localizedDescription])
        }
        UIViewController.attemptRotationToDeviceOrientation()

        let expectsPortrait = targetOrientation.isPortrait ||
            (targetOrientation == .unknown && UIDevice.current.userInterfaceIdiom != .pad)
        let expectsLandscape = targetOrientation.isLandscape
        var stableLayoutSamples = 0
        for _ in 0..<60 {
            let sceneBounds = scene.coordinateSpace.bounds
            let windowBounds = hostWindow?.bounds ?? .zero
            let layoutMatches: Bool
            if expectsPortrait {
                layoutMatches = scene.interfaceOrientation.isPortrait &&
                    sceneBounds.height > sceneBounds.width &&
                    windowBounds.height > windowBounds.width
            } else if expectsLandscape {
                layoutMatches = scene.interfaceOrientation.isLandscape &&
                    sceneBounds.width > sceneBounds.height &&
                    windowBounds.width > windowBounds.height
            } else {
                layoutMatches = true
            }

            if layoutMatches {
                stableLayoutSamples += 1
                if stableLayoutSamples >= 3 { break }
            } else {
                stableLayoutSamples = 0
            }
            try? await Task.sleep(nanoseconds: 50_000_000)
        }
        hostWindow?.layoutIfNeeded()
        AppDiagnostics.shared.event("UIKit", "geometry.restore.completed", ["stableSamples": String(stableLayoutSamples)])
        try? await Task.sleep(nanoseconds: 100_000_000)
        windowScene = nil
        previousOrientation = .unknown
    }

    private func step() {
        guard state == .running || state == .stopping else { return }
        // A synchronous SDL dialog pumps the run loop, which can fire another
        // display-link callback before the current script invocation returns.
        guard !isStepping else { return }
        isStepping = true
        defer { isStepping = false }
        displayTicks += 1
        let result = MikageKRKRStep()
        lastStepResult = String(describing: result)
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
        if AppDiagnostics.shared.isEnabled {
            var diagnosticStats = MikageKRKRStats()
            if MikageKRKRGetStats(&diagnosticStats) {
                AppDiagnostics.shared.event("session", "heartbeat", [
                    "displayTicks": String(displayTicks), "stepResult": lastStepResult,
                    "foreground": String(isForeground), "fps": String(diagnosticStats.framesPerSecond),
                    "frameTimeMS": String(diagnosticStats.frameTimeMilliseconds),
                    "drawable": "\(diagnosticStats.drawableWidth)x\(diagnosticStats.drawableHeight)",
                    "actualRenderer": rendererName(from: &diagnosticStats),
                    "residentBytes": String(residentMemoryBytes())
                ])
                AppDiagnostics.shared.windows("heartbeat.windows")
            }
        }
        if isForeground && state == .running {
            elapsedSeconds += 1
        }
        guard performanceEnabled else {
            overlayCoordinator?.update(
                KRKRPerformanceSnapshot(
                    renderer: rendererLabel,
                    elapsedSeconds: elapsedSeconds
                )
            )
            return
        }
        var raw = MikageKRKRStats()
        guard MikageKRKRGetStats(&raw) else { return }
        overlayCoordinator?.update(
            KRKRPerformanceSnapshot(
                framesPerSecond: raw.framesPerSecond,
                frameTimeMilliseconds: raw.frameTimeMilliseconds,
                drawableWidth: raw.drawableWidth,
                drawableHeight: raw.drawableHeight,
                renderer: rendererName(from: &raw),
                residentMemoryBytes: residentMemoryBytes(),
                elapsedSeconds: elapsedSeconds
            )
        )
    }

    private func rendererName(from stats: inout MikageKRKRStats) -> String {
        let rawName = withUnsafePointer(to: &stats.renderer) { pointer in
            pointer.withMemoryRebound(to: CChar.self, capacity: 32) {
                String(cString: $0)
            }
        }
        switch rawName.lowercased() {
        case "metal":
            return "Metal"
        case "software/metal":
            return "软件合成 · Metal"
        case "software/opengles2":
            return "软件合成 · OpenGL ES"
        case "opengl":
            return "OpenGL ES"
        case "vulkan":
            return "Vulkan"
        case "":
            return rendererLabel
        default:
            return rawName
        }
    }

    private func residentMemoryBytes() -> UInt64 {
        var info = mach_task_basic_info()
        var count = mach_msg_type_number_t(
            MemoryLayout<mach_task_basic_info>.size / MemoryLayout<natural_t>.size
        )
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(
                    mach_task_self_,
                    task_flavor_t(MACH_TASK_BASIC_INFO),
                    $0,
                    &count
                )
            }
        }
        return result == KERN_SUCCESS ? UInt64(info.resident_size) : 0
    }
}

private func mikageKRKRMenuCallback(_ context: UnsafeMutableRawPointer?) {
    guard let context else { return }
    let session = Unmanaged<NativeKRKRSession>.fromOpaque(context).takeUnretainedValue()
    Task { @MainActor in session.runtimeRequestedMenu() }
}

private func mikageKRKRCompletionCallback(
    _ success: Bool,
    _ message: UnsafePointer<CChar>?,
    _ context: UnsafeMutableRawPointer?
) {
    guard let context else { return }
    let session = Unmanaged<NativeKRKRSession>.fromOpaque(context).takeUnretainedValue()
    let detail = message.map { String(cString: $0) }
    Task { @MainActor in await session.runtimeFinished(success: success, message: detail) }
}
