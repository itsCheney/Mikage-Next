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
    /// Busiest thread groups first.
    var busiestThreads: [ThreadCPUSample.Group] = []
    var elapsedSeconds = 0
    /// KiriKiri's layer compositor reports its mode; other engines don't have one.
    var showsLayerCompositing = true
    /// Negative when the engine doesn't report it.
    var drawCallsPerFrame = -1.0
    var gpuMemoryBytes: UInt64 = 0
    /// The game as shown before scaling to the screen, stretch included;
    /// zero when unknown. The simple HUD docks in the bars around it.
    var gameSize = CGSize.zero
}

/// An engine's own tile in the game menu, such as ONScripter's backlog.
struct GameMenuAction: Identifiable {
    let id: String
    let title: LocalizedStringKey
    let symbol: String
    let perform: () -> Void
}

@MainActor
final class KRKROverlayModel: ObservableObject {
    @Published var menuVisible = false
    @Published var performance = KRKRPerformanceSnapshot()
    @Published var sharedImage: UIImage?
    @Published var performanceVisible: Bool
    @Published var engineActions: [GameMenuAction] = []
    /// Engine tiles work only at some points of the game.
    @Published var engineActionsAvailable = true
    @Published var hudPlacement: PerformanceHUDPlacement
    @Published var hudDock = PerformanceHUDDock.floating
    /// The full HUD's width, narrowed on small screens.
    @Published var hudWidth: CGFloat = 400
    /// Frame interval of each metrics tick, oldest first; nil for idle ticks.
    @Published var frameHistory: [Double?] = []

    let gameTitle: String
    let engineName: String
    let performanceDetail: PerformanceDetail
    var onCloseMenu: (() -> Void)?
    var onExit: (() -> Void)?
    var onBack: (() -> Void)?
    var onMarkFinished: (() -> Void)?
    var onScreenshot: (() -> UIImage?)?
    var onTogglePerformance: (() -> Void)?
    var onTuckHUD: ((Bool) -> Void)?
    var onFoldHUD: (() -> Void)?

    init(
        gameTitle: String,
        engineName: String,
        performanceVisible: Bool,
        performanceDetail: PerformanceDetail,
        renderer: String
    ) {
        self.gameTitle = gameTitle
        self.engineName = engineName
        self.performanceVisible = performanceVisible
        self.performanceDetail = performanceDetail
        hudPlacement = PerformanceHUDPlacement.saved(for: performanceDetail)
        performance.renderer = renderer
    }
}

@MainActor
final class KRKROverlayCoordinator: NSObject {
    private let model: KRKROverlayModel
    private var hostingController: UIHostingController<KRKROverlayView>?
    private var hudContainer: PerformanceHUDContainer?
    private weak var containerView: UIView?
    private var floatingButton: UIButton?
    private var engineActionsAvailable: (() -> Bool)?

    init(
        gameTitle: String,
        engineName: String,
        performanceVisible: Bool,
        performanceDetail: PerformanceDetail,
        renderer: String
    ) {
        model = KRKROverlayModel(
            gameTitle: gameTitle,
            engineName: engineName,
            performanceVisible: performanceVisible,
            performanceDetail: performanceDetail,
            renderer: renderer
        )
    }

    func install(
        in window: UIWindow,
        floatingButtonEnabled: Bool,
        floatingButtonOpacity: Double,
        onExit: @escaping () -> Void,
        onBack: @escaping () -> Void,
        onMarkFinished: @escaping () -> Void,
        onScreenshot: @escaping () -> UIImage?,
        onTogglePerformance: @escaping () -> Void,
        engineActions: [GameMenuAction] = [],
        engineActionsAvailable: (() -> Bool)? = nil
    ) {
        guard let rootController = window.rootViewController else { return }
        AppDiagnostics.shared.event("overlay", "install", ["windowBounds": NSCoder.string(for: window.bounds), "root": String(describing: type(of: rootController))])
        // Under the menu layer, which covers it while the menu is open.
        let hud = PerformanceHUDContainer(model: model)
        hud.attach(to: rootController)
        hud.isHidden = !model.performanceVisible
        hudContainer = hud
        model.onTuckHUD = { [weak hud] tucked in hud?.setTucked(tucked) }
        model.onFoldHUD = { [weak hud] in hud?.toggleFolded() }

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
        model.onBack = { [weak self] in
            self?.hideMenu()
            onBack()
        }
        model.onMarkFinished = onMarkFinished
        model.onScreenshot = onScreenshot
        model.onTogglePerformance = onTogglePerformance
        // The menu closes first, so an engine screen can open in its place.
        model.engineActions = engineActions.map { item in
            GameMenuAction(id: item.id, title: item.title, symbol: item.symbol) { [weak self] in
                self?.hideMenu()
                item.perform()
            }
        }
        self.engineActionsAvailable = engineActionsAvailable

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
        model.engineActionsAvailable = engineActionsAvailable?() ?? true
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
        if visible {
            model.frameHistory = []
        }
        hudContainer?.isHidden = !visible
        hudContainer?.setNeedsLayout()
    }

