import SwiftUI
import UIKit

struct KRKRPerformanceSnapshot {
    var framesPerSecond = 0.0
    var frameTimeMilliseconds = 0.0
    var drawableWidth: Int32 = 0
    var drawableHeight: Int32 = 0
    var renderer = "Metal"
    var residentMemoryBytes: UInt64 = 0
    var elapsedSeconds = 0
}

@MainActor
private final class KRKROverlayModel: ObservableObject {
    @Published var menuVisible = false
    @Published var confirmExit = false
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
        guard let controller = hostingController else { return }
        controller.view.isUserInteractionEnabled = true
        containerView?.bringSubviewToFront(controller.view)
        model.menuVisible = true
    }

    func hideMenu() {
        model.menuVisible = false
        hostingController?.view.isUserInteractionEnabled = false
        if let floatingButton {
            containerView?.bringSubviewToFront(floatingButton)
        }
    }

    func update(_ performance: KRKRPerformanceSnapshot) {
        model.performance = performance
    }

    func remove() {
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
        .confirmationDialog(
            "结束游戏并返回游戏库？",
            isPresented: $model.confirmExit,
            titleVisibility: .visible
        ) {
            Button("返回游戏库", role: .destructive) {
                model.onExit?()
            }
            Button("继续游戏", role: .cancel) { }
        }
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
            Text("\(stats.framesPerSecond, specifier: "%.1f") FPS · \(stats.frameTimeMilliseconds, specifier: "%.1f") ms")
            Text("\(stats.drawableWidth)×\(stats.drawableHeight) · \(stats.renderer)")
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
                    model.confirmExit = true
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
