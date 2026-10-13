import Foundation
import UIKit
import ONSRuntime
import VNCore

enum ONSSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "ONScripter failed to start.") : String(localized: "ONScripter failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "ONScripter stopped.") : String(localized: "ONScripter stopped: \(message)")
        }
    }
}

/// Runs ONScripter Yuri (ONSRuntime) in its own landscape window, with the
/// same overlay, lifecycle and orientation handling as KiriKiri.
@MainActor
final class NativeONSSession: NSObject, KRKRSession {
    @MainActor
    private final class DisplayLinkTarget: NSObject {
        weak var owner: NativeONSSession?
        init(owner: NativeONSSession) { self.owner = owner }
        @objc func tick() { owner?.step() }
    }

    private static let rendererLabel = "ONScripter Yuri"

    private(set) var state: KRKRSessionState = .idle {
        didSet {
            AppDiagnostics.shared.event("session", "state.changed", ["from": String(describing: oldValue), "to": String(describing: state)])
        }
    }
    var onFinished: ((Result<Void, Error>) -> Void)?
    var onWarning: ((Error) -> Void)?
    var onReturningToLibrary: (() -> Void)?
    var onMarkFinished: (() -> Void)?

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
    private var performanceEnabled = false

    override init() {
        super.init()
        let center = NotificationCenter.default
        observers.append(
            center.addObserver(forName: UIApplication.willResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.setForeground(false) }
            }
        )
        observers.append(
            center.addObserver(forName: UIApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.setForeground(true) }
            }
        )
    }

    deinit {
        for observer in observers {
            NotificationCenter.default.removeObserver(observer)
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

        AppDiagnostics.shared.beginGame(["engine": EngineID.onscripter.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
                                         "performanceHUD": String(configuration.performance),
                                         "floatingButton": String(configuration.floatingButton),
                                         "threeFingerMenu": String(configuration.threeFingerMenu),
                                         "respectSilentMode": String(configuration.respectSilentMode)])
        AppDiagnostics.shared.windows("launch.hostWindow")
        state = .preparingOrientation
        hostWindow = window
        windowScene = scene
        previousOrientation = scene.interfaceOrientation
        do {
            try await EngineOrientation.prepareLandscape(scene: scene, window: window, controller: viewController)
        } catch {
            state = .failed(error.localizedDescription)
            await restorePreviousOrientation()
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
        AppDiagnostics.shared.event("bridge", "ons.start.scheduledOnRunLoop")
        let started = await KRKRMainRunLoop.perform {
            gamePath.withCString { game in
                savePath.withCString { saves in
                    var options = MikageONSLaunchOptions(
                        gamePath: game,
                        saveDirectory: saves,
                        fontPath: nil,
                        encoding: nil,
                        respectSilentMode: respectSilentMode,
                        menuGestureEnabled: menuGesture
                    )
                    return MikageONSStart(&options, mikageONSMenuCallback, mikageONSCompletionCallback, context)
                }
            }
        }
        AppDiagnostics.shared.event("bridge", "ons.start.returned", [
            "success": String(started), "encoding": String(cString: MikageONSEncoding())
        ])
        guard started else {
            let message = String(cString: MikageONSLastError())
            state = .failed(message)
            await restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw ONSSessionError.runtime(message)
        }

        if let nativeWindow = MikageONSNativeWindow() {
            engineWindow = Unmanaged<UIWindow>.fromOpaque(nativeWindow).takeUnretainedValue()
        }
        guard let engineWindow else {
            AppDiagnostics.shared.event("UIKit", "SDL.window.missing")
            // Wind the engine down; its completion finds the session failed
            // and leaves the return to the library to this error.
            MikageONSRequestStop()
            while MikageONSIsRunning() { MikageONSStep() }
            let message = String(localized: "ONScripter didn’t create a usable window.")
            state = .failed(message)
            await restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw ONSSessionError.runtime(message)
        }

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            performanceVisible: configuration.performance,
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

        let target = DisplayLinkTarget(owner: self)
        let link = CADisplayLink(target: target, selector: #selector(DisplayLinkTarget.tick))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLinkTarget = target
        displayLink = link
        startMetricsTimer()
        state = .running
        isForeground = true
        if !requestedForeground {
            _ = MikageONSSetForeground(false)
            isForeground = false
        }

        engineWindow.makeKeyAndVisible()
        hostWindow?.isHidden = true
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// ONScripter quits the way closing its window would: it saves its
    /// settings and ends, and completion returns to the library.
    private func requestNativeExit() {
        guard state == .running else { return }
        AppDiagnostics.shared.event("session", "nativeExit.requested")
        if !MikageONSRequestExit() {
            AppDiagnostics.shared.event("session", "nativeExit.unavailable")
        }
    }

    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        MikageONSRequestStop()
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running || state == .stopping else { return }
        guard isForeground != foreground else { return }
        let changed = MikageONSSetForeground(foreground)
        if !foreground || changed {
            isForeground = foreground
        }
        if !changed {
            let detail = String(cString: MikageONSLastError())
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
        var frame = MikageONSCapturedFrame()
        guard MikageONSCaptureFrame(&frame) else { return nil }
        defer { MikageONSFreeCapturedFrame(&frame) }
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
        // The game is letterboxed to its own aspect ratio, as on screen.
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
        AppDiagnostics.shared.event("bridge", "ons.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
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
        onMarkFinished = nil
        if success {
            state = .idle
            completion?(.success(()))
        } else {
            let detail = message ?? ""
            state = .failed(detail)
            completion?(.failure(ONSSessionError.stopped(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func restorePreviousOrientation() async {
        guard let scene = windowScene else { return }
        await EngineOrientation.restore(scene: scene, to: previousOrientation, hostWindow: hostWindow)
        windowScene = nil
        previousOrientation = .unknown
    }

    private func step() {
        guard state == .running || state == .stopping else { return }
        MikageONSStep()
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
        overlayCoordinator?.update(
            KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds)
        )
    }
}

private func mikageONSMenuCallback(_ context: UnsafeMutableRawPointer?) {
    guard let context else { return }
    let session = Unmanaged<NativeONSSession>.fromOpaque(context).takeUnretainedValue()
    Task { @MainActor in session.runtimeRequestedMenu() }
}

private func mikageONSCompletionCallback(
    _ success: Bool,
    _ message: UnsafePointer<CChar>?,
    _ context: UnsafeMutableRawPointer?
) {
    guard let context else { return }
    let session = Unmanaged<NativeONSSession>.fromOpaque(context).takeUnretainedValue()
    let detail = message.map { String(cString: $0) }
    Task { @MainActor in await session.runtimeFinished(success: success, message: detail) }
}
