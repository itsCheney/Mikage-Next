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


/// What the game menu asks the coordinator to do.
enum GameMenuAction {
    case close
    case hideButton
    case command(GameCommand)
    case screenshot
    case toggleMouse
    case back
    case toggleQuickActions
    case cycleStats
    case markFinished
    case quit
}

@MainActor
final class KRKROverlayModel: ObservableObject {
    @Published var menuVisible = false
    @Published var performance = KRKRPerformanceSnapshot()
    @Published var sharedImage: UIImage?
    @Published var performanceVisible: Bool
    @Published var performanceDetail: PerformanceDetail
    @Published var hudPlacement: PerformanceHUDPlacement
    @Published var hudDock = PerformanceHUDDock.floating
    /// The full HUD's width, narrowed on small screens.
    @Published var hudWidth: CGFloat = 400
    /// Frame interval of each metrics tick, oldest first; nil for idle ticks.
    @Published var frameHistory: [Double?] = []
    /// The game's auto, skip and story controls as the engine last reported.
    @Published var controls = GameControlState()
    /// Ctrl is held for the Skip quick action.
    @Published var controlSkipHeld = false
    /// Either skip runs: the menu's Skip and the Skip quick action light
    /// together, though each starts its own kind of skip.
    var skipping: Bool { controls.contains(.skip) || controlSkipHeld }
    /// The quick action buttons show; the menu turns them off for a while.
    @Published var quickActionsOn = true
    @Published var mouseOn = false
    @Published var lastQuickSave: Date?

    let gameTitle: String
    let engineName: String
    var floatingButtonEnabled = true
    var quickActions: [QuickActionKind] = []
    /// The engine opens its backlog and saves only while the game waits on
    /// its text (ONScripter).
    var storyWaitsOnText = false
    var perform: ((GameMenuAction) -> Void)?
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
final class KRKROverlayCoordinator: NSObject, UIGestureRecognizerDelegate {
    private static let ballID = "ball"

