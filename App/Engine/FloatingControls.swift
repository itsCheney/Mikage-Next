import UIKit

/// Where each floating button was last left, as its center in fractions of
/// the screen. Shared by every game, as the settings page shows them.
enum FloatingControlPositions {
    private static let key = "floatingControls.positions"

    static func load() -> [String: CGPoint] {
        guard let stored = UserDefaults.standard.dictionary(forKey: key) as? [String: [Double]] else { return [:] }
        return stored.compactMapValues { $0.count == 2 ? CGPoint(x: $0[0], y: $0[1]) : nil }
    }

    static func save(_ positions: [String: CGPoint]) {
        UserDefaults.standard.set(positions.mapValues { [Double($0.x), Double($0.y)] }, forKey: key)
    }

    static func reset() {
        UserDefaults.standard.removeObject(forKey: key)
    }

    /// Before the player moves them: the menu button at the top right, the
    /// first quick action at the bottom right and the second at the bottom
    /// left. Each then snaps to its edge, or into the bar beside the game.
    static func defaultPosition(for id: String, quickActionIndex: Int?) -> CGPoint {
        guard let index = quickActionIndex else { return CGPoint(x: 1, y: 0) }
        return index == 0 ? CGPoint(x: 1, y: 1) : CGPoint(x: 0, y: 1)
    }
}

/// Full-screen layer holding the menu button and the quick actions. Touches
/// anywhere else reach the game (or the HUD under it). Drag a button to move
/// it: let go and it snaps to the nearest screen edge, or stays centered in
/// the bar beside a letterboxed game. Buttons never rest on each other or
/// on the performance HUD; one that would slides along its edge.
@MainActor
final class FloatingControlsView: UIView {
    static let ballSize: CGFloat = 44
    static let quickActionSize: CGFloat = 38
    private static let edgeInset: CGFloat = 8
    private static let spacing: CGFloat = 8
    private static let idleDelay: TimeInterval = 3

    struct Item {
        let id: String
        let button: UIButton
        let size: CGFloat
        let quickActionIndex: Int?
    }

    /// Frames the buttons keep clear of, such as the HUD.
    var obstacles: () -> [CGRect] = { [] }
    /// The game's rectangle in this view when letterboxed, or nil.
    var gameRect: () -> CGRect? = { nil }
    var idleOpacity: CGFloat = 0.38 {
        didSet { applyIdle(animated: false) }
    }

