import Foundation
import UIKit
import KRKRRuntime
import VNCore

@MainActor
protocol KRKRSession: AnyObject {
    var state: KRKRSessionState { get }
    var onMenuRequested: (() -> Void)? { get set }
    var onFinished: ((Result<Void, Error>) -> Void)? { get set }
    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) throws
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
}

enum KRKRSessionState: Equatable {
    case idle, starting, running, stopping, failed(String)
}

enum KRKRSessionError: LocalizedError {
    case alreadyRunning
    case missingHostWindow
    case invalidGameDirectory
    case runtime(String)

    var errorDescription: String? {
        switch self {
        case .alreadyRunning:
            return "已有 KRKR 游戏正在运行。"
        case .missingHostWindow:
            return "无法取得当前 iOS 窗口，KRKR 未启动。"
        case .invalidGameDirectory:
            return "游戏目录或启动入口已经不存在。"
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

    private(set) var state: KRKRSessionState = .idle
    var onMenuRequested: (() -> Void)?
    var onFinished: ((Result<Void, Error>) -> Void)?

    private weak var hostWindow: UIWindow?
    private weak var engineWindow: UIWindow?
    private var displayLink: CADisplayLink?
    private var displayLinkTarget: DisplayLinkTarget?
    private var floatingButton: UIButton?

    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) throws {
        guard state == .idle || isFailed else { throw KRKRSessionError.alreadyRunning }
        guard FileManager.default.fileExists(atPath: configuration.gameDirectory.path),
              FileManager.default.fileExists(atPath: configuration.entryPoint.path) else {
            throw KRKRSessionError.invalidGameDirectory
        }
        let rawTarget = configuration.entryPoint.standardizedFileURL
        let root = configuration.gameDirectory.standardizedFileURL.resolvingSymlinksInPath()
        let target = rawTarget.resolvingSymlinksInPath()
        guard target == root || target.path.hasPrefix(root.path + "/"),
              (try? rawTarget.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) != true else {
            throw KRKRSessionError.invalidGameDirectory
        }
        guard let window = viewController.viewIfLoaded?.window,
              let windowScene = window.windowScene else {
            throw KRKRSessionError.missingHostWindow
        }

        try FileManager.default.createDirectory(
            at: configuration.gameDirectory.appendingPathComponent("savedata", isDirectory: true),
            withIntermediateDirectories: true
        )

        state = .starting
        hostWindow = window
        let context = Unmanaged.passUnretained(self).toOpaque()
        let scenePointer = Unmanaged.passUnretained(windowScene).toOpaque()
        var runtimePath = target.path
        if configuration.targetKind == .directory && !runtimePath.hasSuffix("/") {
            runtimePath.append("/")
        }
        let started = runtimePath.withCString { gamePath in
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
        guard started else {
            let message = String(cString: MikageKRKRLastError())
            state = .failed(message)
            throw KRKRSessionError.runtime(message)
        }

        if let nativeWindow = MikageKRKRNativeWindow() {
            engineWindow = Unmanaged<UIWindow>.fromOpaque(nativeWindow).takeUnretainedValue()
        }
        guard let engineWindow else {
            MikageKRKRRequestStop()
            state = .failed("KRKR 没有创建可用的 SDL window。")
            throw KRKRSessionError.runtime("KRKR 没有创建可用的 SDL window。")
        }

        installFloatingButton(
            in: engineWindow,
            pawStyle: configuration.floatingButton,
            opacity: configuration.idleOpacity
        )
        let target = DisplayLinkTarget(owner: self)
        let link = CADisplayLink(target: target, selector: #selector(DisplayLinkTarget.tick))
        link.add(to: .main, forMode: .common)
        displayLinkTarget = target
        displayLink = link
        state = .running

        engineWindow.makeKeyAndVisible()
        hostWindow?.isHidden = true
    }

    func requestStop() {
        guard state == .running || state == .starting else { return }
        state = .stopping
        MikageKRKRRequestStop()
    }

    func setForeground(_ foreground: Bool) {
        guard state == .running || state == .stopping else { return }
        MikageKRKRSetForeground(foreground)
    }

    func showMenuOverlay() {
        guard state == .running || state == .stopping else { return }
        hostWindow?.isHidden = false
        hostWindow?.makeKeyAndVisible()
    }

    func hideMenuOverlay() {
        guard state == .running else { return }
        hostWindow?.isHidden = true
        engineWindow?.makeKeyAndVisible()
    }

    func snapshot() -> UIImage? {
        guard let window = engineWindow, !window.bounds.isEmpty else { return nil }
        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
        return renderer.image { context in
            window.layer.render(in: context.cgContext)
        }
    }

    fileprivate func runtimeRequestedMenu() {
        guard state == .running else { return }
        showMenuOverlay()
        onMenuRequested?()
    }

    fileprivate func runtimeFinished(success: Bool, message: String?) {
        displayLink?.invalidate()
        displayLink = nil
        displayLinkTarget = nil
        floatingButton?.removeFromSuperview()
        floatingButton = nil
        engineWindow = nil

        hostWindow?.isHidden = false
        hostWindow?.makeKeyAndVisible()

        if success {
            state = .idle
            onFinished?(.success(()))
        } else {
            let detail = message ?? "KRKR runtime 已停止。"
            state = .failed(detail)
            onFinished?(.failure(KRKRSessionError.runtime(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func step() {
        guard state == .running || state == .stopping else { return }
        _ = MikageKRKRStep()
    }

    private func installFloatingButton(in window: UIWindow, pawStyle: Bool, opacity: Double) {
        guard let rootView = window.rootViewController?.view else { return }
        let configuration: UIButton.Configuration
        if #available(iOS 26.0, *) {
            configuration = .glass()
        } else {
            configuration = .filled()
        }
        let button = UIButton(configuration: configuration)
        button.setImage(UIImage(systemName: pawStyle ? "pawprint.fill" : "line.3.horizontal"), for: .normal)
        button.accessibilityLabel = "唤出游戏菜单"
        button.alpha = pawStyle ? max(0.1, min(opacity, 1)) : 0.8
        button.frame = CGRect(x: max(rootView.bounds.width - 72, 12), y: 28, width: 52, height: 52)
        button.autoresizingMask = [.flexibleLeftMargin, .flexibleBottomMargin]
        button.addAction(UIAction { [weak self] _ in self?.runtimeRequestedMenu() }, for: .touchUpInside)
        button.addGestureRecognizer(UIPanGestureRecognizer(target: self, action: #selector(dragFloatingButton(_:))))
        rootView.addSubview(button)
        rootView.bringSubviewToFront(button)
        floatingButton = button
    }

    @objc private func dragFloatingButton(_ recognizer: UIPanGestureRecognizer) {
        guard let button = recognizer.view, let container = button.superview else { return }
        let translation = recognizer.translation(in: container)
        var center = CGPoint(x: button.center.x + translation.x, y: button.center.y + translation.y)
        let safe = container.safeAreaLayoutGuide.layoutFrame.insetBy(dx: -4, dy: -4)
        center.x = min(max(center.x, safe.minX + button.bounds.width / 2), safe.maxX - button.bounds.width / 2)
        center.y = min(max(center.y, safe.minY + button.bounds.height / 2), safe.maxY - button.bounds.height / 2)
        button.center = center
        recognizer.setTranslation(.zero, in: container)
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
    Task { @MainActor in session.runtimeFinished(success: success, message: detail) }
}