    private let model: KRKROverlayModel
    private var hostingController: UIHostingController<KRKROverlayView>?
    private var hudContainer: PerformanceHUDContainer?
    private var floatingControls: FloatingControlsView?
    private var trackpad: TrackpadView?
    private weak var containerView: UIView?
    private var controls = GameControls()
    private var options: GameOverlayOptions?
    private var onExit: (() -> Void)?
    private var onBack: (() -> Void)?
    private var onMarkFinished: (() -> Void)?
    private var onScreenshot: (() -> UIImage?)?
    private var onSetPerformance: ((Bool) -> Void)?
    private var refreshTimer: Timer?
    private var ballHidden = false
    private var restoreGesture: UITapGestureRecognizer?
    private var controlSkipHeld: Bool {
        get { model.controlSkipHeld }
        set { model.controlSkipHeld = newValue }
    }

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
        options: GameOverlayOptions,
        controls: GameControls,
        storyWaitsOnText: Bool = false,
        onExit: @escaping () -> Void,
        onBack: @escaping () -> Void,
        onMarkFinished: @escaping () -> Void,
        onScreenshot: @escaping () -> UIImage?,
        onSetPerformance: @escaping (Bool) -> Void
    ) {
        guard let rootController = window.rootViewController else { return }
        AppDiagnostics.shared.event("overlay", "install", ["windowBounds": NSCoder.string(for: window.bounds), "root": String(describing: type(of: rootController))])
        self.options = options
        self.controls = controls
        self.onExit = onExit
        self.onBack = onBack
        self.onMarkFinished = onMarkFinished
        self.onScreenshot = onScreenshot
        self.onSetPerformance = onSetPerformance
        model.floatingButtonEnabled = options.floatingButton
        model.quickActions = options.quickActions
        model.storyWaitsOnText = storyWaitsOnText
        model.lastQuickSave = QuickSaveTimes.date(for: model.gameTitle)
        model.perform = { [weak self] action in self?.perform(action) }
        let root = rootController.view!
        containerView = root

        // Bottom to top: the trackpad, the HUD, the floating buttons, and
        // the menu, which covers them all while open.
        let pad = TrackpadView(frame: root.bounds)
        pad.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        pad.isHidden = true
        pad.sendPointer = { [weak self] phase, point in _ = self?.controls.sendPointer(phase, point) }
        pad.onMenuGesture = { [weak self] in self?.showMenu() }
        root.addSubview(pad)
        trackpad = pad

        let hud = PerformanceHUDContainer(model: model)
        hud.attach(to: rootController)
        hud.isHidden = !model.performanceVisible
        hudContainer = hud
        model.onTuckHUD = { [weak hud] tucked in hud?.setTucked(tucked) }
        model.onFoldHUD = { [weak hud] in hud?.toggleFolded() }

        let floating = FloatingControlsView(frame: root.bounds)
        floating.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        floating.idleOpacity = CGFloat(options.idleOpacity)
        floating.obstacles = { [weak hud] in hud?.occupiedFrame.map { [$0] } ?? [] }
        floating.gameRect = { [weak self, weak floating] in
            guard let self, let floating else { return nil }
            return PerformanceHUDContainer.gameRect(for: self.model.performance.gameSize, in: floating.bounds)
        }
        hud.onMove = { [weak floating] in floating?.relayout(animated: true) }
        if options.floatingButton {
            let ball = FloatingButtonStyle.menuButton()
            ball.accessibilityLabel = String(localized: "Open Game Menu")
            ball.accessibilityIdentifier = "krkr-floating-button"
            ball.addAction(UIAction { [weak self] _ in self?.showMenu() }, for: .touchUpInside)
            floating.add(.init(id: Self.ballID, button: ball, size: FloatingControlsView.ballSize, quickActionIndex: nil))
        }
        for (index, kind) in options.quickActions.prefix(QuickActionKind.limit).enumerated() {
            let button = FloatingButtonStyle.quickAction(symbol: kind.symbol)
            button.accessibilityLabel = kind.displayName
            button.accessibilityIdentifier = "quick-action-\(kind.rawValue)"
            button.addAction(UIAction { [weak self] _ in self?.performQuickAction(kind) }, for: .touchUpInside)
            floating.add(.init(id: kind.rawValue, button: button, size: FloatingControlsView.quickActionSize, quickActionIndex: index))
        }
        root.addSubview(floating)
        floatingControls = floating

        let overlay = KRKROverlayView(model: model)
        let controller = UIHostingController(rootView: overlay)
        controller.view.backgroundColor = .clear
        controller.view.isOpaque = false
        controller.view.isUserInteractionEnabled = false
        controller.view.translatesAutoresizingMaskIntoConstraints = false
        rootController.addChild(controller)
        root.addSubview(controller.view)
        NSLayoutConstraint.activate([
            controller.view.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            controller.view.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            controller.view.topAnchor.constraint(equalTo: root.topAnchor),
            controller.view.bottomAnchor.constraint(equalTo: root.bottomAnchor)
        ])
        controller.didMove(toParent: rootController)
        hostingController = controller
    }

