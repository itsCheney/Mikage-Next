import SwiftUI
import UIKit

struct KRKRPerformanceSnapshot {
    var framesPerSecond = 0.0
    var frameTimeMilliseconds = 0.0
    var cpuFrameTimeMilliseconds = 0.0
    var maxCpuFrameTimeMilliseconds = 0.0
    var gpuSubmissionTimeMilliseconds = -1.0
    var presentationWaitTimeMilliseconds = -1.0
    var drawableWidth: Int32 = 0
    var drawableHeight: Int32 = 0
    var renderer = "Metal"
    var gpuLayerComposition = false
    var gpuLayerOperations: UInt64 = 0
    var layerCPUFallbacks: UInt64 = 0
    var layerUploadedBytes: UInt64 = 0
    var layerReadbackBytes: UInt64 = 0
    var residentMemoryBytes: UInt64 = 0
    var elapsedSeconds = 0
}

@MainActor
private final class KRKROverlayModel: ObservableObject {
    @Published var menuVisible = false
    @Published var performance = KRKRPerformanceSnapshot()
    @Published var sharedImage: UIImage?

    let gameTitle: String
    let performanceVisible: Bool
    var onCloseMenu: (() -> Void)?
    var onExit: (() -> Void)?
    var onScreenshot: (() -> UIImage?)?

    init(gameTitle: String, performanceVisible: Bool, renderer: String) {
        self.gameTitle = gameTitle
        self.performanceVisible = performanceVisible
        performance.renderer = renderer
    }
}

@MainActor
final class KRKROverlayCoordinator: NSObject {
    private let model: KRKROverlayModel
    private var hostingController: UIHostingController<KRKROverlayView>?
    private weak var containerView: UIView?
    private var floatingButton: UIButton?

    init(gameTitle: String, performanceVisible: Bool, renderer: String) {
        model = KRKROverlayModel(
            gameTitle: gameTitle,
            performanceVisible: performanceVisible,
            renderer: renderer
        )
    }

    func install(
        in window: UIWindow,
        floatingButtonEnabled: Bool,
        floatingButtonOpacity: Double,
        onExit: @escaping () -> Void,
        onScreenshot: @escaping () -> UIImage?
    ) {
        guard let rootController = window.rootViewController else { return }
        AppDiagnostics.shared.event("overlay", "install", ["windowBounds": NSCoder.string(for: window.bounds), "root": String(describing: type(of: rootController))])
        let overlay = KRKROverlayView(model: model)
        let controller = UIHostingController(rootView: overlay)
        controller.view.backgroundColor = .clear
        controller.view.isOpaque = false
        controller.view.isUserInteractionEnabled = false
        controller.view.translatesAutoresizingMaskIntoConstraints = false
        rootController.addChild(controller)
        rootController.view.addSubview(controller.view)
        NSLayoutConstraint.activate([
            controller.view.leadingAnchor.constraint(equalTo: rootController.view.leadingAnchor),
            controller.view.trailingAnchor.constraint(equalTo: rootController.view.trailingAnchor),
            controller.view.topAnchor.constraint(equalTo: rootController.view.topAnchor),
            controller.view.bottomAnchor.constraint(equalTo: rootController.view.bottomAnchor)
        ])
        controller.didMove(toParent: rootController)
        hostingController = controller
        containerView = rootController.view

        model.onCloseMenu = { [weak self] in self?.hideMenu() }
        model.onExit = onExit
        model.onScreenshot = onScreenshot

        if floatingButtonEnabled {
            installFloatingButton(
                in: rootController.view,
                opacity: floatingButtonOpacity
            )
        }
    }

    func showMenu() {
        AppDiagnostics.shared.event("overlay", "menu.show")
        guard let controller = hostingController else { return }
        controller.view.isUserInteractionEnabled = true
        containerView?.bringSubviewToFront(controller.view)
        model.menuVisible = true
    }

    func hideMenu() {
        AppDiagnostics.shared.event("overlay", "menu.hide")
        model.menuVisible = false
        hostingController?.view.isUserInteractionEnabled = false
        if let floatingButton {
            containerView?.bringSubviewToFront(floatingButton)
        }
    }

    func update(_ performance: KRKRPerformanceSnapshot) {
        model.performance = performance
    }

    // Render only UIKit overlays over the native Metal screenshot. Drawing the
    // full SDL hierarchy would cover the captured image with an empty GPU view.
    func drawScreenshotOverlay(in window: UIWindow) {
        let views = [hostingController?.view, floatingButton].compactMap { $0 }
        guard let container = containerView else { return }
        for view in container.subviews where views.contains(where: { $0 === view }) {
            guard !view.isHidden, view.alpha > 0 else { continue }
            let rect = view.convert(view.bounds, to: window)
            view.drawHierarchy(in: rect, afterScreenUpdates: true)
        }
    }

    func remove() {
        AppDiagnostics.shared.event("overlay", "remove")
        floatingButton?.removeFromSuperview()
        floatingButton = nil
        hostingController?.willMove(toParent: nil)
        hostingController?.view.removeFromSuperview()
        hostingController?.removeFromParent()
        hostingController = nil
        containerView = nil
    }