    private(set) var items: [Item] = []
    private var positions = FloatingControlPositions.load()
    /// Most recently moved first: it keeps its spot, the others make room.
    private var priority: [String] = []
    private var dragging: (id: String, startCenter: CGPoint)?
    private var idleTimer: Timer?
    private var idle = false
    /// Buttons that stay fully opaque while on, such as a held skip.
    private var activeIDs: Set<String> = []
    private let snapGuide = CAShapeLayer()
    private let toast = ToastLabel()
    private var toastTimer: Timer?

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        snapGuide.fillColor = UIColor.clear.cgColor
        snapGuide.strokeColor = UIColor.white.withAlphaComponent(0.6).cgColor
        snapGuide.lineWidth = 1.5
        snapGuide.lineDashPattern = [4, 4]
        snapGuide.isHidden = true
        layer.addSublayer(snapGuide)
        toast.alpha = 0
        addSubview(toast)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        let hit = super.hitTest(point, with: event)
        return hit === self || hit === toast ? nil : hit
    }

    func add(_ item: Item) {
        items.append(item)
        item.button.bounds = CGRect(x: 0, y: 0, width: item.size, height: item.size)
        item.button.layer.cornerRadius = item.size / 2
        let pan = UIPanGestureRecognizer(target: self, action: #selector(drag(_:)))
        item.button.addGestureRecognizer(pan)
        item.button.addAction(UIAction { [weak self] _ in self?.wake() }, for: .touchDown)
        addSubview(item.button)
        bringSubviewToFront(toast)
        setNeedsLayout()
        wake()
    }

    func setHidden(_ hidden: Bool, id: String) {
        items.first { $0.id == id }?.button.isHidden = hidden
        setNeedsLayout()
    }

    func setActive(_ active: Bool, id: String) {
        if active { activeIDs.insert(id) } else { activeIDs.remove(id) }
        applyIdle(animated: true)
    }

    /// Forgets where the buttons were moved, as the settings page's reset does.
    func resetPositions() {
        positions = [:]
        priority = []
        FloatingControlPositions.reset()
        relayout(animated: true)
    }

    func relayout(animated: Bool) {
        let frames = restingFrames()
        let apply = {
            for item in self.items where self.dragging?.id != item.id {
                if let frame = frames[item.id] { item.button.center = CGPoint(x: frame.midX, y: frame.midY) }
            }
        }
        if animated {
            UIView.animate(withDuration: 0.4, delay: 0, usingSpringWithDamping: 0.86, initialSpringVelocity: 0,
                           options: [.allowUserInteraction, .beginFromCurrentState], animations: apply)
        } else {
            apply()
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard dragging == nil else { return }
        relayout(animated: false)
        toast.sizeToFit()
        let safe = bounds.inset(by: safeAreaInsets)
        toast.center = CGPoint(x: bounds.midX, y: safe.minY + 10 + toast.bounds.height / 2)
    }

    /// A short note at the top of the screen, such as "Quick saved · 21:32".
    func showToast(_ text: String) {
        toast.text = text
        setNeedsLayout()
        layoutIfNeeded()
        toastTimer?.invalidate()
        UIView.animate(withDuration: 0.2) { self.toast.alpha = 1 }
        toastTimer = Timer.scheduledTimer(withTimeInterval: 1.8, repeats: false) { [weak self] _ in
            Task { @MainActor in
                UIView.animate(withDuration: 0.35) { self?.toast.alpha = 0 }
            }
        }
        UIAccessibility.post(notification: .announcement, argument: text)
    }

    // MARK: Placement

    private var safeFrame: CGRect {
        bounds.inset(by: safeAreaInsets).insetBy(dx: Self.edgeInset, dy: Self.edgeInset)
    }

    /// The bars beside a letterboxed game that a button of this size fits in.
    private func bars(for size: CGFloat) -> [CGRect] {
        guard let game = gameRect(), !game.isEmpty else { return [] }
        let safe = safeFrame
        let left = CGRect(x: bounds.minX, y: safe.minY, width: game.minX - bounds.minX, height: safe.height)
        let right = CGRect(x: game.maxX, y: safe.minY, width: bounds.maxX - game.maxX, height: safe.height)
        return [left, right].filter { $0.width >= size + 2 * Self.edgeInset }
    }

    /// Where a button dropped at `center` comes to rest, before making room
    /// for the others.
    private func snapped(_ center: CGPoint, size: CGFloat) -> CGPoint {
        let safe = safeFrame
        let y = min(max(center.y, safe.minY + size / 2), safe.maxY - size / 2)
        if let bar = bars(for: size).first(where: { $0.minX <= center.x && center.x <= $0.maxX }) {
            return CGPoint(x: bar.midX, y: y)
        }
        let x = center.x < bounds.midX ? safe.minX + size / 2 : safe.maxX - size / 2
        return CGPoint(x: x, y: y)
    }

    private func storedCenter(of item: Item) -> CGPoint {
        let fraction = positions[item.id]
            ?? FloatingControlPositions.defaultPosition(for: item.id, quickActionIndex: item.quickActionIndex)
        return CGPoint(x: bounds.minX + fraction.x * bounds.width, y: bounds.minY + fraction.y * bounds.height)
    }

    private func restingFrames() -> [String: CGRect] {
        var occupied = obstacles().map { $0.insetBy(dx: -Self.spacing, dy: -Self.spacing) }
        var frames: [String: CGRect] = [:]
        let ordered = items
            .filter { !$0.button.isHidden }
            .sorted { (priority.firstIndex(of: $0.id) ?? Int.max) < (priority.firstIndex(of: $1.id) ?? Int.max) }
        for item in ordered {
            let center = snapped(storedCenter(of: item), size: item.size)
            let frame = clear(of: occupied, center: center, size: item.size)
            frames[item.id] = frame
            occupied.append(frame.insetBy(dx: -Self.spacing / 2, dy: -Self.spacing / 2))
        }
        return frames
    }

    /// Slides the button along its edge, nearest spot first, until it
    /// overlaps nothing; failing that, it stays where it was dropped.
    private func clear(of occupied: [CGRect], center: CGPoint, size: CGFloat) -> CGRect {
        let safe = safeFrame
        func frame(_ y: CGFloat) -> CGRect {
            CGRect(x: center.x - size / 2, y: y - size / 2, width: size, height: size)
        }
        func free(_ rect: CGRect) -> Bool {
            !occupied.contains { $0.intersects(rect) }
        }
        let start = frame(center.y)
        if free(start) { return start }
        var offset: CGFloat = 4
        while offset < safe.height {
            for y in [center.y + offset, center.y - offset]
            where y >= safe.minY + size / 2 && y <= safe.maxY - size / 2 {
                let candidate = frame(y)
                if free(candidate) { return candidate }
            }
            offset += 4
        }
        return start
    }

    // MARK: Dragging

    @objc private func drag(_ recognizer: UIPanGestureRecognizer) {
        guard let button = recognizer.view as? UIButton,
              let item = items.first(where: { $0.button === button }) else { return }
        let translation = recognizer.translation(in: self)
        switch recognizer.state {
        case .began:
            dragging = (item.id, button.center)
            wake()
            bringSubviewToFront(button)
            UIView.animate(withDuration: 0.18) {
                button.transform = CGAffineTransform(scaleX: 1.14, y: 1.14)
                button.layer.shadowOpacity = 0.35
            }
        case .changed:
            guard let start = dragging?.startCenter else { return }
            button.center = CGPoint(x: start.x + translation.x, y: start.y + translation.y)
            showGuide(at: snapped(button.center, size: item.size), size: item.size)
            wake()
        case .ended, .cancelled, .failed:
            guard dragging != nil else { return }
            dragging = nil
            snapGuide.isHidden = true
            let resting = snapped(button.center, size: item.size)
            positions[item.id] = CGPoint(
                x: (resting.x - bounds.minX) / max(bounds.width, 1),
                y: (resting.y - bounds.minY) / max(bounds.height, 1)
            )
            FloatingControlPositions.save(positions)
            priority.removeAll { $0 == item.id }
            priority.insert(item.id, at: 0)
            AppDiagnostics.shared.event("overlay", "floatingControl.placed", [
                "id": item.id, "x": String(format: "%.2f", positions[item.id]?.x ?? 0),
                "y": String(format: "%.2f", positions[item.id]?.y ?? 0)
            ])
            UIView.animate(withDuration: 0.18) {
                button.transform = .identity
                button.layer.shadowOpacity = 0.2
            }
            relayout(animated: true)
            wake()
        default:
            break
        }
    }

    private func showGuide(at center: CGPoint, size: CGFloat) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        snapGuide.path = UIBezierPath(ovalIn: CGRect(x: center.x - size / 2, y: center.y - size / 2, width: size, height: size)).cgPath
        snapGuide.isHidden = false
        CATransaction.commit()
    }

    // MARK: Idle

    /// Full opacity while in use; fades to the idle opacity a few seconds later.
    func wake() {
        idle = false
        applyIdle(animated: true)
        idleTimer?.invalidate()
        idleTimer = Timer.scheduledTimer(withTimeInterval: Self.idleDelay, repeats: false) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.dragging == nil else { return }
                self.idle = true
                self.applyIdle(animated: true)
            }
        }
    }

    private func applyIdle(animated: Bool) {
        let update = {
            for item in self.items {
                item.button.alpha = self.idle && !self.activeIDs.contains(item.id) ? max(0.1, min(self.idleOpacity, 1)) : 1
            }
        }
        if animated {
            UIView.animate(withDuration: 0.35, delay: 0, options: [.allowUserInteraction, .beginFromCurrentState], animations: update)
        } else {
            update()
        }
    }

    func invalidateTimers() {
        idleTimer?.invalidate()
        toastTimer?.invalidate()
    }
}