    func showMenu() {
        AppDiagnostics.shared.event("overlay", "menu.show")
        guard let controller = hostingController else { return }
        // The three-finger tap that opened the menu also brings the button back.
        setBallHidden(false)
        controller.view.isUserInteractionEnabled = true
        containerView?.bringSubviewToFront(controller.view)
        refreshControls()
        model.menuVisible = true
        refreshTimer?.invalidate()
        let timer = Timer(timeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshControls() }
        }
        RunLoop.main.add(timer, forMode: .common)
        refreshTimer = timer
    }

    func hideMenu() {
        AppDiagnostics.shared.event("overlay", "menu.hide")
        refreshTimer?.invalidate()
        refreshTimer = nil
        model.menuVisible = false
        hostingController?.view.isUserInteractionEnabled = false
    }

    func setPerformanceVisible(_ visible: Bool) {
        model.performanceVisible = visible
        if visible {
            model.frameHistory = []
        }
        hudContainer?.isHidden = !visible
        hudContainer?.setNeedsLayout()
        floatingControls?.relayout(animated: true)
    }

    func update(_ performance: KRKRPerformanceSnapshot) {
        let gameSizeChanged = performance.gameSize != model.performance.gameSize
        model.performance = performance
        if gameSizeChanged {
            floatingControls?.relayout(animated: false)
        }
        // The game turns auto and skip off itself (a click, a choice,
        // unread text); keep the quick actions' state current.
        if (model.quickActions.contains(.auto) || model.quickActions.contains(.skip)) && !model.menuVisible {
            refreshControls()
        }
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
        let views = [hudContainer, floatingControls, hostingController?.view].compactMap { $0 }
        guard let container = containerView else { return }
        for view in container.subviews where views.contains(where: { $0 === view }) {
            guard !view.isHidden, view.alpha > 0 else { continue }
            let rect = view.convert(view.bounds, to: window)
            view.drawHierarchy(in: rect, afterScreenUpdates: true)
        }
    }

    func remove() {
        AppDiagnostics.shared.event("overlay", "remove")
        refreshTimer?.invalidate()
        refreshTimer = nil
        if controlSkipHeld {
            controlSkipHeld = false
            _ = controls.setControlSkip(false)
        }
        trackpad?.stop()
        trackpad?.removeFromSuperview()
        trackpad = nil
        floatingControls?.invalidateTimers()
        floatingControls?.removeFromSuperview()
        floatingControls = nil
        if let restoreGesture {
            restoreGesture.view?.removeGestureRecognizer(restoreGesture)
        }
        restoreGesture = nil
        hudContainer?.detach()
        hudContainer = nil
        hostingController?.willMove(toParent: nil)
        hostingController?.view.removeFromSuperview()
        hostingController?.removeFromParent()
        hostingController = nil
        containerView = nil
        controls = GameControls()
    }

    // MARK: Actions

    private func refreshControls() {
        let state = controls.state()
        if model.controls != state {
            model.controls = state
        }
        updateQuickActionStates()
    }

    /// Engines take a command on their next step; read their state after it.
    private func refreshSoon() {
        for delay in [0.15, 0.5] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in self?.refreshControls() }
        }
    }

    private func perform(_ action: GameMenuAction) {
        switch action {
        case .close:
            hideMenu()
        case .hideButton:
            hideMenu()
            setBallHidden(true)
            floatingControls?.showToast(String(localized: "Tap with three fingers to bring the button back"))
        case .command(let command):
            AppDiagnostics.shared.event("overlay", "menu.command", ["command": String(describing: command)])
            switch command {
            case .skip where model.skipping:
                stopSkipping()
            case .auto, .skip:
                // Toggles stay in the menu, which shows their new state.
                _ = controls.run(command)
                refreshSoon()
            default:
                hideMenu()
                run(command)
            }
        case .screenshot:
            model.sharedImage = onScreenshot?()
        case .toggleMouse:
            setMouse(!model.mouseOn)
        case .back:
            hideMenu()
            onBack?()
        case .toggleQuickActions:
            setQuickActionsOn(!model.quickActionsOn)
        case .cycleStats:
            cycleStats()
        case .markFinished:
            onMarkFinished?()
        case .quit:
            onExit?()
        }
    }

    /// Runs a command that leaves the menu, and says so when the game can't
    /// take it now.
    private func run(_ command: GameCommand) {
        let done = controls.run(command)
        AppDiagnostics.shared.event("overlay", "command", ["command": String(describing: command), "done": String(done)])
        guard done else {
            floatingControls?.showToast(command == .quickSave
                ? String(localized: "Can’t quick save here")
                : String(localized: "Not available right now"))
            return
        }
        if command == .quickSave {
            let now = Date()
            model.lastQuickSave = now
            QuickSaveTimes.record(now, for: model.gameTitle)
            floatingControls?.showToast(String(localized: "Quick saved · \(now.formatted(date: .omitted, time: .shortened))"))
        }
        refreshSoon()
    }

    private func performQuickAction(_ kind: QuickActionKind) {
        AppDiagnostics.shared.event("overlay", "quickAction", ["action": kind.rawValue])
        floatingControls?.wake()
        switch kind {
        case .quickSave:
            run(.quickSave)
        case .quickLoad:
            run(.quickLoad)
        case .screenshot:
            model.sharedImage = onScreenshot?()
        case .skip:
            if model.skipping {
                stopSkipping()
                return
            }
            // Ctrl held: skips read and unread text alike, unlike the menu's Skip.
            controlSkipHeld = controls.setControlSkip(true)
            if !controlSkipHeld {
                floatingControls?.showToast(String(localized: "Not available right now"))
            }
            updateQuickActionStates()
        case .auto:
            if controls.run(.auto) {
                refreshSoon()
            } else {
                floatingControls?.showToast(String(localized: "Not available right now"))
            }
        case .backlog:
            run(.backlog)
        case .mouse:
            setMouse(!model.mouseOn)
        case .stats:
            cycleStats()
        case .back:
            onBack?()
        }
    }

    /// Stops whichever skip runs: lets go of Ctrl and turns the game's
    /// own skip off.
    private func stopSkipping() {
        if controlSkipHeld {
            controlSkipHeld = false
            _ = controls.setControlSkip(false)
        }
        if model.controls.contains(.skip) {
            _ = controls.run(.skip)
        }
        updateQuickActionStates()
        refreshSoon()
    }

    private func updateQuickActionStates() {
        guard let floatingControls else { return }
        for item in floatingControls.items {
            guard let kind = QuickActionKind(rawValue: item.id) else { continue }
            let on: Bool
            switch kind {
            case .skip: on = model.skipping
            case .auto: on = model.controls.contains(.auto)
            case .mouse: on = model.mouseOn
            case .stats: on = model.performanceVisible
            default: continue
            }
            let value = on ? String(localized: "On") : String(localized: "Off")
            // Runs every tick; a new configuration restarts the glass.
            guard item.button.accessibilityValue != value else { continue }
            item.button.configuration = FloatingButtonStyle.quickActionConfiguration(on: on, symbol: kind.symbol)
            item.button.accessibilityValue = value
            floatingControls.setActive(on, id: item.id)
        }
    }

    private func setBallHidden(_ hidden: Bool) {
        guard model.floatingButtonEnabled, hidden != ballHidden else { return }
        ballHidden = hidden
        floatingControls?.setHidden(hidden, id: Self.ballID)
        floatingControls?.relayout(animated: true)
        AppDiagnostics.shared.event("overlay", "floatingButton.hidden", ["hidden": String(hidden)])
        if hidden {
            // Works whether or not the three-finger menu is on.
            let gesture = UITapGestureRecognizer(target: self, action: #selector(restoreBall))
            gesture.numberOfTouchesRequired = 3
            gesture.cancelsTouchesInView = false
            gesture.delaysTouchesEnded = false
            gesture.delegate = self
            containerView?.addGestureRecognizer(gesture)
            restoreGesture = gesture
        } else if let restoreGesture {
            restoreGesture.view?.removeGestureRecognizer(restoreGesture)
            self.restoreGesture = nil
        }
    }

    @objc private func restoreBall() {
        setBallHidden(false)
    }

    func gestureRecognizer(
        _ gestureRecognizer: UIGestureRecognizer,
        shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer
    ) -> Bool {
        true
    }

    private func setQuickActionsOn(_ on: Bool) {
        model.quickActionsOn = on
        for kind in model.quickActions {
            floatingControls?.setHidden(!on, id: kind.rawValue)
        }
        floatingControls?.relayout(animated: true)
    }

    private func setMouse(_ on: Bool) {
        model.mouseOn = on
        AppDiagnostics.shared.event("overlay", "mouse", ["on": String(on)])
        guard let trackpad else { return }
        if on {
            trackpad.isHidden = false
            trackpad.start()
        } else {
            trackpad.stop()
            trackpad.isHidden = true
        }
        updateQuickActionStates()
    }

    /// Off, simple, full, off.
    private func cycleStats() {
        if !model.performanceVisible {
            setDetail(.simple)
            onSetPerformance?(true)
        } else if model.performanceDetail == .simple {
            setDetail(.full)
        } else {
            onSetPerformance?(false)
        }
        updateQuickActionStates()
    }

    private func setDetail(_ detail: PerformanceDetail) {
        guard model.performanceDetail != detail else { return }
        model.performanceDetail = detail
        hudContainer?.reloadPlacement()
    }
}

