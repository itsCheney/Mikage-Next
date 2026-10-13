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
    var renderer = RendererPreference.storageDefault.displayName
    var gpuLayerComposition = false
    var gpuLayerOperations: UInt64 = 0
    var layerCPUFallbacks: UInt64 = 0
    var layerUploadedBytes: UInt64 = 0
    var layerReadbackBytes: UInt64 = 0
    var layerGammaLUTUploads: UInt64 = 0
    var layerGammaLUTUploadedBytes: UInt64 = 0
    var layerPsTableUploads: UInt64 = 0
    var layerPsTableUploadedBytes: UInt64 = 0
    var residentMemoryBytes: UInt64 = 0
    /// All threads, percent of one core; negative until the second sample.
    var processCPUPercent = -1.0
    var busiestThreads = ""
    var elapsedSeconds = 0
}

@MainActor
private final class KRKROverlayModel: ObservableObject {
    @Published var menuVisible = false
    @Published var performance = KRKRPerformanceSnapshot()
    @Published var sharedImage: UIImage?
    @Published var performanceVisible: Bool

    let gameTitle: String
    var onCloseMenu: (() -> Void)?
    var onExit: (() -> Void)?
    var onScreenshot: (() -> UIImage?)?
    var onTogglePerformance: (() -> Void)?

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
        onScreenshot: @escaping () -> UIImage?,
        onTogglePerformance: @escaping () -> Void
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
        model.onTogglePerformance = onTogglePerformance

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

    func setPerformanceVisible(_ visible: Bool) {
        model.performanceVisible = visible
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
        // Tinted glass in the app accent; the menu opens from it.
        var configuration: UIButton.Configuration
        if #available(iOS 26.0, *) {
            configuration = .prominentGlass()
        } else {
            configuration = .filled()
        }
        configuration.cornerStyle = .capsule
        configuration.baseBackgroundColor = .systemIndigo
        configuration.baseForegroundColor = .white
        configuration.preferredSymbolConfigurationForImage = UIImage.SymbolConfiguration(pointSize: 20, weight: .medium)
        let button = UIButton(configuration: configuration)
        button.tintColor = .systemIndigo
        button.setImage(UIImage(systemName: "moon.stars.fill"), for: .normal)
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
                    .frame(maxWidth: 440)
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
            if stats.processCPUPercent >= 0 {
                Text("全部线程 CPU \(stats.processCPUPercent, specifier: "%.0f")%")
                if !stats.busiestThreads.isEmpty {
                    Text(stats.busiestThreads)
                        .lineLimit(1)
                }
            }
            Text("本次游玩 \(duration(stats.elapsedSeconds))")
        }
        .font(.system(size: 12, design: .monospaced))
        .foregroundStyle(.white)
        .padding(9)
        .background(.black.opacity(0.62), in: RoundedRectangle(cornerRadius: 10))
        .accessibilityIdentifier("krkr-performance-hud")
    }

    private var menuPanel: some View {
        VStack(spacing: 18) {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(model.gameTitle)
                        .font(.headline)
                        .lineLimit(1)
                    Text("本次已游玩 \(duration(model.performance.elapsedSeconds))")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Spacer(minLength: 0)
                RoundButton(symbol: "xmark", label: "关闭菜单") {
                    model.onCloseMenu?()
                }
            }
            actions
        }
        .padding(20)
        .nativeGlassPanel(32)
        .foregroundStyle(.white)
        .environment(\.colorScheme, .dark)
        .tint(.indigo)
        .accessibilityIdentifier("krkr-game-menu")
    }

    /// Tiles sit close together so on iOS 26 their glass blends as one group.
    @ViewBuilder
    private var actions: some View {
        if #available(iOS 26.0, *) {
            GlassEffectContainer(spacing: 8) { actionGrid }
        } else {
            actionGrid
        }
    }

    private var actionGrid: some View {
        HStack(spacing: 8) {
            action("截图", "camera") {
                model.sharedImage = model.onScreenshot?()
            }
            action(
                "性能",
                "gauge.with.dots.needle.33percent",
                highlighted: model.performanceVisible
            ) {
                model.onTogglePerformance?()
            }
            .accessibilityValue(model.performanceVisible ? "已开启" : "已关闭")
            .accessibilityIdentifier("krkr-performance-toggle")
            action("退出", "rectangle.portrait.and.arrow.right", destructive: true) {
                model.onExit?()
            }
        }
    }

    @ViewBuilder
    private func action(
        _ title: String,
        _ icon: String,
        destructive: Bool = false,
        highlighted: Bool = false,
        perform: @escaping () -> Void
    ) -> some View {
        let button = Button(role: destructive ? .destructive : nil, action: perform) {
            VStack(spacing: 6) {
                Image(systemName: icon)
                    .font(.title2.weight(.medium))
                Text(title)
                    .font(.subheadline.weight(destructive ? .semibold : .medium))
            }
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity)
            .frame(height: 72)
        }
        if destructive {
            button
                .nativeGlassButtonStyle(prominent: true)
                .tint(.red)
        } else if highlighted {
            button.nativeGlassButtonStyle(prominent: true)
        } else {
            button.nativeGlassButtonStyle()
        }
    }

    private func duration(_ seconds: Int) -> String {
        TimeInterval(seconds).playTimeClock
    }
}