/// A rounded dark pill with white text.
private final class ToastLabel: UILabel {
    private let insets = UIEdgeInsets(top: 8, left: 16, bottom: 8, right: 16)

    override init(frame: CGRect) {
        super.init(frame: frame)
        font = .systemFont(ofSize: 14, weight: .semibold)
        textColor = .white
        backgroundColor = UIColor(red: 14 / 255, green: 16 / 255, blue: 30 / 255, alpha: 0.72)
        layer.cornerRadius = 17
        layer.cornerCurve = .continuous
        layer.masksToBounds = true
        textAlignment = .center
        isUserInteractionEnabled = false
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func drawText(in rect: CGRect) {
        super.drawText(in: rect.inset(by: insets))
    }

    override func sizeThatFits(_ size: CGSize) -> CGSize {
        let fitted = super.sizeThatFits(size)
        return CGSize(width: fitted.width + insets.left + insets.right, height: max(34, fitted.height + insets.top + insets.bottom))
    }
}

/// The floating button styles: the menu button in accent glass, quick
/// actions in dark glass (accent while on).
enum FloatingButtonStyle {
    static let accent = UIColor(red: 0x5E / 255, green: 0x5C / 255, blue: 0xE6 / 255, alpha: 1)

    static func menuButton() -> UIButton {
        var configuration: UIButton.Configuration
        if #available(iOS 26.0, *) {
            configuration = .prominentGlass()
        } else {
            configuration = .filled()
        }
        configuration.cornerStyle = .capsule
        configuration.baseBackgroundColor = accent.withAlphaComponent(0.7)
        configuration.baseForegroundColor = .white
        configuration.preferredSymbolConfigurationForImage = UIImage.SymbolConfiguration(pointSize: 18, weight: .medium)
        configuration.contentInsets = .zero
        let button = UIButton(configuration: configuration)
        button.tintColor = accent
        button.setImage(UIImage(systemName: "moon.stars.fill"), for: .normal)
        decorate(button)
        return button
    }