    private func installFloatingButton(in view: UIView, opacity: Double) {
        let configuration: UIButton.Configuration
        if #available(iOS 26.0, *) {
            configuration = .glass()
        } else {
            configuration = .filled()
        }
        let button = UIButton(configuration: configuration)
        button.setImage(UIImage(systemName: "pawprint.fill"), for: .normal)
        button.accessibilityLabel = "唤出游戏菜单"
        button.alpha = max(0.1, min(opacity, 1))
        button.frame = CGRect(
            x: max(view.bounds.width - 72, 12),
            y: max(view.safeAreaInsets.top + 12, 20),
            width: 52,
            height: 52
        )
        button.autoresizingMask = [.flexibleLeftMargin, .flexibleBottomMargin]
        button.addAction(UIAction { [weak self] _ in self?.showMenu() }, for: .touchUpInside)
        button.addGestureRecognizer(
            UIPanGestureRecognizer(target: self, action: #selector(dragFloatingButton(_:)))
        )
        view.addSubview(button)
        view.bringSubviewToFront(button)
        floatingButton = button
    }

    @objc private func dragFloatingButton(_ recognizer: UIPanGestureRecognizer) {
        guard let button = recognizer.view, let container = button.superview else { return }
        let translation = recognizer.translation(in: container)
        var center = CGPoint(
            x: button.center.x + translation.x,
            y: button.center.y + translation.y
        )
        let safe = container.safeAreaLayoutGuide.layoutFrame.insetBy(dx: -4, dy: -4)
        center.x = min(
            max(center.x, safe.minX + button.bounds.width / 2),
            safe.maxX - button.bounds.width / 2
        )
        center.y = min(
            max(center.y, safe.minY + button.bounds.height / 2),
            safe.maxY - button.bounds.height / 2
        )
        button.center = center
        recognizer.setTranslation(.zero, in: container)
    }
}

private struct KRKROverlayView: View {
    @ObservedObject var model: KRKROverlayModel

    var body: some View {
        ZStack(alignment: .topLeading) {
            if model.performanceVisible {
                performanceHUD
                    .padding(18)
                    .allowsHitTesting(false)
            }

            if model.menuVisible {
                Color.black.opacity(0.36)
                    .ignoresSafeArea()
                menuPanel
                    .frame(maxWidth: 640)
                    .padding(40)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .sheet(
            isPresented: Binding(
                get: { model.sharedImage != nil },
                set: { if !$0 { model.sharedImage = nil } }
            )
        ) {
            if let image = model.sharedImage {
                ActivitySheet(items: [image])
            }
        }
    }

    private var performanceHUD: some View {
        let stats = model.performance
        return VStack(alignment: .leading, spacing: 2) {
            Text("\(stats.framesPerSecond, specifier: "%.1f") FPS · 间隔 \(stats.frameTimeMilliseconds, specifier: "%.1f") ms")
            Text("主线程 \(stats.cpuFrameTimeMilliseconds, specifier: "%.1f") ms · 峰值 \(stats.maxCpuFrameTimeMilliseconds, specifier: "%.1f") ms")
            if stats.gpuSubmissionTimeMilliseconds >= 0 {
                Text("GPU 提交 \(stats.gpuSubmissionTimeMilliseconds, specifier: "%.1f") ms")
            }
            if stats.presentationWaitTimeMilliseconds >= 0 {
                Text("呈现等待 \(stats.presentationWaitTimeMilliseconds, specifier: "%.1f") ms")
            }
            Text("\(stats.drawableWidth)×\(stats.drawableHeight) · \(stats.renderer)")
            Text("图层合成 · \(stats.gpuLayerComposition ? "GPU Metal" : "软件")")
            if stats.gpuLayerComposition {
                Text("算子 \(stats.gpuLayerOperations) · CPU 回退 \(stats.layerCPUFallbacks)")
                Text("上传 \(ByteCountFormatter.string(fromByteCount: Int64(stats.layerUploadedBytes), countStyle: .memory)) · 回读 \(ByteCountFormatter.string(fromByteCount: Int64(stats.layerReadbackBytes), countStyle: .memory))")
            }
            Text("内存 \(ByteCountFormatter.string(fromByteCount: Int64(stats.residentMemoryBytes), countStyle: .memory))")
            Text("本次游玩 \(duration(stats.elapsedSeconds))")
        }
        .font(.system(size: 12, design: .monospaced))
        .foregroundStyle(.white)
        .padding(9)
        .background(.black.opacity(0.62), in: RoundedRectangle(cornerRadius: 10))
        .accessibilityIdentifier("krkr-performance-hud")
    }

    private var menuPanel: some View {
        VStack(spacing: 20) {
            HStack(spacing: 10) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(model.gameTitle)
                        .font(.headline)
                        .lineLimit(1)
                    Text("本次已游玩 \(duration(model.performance.elapsedSeconds))")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
                RoundButton(symbol: "xmark", label: "关闭菜单") {
                    model.onCloseMenu?()
                }
            }
            LazyVGrid(
                columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: 2),
                spacing: 12
            ) {
                action("截图", "camera") {
                    model.sharedImage = model.onScreenshot?()
                }
                action("退出", "rectangle.portrait.and.arrow.right", destructive: true) {
                    model.onExit?()
                }
            }
        }
        .padding(22)
        .nativeGlassPanel(30)
        .foregroundStyle(.white)
        .accessibilityIdentifier("krkr-game-menu")
    }

    private func action(
        _ title: String,
        _ icon: String,
        destructive: Bool = false,
        perform: @escaping () -> Void
    ) -> some View {
        Button(role: destructive ? .destructive : nil, action: perform) {
            VStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.title2.weight(.medium))
                    .symbolRenderingMode(.hierarchical)
                Text(title).font(.body.weight(.medium))
            }
            .frame(maxWidth: .infinity)
            .frame(height: 72)
        }
        .nativeGlassButtonStyle()
    }

    private func duration(_ seconds: Int) -> String {
        String(format: "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60)
    }
}