    func update(_ performance: KRKRPerformanceSnapshot) {
        model.performance = performance
        guard model.performanceVisible else { return }
        model.frameHistory.append(performance.isIdle ? nil : performance.frameTimeMilliseconds)
        if model.frameHistory.count > PerformanceHUDView.historyLength {
            model.frameHistory.removeFirst(model.frameHistory.count - PerformanceHUDView.historyLength)
        }
        // Values change width as they tick.
        hudContainer?.setNeedsLayout()
    }

    // Render only UIKit overlays over the native Metal screenshot. Drawing the
    // full SDL hierarchy would cover the captured image with an empty GPU view.
    func drawScreenshotOverlay(in window: UIWindow) {
        let views = [hudContainer, hostingController?.view, floatingButton].compactMap { $0 }
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
        hudContainer?.detach()
        hudContainer = nil
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
        button.accessibilityLabel = String(localized: "Open Game Menu")
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

    private var menuPanel: some View {
        VStack(spacing: 18) {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(model.gameTitle)
                        .font(.headline)
                        .lineLimit(1)
                    Text("Played \(duration(model.performance.elapsedSeconds)) this session")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Spacer(minLength: 0)
                RoundButton(symbol: "xmark", label: "Close Menu") {
                    model.onCloseMenu?()
                }
            }
            actions
            if !model.engineActions.isEmpty && !model.engineActionsAvailable {
                Text("Backlog and saves open when the game is waiting on its text.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
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
        VStack(spacing: 8) {
            if !model.engineActions.isEmpty {
                engineActionRow
            }
            mainActionRow
        }
    }

    private var engineActionRow: some View {
        HStack(spacing: 8) {
            ForEach(model.engineActions) { item in
                action(item.title, item.symbol) { item.perform() }
                    .disabled(!model.engineActionsAvailable)
                    .accessibilityIdentifier("game-menu-\(item.id)")
            }
        }
    }

    private var mainActionRow: some View {
        HStack(spacing: 8) {
            // Presses Escape for games whose screens have no close button.
            action("Back", "arrow.uturn.backward") {
                model.onBack?()
            }
            .accessibilityIdentifier("game-menu-back")
            action("Screenshot", "camera") {
                model.sharedImage = model.onScreenshot?()
            }
            action(
                "Performance",
                "gauge.with.dots.needle.33percent",
                highlighted: model.performanceVisible
            ) {
                model.onTogglePerformance?()
            }
            .accessibilityValue(model.performanceVisible ? Text("On") : Text("Off"))
            .accessibilityIdentifier("krkr-performance-toggle")
            // Quits like the tile beside it, then asks for a rating in the
            // library: reaching the ending is when players want to record it.
            action("Mark Finished", "checkmark.seal", tint: .green) {
                model.onMarkFinished?()
            }
            .accessibilityIdentifier("krkr-mark-finished")
            action("Quit", "rectangle.portrait.and.arrow.right", destructive: true) {
                model.onExit?()
            }
        }
    }

    @ViewBuilder
    private func action(
        _ title: LocalizedStringKey,
        _ icon: String,
        destructive: Bool = false,
        highlighted: Bool = false,
        tint: Color? = nil,
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
        } else if let tint {
            button
                .nativeGlassButtonStyle(prominent: true)
                .tint(tint)
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