    static func quickAction(symbol: String) -> UIButton {
        let button = UIButton(configuration: quickActionConfiguration(on: false, symbol: symbol))
        decorate(button)
        return button
    }

    /// The whole configuration, image included: a button given a new
    /// configuration drops the image of the old one.
    static func quickActionConfiguration(on: Bool, symbol: String) -> UIButton.Configuration {
        var configuration: UIButton.Configuration
        if #available(iOS 26.0, *) {
            configuration = on ? .prominentGlass() : .glass()
        } else {
            configuration = .filled()
        }
        configuration.cornerStyle = .capsule
        configuration.baseBackgroundColor = on
            ? accent.withAlphaComponent(0.62)
            : UIColor(red: 14 / 255, green: 16 / 255, blue: 30 / 255, alpha: 0.42)
        configuration.baseForegroundColor = .white
        configuration.image = UIImage(systemName: symbol)
        configuration.preferredSymbolConfigurationForImage = UIImage.SymbolConfiguration(pointSize: 15, weight: .semibold)
        configuration.contentInsets = .zero
        return configuration
    }

    private static func decorate(_ button: UIButton) {
        button.layer.borderColor = UIColor.white.withAlphaComponent(0.35).cgColor
        button.layer.borderWidth = 1
        button.layer.cornerCurve = .circular
        button.layer.shadowColor = UIColor.black.cgColor
        button.layer.shadowOpacity = 0.2
        button.layer.shadowRadius = 8
        button.layer.shadowOffset = CGSize(width: 0, height: 4)
    }
}