// MARK: Menu

private enum MenuPalette {
    static let accent = Color(red: 0x5E / 255, green: 0x5C / 255, blue: 0xE6 / 255)
    static let panel = Color(red: 26 / 255, green: 28 / 255, blue: 48 / 255)
    static let finished = Color(red: 0x5C / 255, green: 0xDB / 255, blue: 0x7C / 255)
    static let quit = Color(red: 214 / 255, green: 48 / 255, blue: 62 / 255)
}

/// Translucent white tiles; accent when on.
private struct MenuTileStyle: ButtonStyle {
    var on = false
    var fill: Color?
    var radius: CGFloat = 22

    func makeBody(configuration: Configuration) -> some View {
        let shape = RoundedRectangle(cornerRadius: radius, style: .continuous)
        configuration.label
            .foregroundStyle(.white)
            .background(shape.fill(fill ?? (on ? MenuPalette.accent.opacity(0.62) : .white.opacity(0.12))))
            .overlay(shape.strokeBorder(.white.opacity(0.22), lineWidth: 1))
            .contentShape(shape)
            .opacity(configuration.isPressed ? 0.72 : 1)
    }
}

private struct KRKROverlayView: View {
    @ObservedObject var model: KRKROverlayModel

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                if model.menuVisible {
                    Color(red: 8 / 255, green: 8 / 255, blue: 24 / 255).opacity(0.36)
                        .ignoresSafeArea()
                        .onTapGesture { model.perform?(.close) }
                        .accessibilityHidden(true)
                    menuPanel(width: min(640, geometry.size.width - 24))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
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

    private func menuPanel(width: CGFloat) -> some View {
        let shape = RoundedRectangle(cornerRadius: 32, style: .continuous)
        // Two mirrored columns side by side; stacked on a narrow screen.
        let stacked = width < 520
        return VStack(spacing: 12) {
            header
            if stacked {
                ScrollView {
                    VStack(spacing: 16) {
                        storyColumn
                        playColumn
                    }
                }
            } else {
                HStack(alignment: .top, spacing: 16) {
                    storyColumn
                    playColumn
                }
            }
            footer
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 16)
        .frame(width: width)
        .background(MenuPalette.panel.opacity(0.5), in: shape)
        .nativeGlassPanel(32)
        .overlay(shape.strokeBorder(.white.opacity(0.26), lineWidth: 1))
        .shadow(color: .black.opacity(0.32), radius: 22, y: 20)
        .foregroundStyle(.white)
        .environment(\.colorScheme, .dark)
        .tint(MenuPalette.accent)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("krkr-game-menu")
    }

    // MARK: Header

    private var header: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 2) {
                Text(model.gameTitle)
                    .font(.system(size: 17, weight: .bold))
                    .lineLimit(1)
                Text("This session \(TimeInterval(model.performance.elapsedSeconds).playTimeClock)")
                    .font(.system(size: 13))
                    .foregroundStyle(.white.opacity(0.78))
                    .monospacedDigit()
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            if model.floatingButtonEnabled {
                Button { model.perform?(.hideButton) } label: {
                    HStack(spacing: 7) {
                        Image(systemName: "nosign")
                            .font(.system(size: 16, weight: .medium))
                        Text("Hide Button")
                            .font(.system(size: 14, weight: .medium))
                        Text("3-finger tap restores")
                            .font(.system(size: 11))
                            .foregroundStyle(.white.opacity(0.6))
                    }
                    .lineLimit(1)
                    .padding(.horizontal, 12)
                    .frame(height: 44)
                    .foregroundStyle(.white.opacity(0.86))
                    .contentShape(Capsule())
                }
                .buttonStyle(.plain)
                .accessibilityIdentifier("game-menu-hide-button")
            }
            Button { model.perform?(.close) } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 16, weight: .semibold))
                    .frame(width: 44, height: 44)
            }
            .buttonStyle(MenuTileStyle(radius: 22))
            .accessibilityLabel(Text("Close Menu"))
        }
        .frame(minHeight: 44)
    }

    // MARK: Columns

    private func columnLabel(_ title: LocalizedStringKey, note: LocalizedStringKey? = nil) -> some View {
        HStack(spacing: 8) {
            Text(title)
                .font(.system(size: 11, weight: .semibold))
                .tracking(0.4)
                .textCase(.uppercase)
                .foregroundStyle(.white.opacity(0.66))
            if let note {
                Text(note)
                    .font(.system(size: 11))
                    .foregroundStyle(.white.opacity(0.55))
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityAddTraits(.isHeader)
    }

    private var storyReady: Bool {
        let story: GameControlState = [.canBacklog, .canSave, .canLoad, .canQuickSave, .canQuickLoad]
        return !model.controls.isDisjoint(with: story)
    }

    private var storyColumn: some View {
        VStack(spacing: 8) {
            columnLabel(
                "Story",
                note: model.storyWaitsOnText && !storyReady ? "Backlog and saves open when the game is waiting on its text." : nil
            )
            HStack(spacing: 8) {
                bigTile("Backlog", "text.alignleft", command: .backlog)
                bigTile("Save", "square.and.arrow.down", command: .save)
                bigTile("Load", "square.and.arrow.up", command: .load)
            }
            HStack(spacing: 8) {
                slimTile("Quick Save", "bolt.fill", command: .quickSave)
                slimTile("Quick Load", "arrow.counterclockwise", command: .quickLoad, detail: quickLoadTime)
            }
        }
    }

    private var playColumn: some View {
        VStack(spacing: 8) {
            columnLabel("Play")
            HStack(spacing: 8) {
                Button { model.perform?(.screenshot) } label: {
                    tileLabel(Text("Screenshot"), "camera")
                }
                .buttonStyle(MenuTileStyle())
                Button { model.perform?(.toggleMouse) } label: {
                    tileLabel(Text("Mouse"), "cursorarrow")
                }
                .buttonStyle(MenuTileStyle(on: model.mouseOn))
                .accessibilityValue(model.mouseOn ? Text("On") : Text("Off"))
                .accessibilityHint(Text("Drag to move the pointer, tap to click."))
                .accessibilityIdentifier("game-menu-mouse")
                // Presses Escape for games whose screens have no close button.
                Button { model.perform?(.back) } label: {
                    VStack(spacing: 6) {
                        Image(systemName: "arrow.uturn.backward")
                            .font(.system(size: 22, weight: .medium))
                        HStack(spacing: 4) {
                            Text("Back")
                            Text(verbatim: "Esc")
                                .font(.system(size: 10, weight: .bold, design: .monospaced))
                                .padding(.horizontal, 5)
                                .padding(.vertical, 1)
                                .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(.white.opacity(0.5), lineWidth: 1))
                                .accessibilityHidden(true)
                        }
                        .font(.system(size: 14, weight: .semibold))
                    }
                    .frame(maxWidth: .infinity)
                    .frame(height: 84)
                }
                .buttonStyle(MenuTileStyle())
                .accessibilityIdentifier("game-menu-back")
            }
            HStack(spacing: 8) {
                toggleTile("Auto", "play.fill", command: .auto, on: model.controls.contains(.auto))
                toggleTile("Skip", "forward.fill", command: .skip, on: model.skipping, detail: Text("Read only"))
            }
        }
    }

    private var quickLoadTime: Text? {
        guard model.controls.contains(.canQuickLoad), let date = model.lastQuickSave else { return nil }
        if Calendar.current.isDateInToday(date) {
            return Text(verbatim: date.formatted(date: .omitted, time: .shortened))
        }
        return Text(verbatim: date.formatted(.dateTime.month(.defaultDigits).day()))
    }

    private func tileLabel(_ title: Text, _ symbol: String) -> some View {
        VStack(spacing: 6) {
            Image(systemName: symbol)
                .font(.system(size: 22, weight: .medium))
            title
                .font(.system(size: 14, weight: .semibold))
                .lineLimit(1)
                .minimumScaleFactor(0.75)
        }
        .padding(.horizontal, 4)
        .frame(maxWidth: .infinity)
        .frame(height: 84)
    }

    private func bigTile(_ title: LocalizedStringKey, _ symbol: String, command: GameCommand) -> some View {
        let enabled = model.controls.allows(command)
        return Button { model.perform?(.command(command)) } label: {
            tileLabel(Text(title), symbol)
        }
        .buttonStyle(MenuTileStyle())
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.42)
        .accessibilityIdentifier("game-menu-\(command)")
    }

    private func slimLabel(_ title: LocalizedStringKey, _ symbol: String, detail: Text?) -> some View {
        HStack(spacing: 8) {
            Image(systemName: symbol)
                .font(.system(size: 16, weight: .semibold))
            Text(title)
                .font(.system(size: 14, weight: .semibold))
                .lineLimit(1)
                .minimumScaleFactor(0.75)
            if let detail {
                detail
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(.white.opacity(0.7))
                    .lineLimit(1)
            }
        }
        .padding(.horizontal, 10)
        .frame(maxWidth: .infinity)
        .frame(height: 56)
    }

    private func slimTile(_ title: LocalizedStringKey, _ symbol: String, command: GameCommand, detail: Text? = nil) -> some View {
        let enabled = model.controls.allows(command)
        return Button { model.perform?(.command(command)) } label: {
            slimLabel(title, symbol, detail: detail)
        }
        .buttonStyle(MenuTileStyle())
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.42)
        .accessibilityIdentifier("game-menu-\(command)")
    }

    /// The game's own auto and skip; they turn each other off.
    private func toggleTile(_ title: LocalizedStringKey, _ symbol: String, command: GameCommand, on: Bool, detail: Text? = nil) -> some View {
        let enabled = model.controls.allows(command) || on
        return Button { model.perform?(.command(command)) } label: {
            slimLabel(title, symbol, detail: detail)
        }
        .buttonStyle(MenuTileStyle(on: on))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.42)
        .accessibilityValue(on ? Text("On") : Text("Off"))
        .accessibilityIdentifier("game-menu-\(command)")
    }

    // MARK: Footer

    private var statsName: Text {
        guard model.performanceVisible else { return Text("Off") }
        return Text(model.performanceDetail.displayName)
    }

    private func pill(_ title: Text, _ symbol: String, value: Text? = nil, symbolColor: Color? = nil) -> some View {
        HStack(spacing: 7) {
            Image(systemName: symbol)
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(symbolColor ?? .white)
            title
                .font(.system(size: 14, weight: .semibold))
            if let value {
                value
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(.white.opacity(0.78))
            }
        }
        .lineLimit(1)
        .padding(.horizontal, 16)
        .frame(height: 44)
    }

    /// Two switches on the left, mirrored by the two exits on the right; in
    /// two rows when one doesn't fit.
    private var footer: some View {
        ViewThatFits(in: .horizontal) {
            HStack(spacing: 8) {
                footerSwitches
                Spacer(minLength: 0)
                footerExits
            }
            VStack(spacing: 8) {
                HStack(spacing: 8) {
                    footerSwitches
                    Spacer(minLength: 0)
                }
                HStack(spacing: 8) {
                    Spacer(minLength: 0)
                    footerExits
                }
            }
        }
    }

    @ViewBuilder
    private var footerSwitches: some View {
        if !model.quickActions.isEmpty {
            Button { model.perform?(.toggleQuickActions) } label: {
                pill(Text("Quick Actions"), "rectangle.stack", value: model.quickActionsOn ? Text("On") : Text("Off"))
            }
            .buttonStyle(MenuTileStyle(on: model.quickActionsOn))
            .accessibilityIdentifier("game-menu-quick-actions")
        }
        Button { model.perform?(.cycleStats) } label: {
            pill(Text("Stats"), "gauge.with.dots.needle.33percent", value: statsName)
        }
        .buttonStyle(MenuTileStyle(on: model.performanceVisible))
        .accessibilityHint(Text("Switches between off, simple and full"))
        .accessibilityIdentifier("krkr-performance-toggle")
    }

    @ViewBuilder
    private var footerExits: some View {
        // Quits like the button beside it, then asks for a rating in the
        // library: reaching the ending is when players want to record it.
        Button { model.perform?(.markFinished) } label: {
            pill(Text("Mark Finished"), "checkmark.seal", symbolColor: MenuPalette.finished)
        }
        .buttonStyle(MenuTileStyle())
        .accessibilityIdentifier("krkr-mark-finished")
        Button(role: .destructive) { model.perform?(.quit) } label: {
            pill(Text("Quit"), "rectangle.portrait.and.arrow.right")
        }
        .buttonStyle(MenuTileStyle(fill: MenuPalette.quit.opacity(0.7)))
    }
}
