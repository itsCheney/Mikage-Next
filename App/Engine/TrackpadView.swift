import UIKit

/// The Mouse mode: the screen works as a trackpad for a pointer drawn over
/// the game. Drag one finger to move it, tap to click where it points,
/// tap with two fingers to right-click, and touch and hold to press the
/// button down and drag with it. Games built for a mouse get hover and
/// precise clicks this way.
@MainActor
final class TrackpadView: UIView {
    /// Pointer travel per point of finger travel.
    private static let speed: CGFloat = 1.25
    /// A tap's press is held this long, so engines that read the button
    /// once per frame see it down.
    private static let clickHold: TimeInterval = 0.06

    /// Sends the pointer to the engine, in this view's coordinates.
    var sendPointer: (PointerPhase, CGPoint) -> Void = { _, _ in }
    var onMenuGesture: (() -> Void)?

    private let cursor = UIImageView()
    private(set) var pointer = CGPoint.zero
    private var dragOrigin = CGPoint.zero
    private var heldStart = CGPoint.zero
    private var pressed = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isMultipleTouchEnabled = true
        let configuration = UIImage.SymbolConfiguration(pointSize: 22, weight: .regular)
        cursor.image = UIImage(systemName: "cursorarrow", withConfiguration: configuration)?
            .withTintColor(.white, renderingMode: .alwaysOriginal)
        cursor.sizeToFit()
        cursor.layer.shadowColor = UIColor.black.cgColor
        cursor.layer.shadowOpacity = 0.8
        cursor.layer.shadowRadius = 1.5
        cursor.layer.shadowOffset = CGSize(width: 0, height: 1)
        cursor.isUserInteractionEnabled = false
        addSubview(cursor)

        let hold = UILongPressGestureRecognizer(target: self, action: #selector(hold(_:)))
        hold.minimumPressDuration = 0.35
        addGestureRecognizer(hold)
        let pan = UIPanGestureRecognizer(target: self, action: #selector(pan(_:)))
        pan.maximumNumberOfTouches = 1
        pan.require(toFail: hold)
        addGestureRecognizer(pan)
        let menu = UITapGestureRecognizer(target: self, action: #selector(menuGesture))
        menu.numberOfTouchesRequired = 3
        addGestureRecognizer(menu)
        let secondary = UITapGestureRecognizer(target: self, action: #selector(secondaryClick))
        secondary.numberOfTouchesRequired = 2
        secondary.require(toFail: menu)
        addGestureRecognizer(secondary)
        let click = UITapGestureRecognizer(target: self, action: #selector(click))
        click.require(toFail: secondary)
        addGestureRecognizer(click)

        isAccessibilityElement = true
        accessibilityLabel = String(localized: "Trackpad")
        accessibilityHint = String(localized: "Drag to move the pointer, tap to click.")
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    /// Starts with the pointer in the middle of the screen.
    func start() {
        pointer = CGPoint(x: bounds.midX, y: bounds.midY)
        placeCursor()
        sendPointer(.move, pointer)
    }

    /// Lets go of a held button, as when Mouse mode turns off mid-drag.
    func stop() {
        if pressed {
            pressed = false
            sendPointer(.up, pointer)
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        pointer = clamped(pointer)
        placeCursor()
    }

    private func clamped(_ point: CGPoint) -> CGPoint {
        CGPoint(x: min(max(point.x, bounds.minX), bounds.maxX - 1),
                y: min(max(point.y, bounds.minY), bounds.maxY - 1))
    }

    /// The arrow's tip is the pointer.
    private func placeCursor() {
        cursor.frame.origin = CGPoint(x: pointer.x - 5, y: pointer.y - 3)
    }

    private func move(by translation: CGPoint) {
        pointer = clamped(CGPoint(x: dragOrigin.x + translation.x * Self.speed,
                                  y: dragOrigin.y + translation.y * Self.speed))
        placeCursor()
        sendPointer(.move, pointer)
    }

    @objc private func pan(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            dragOrigin = pointer
        case .changed, .ended:
            move(by: recognizer.translation(in: self))
        default:
            break
        }
    }

    @objc private func hold(_ recognizer: UILongPressGestureRecognizer) {
        let location = recognizer.location(in: self)
        switch recognizer.state {
        case .began:
            dragOrigin = pointer
            heldStart = location
            pressed = true
            sendPointer(.down, pointer)
        case .changed:
            move(by: CGPoint(x: location.x - heldStart.x, y: location.y - heldStart.y))
        case .ended, .cancelled, .failed:
            stop()
        default:
            break
        }
    }

    @objc private func click() {
        let point = pointer
        sendPointer(.move, point)
        sendPointer(.down, point)
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.clickHold) { [weak self] in
            self?.sendPointer(.up, point)
        }
    }

    @objc private func secondaryClick() {
        let point = pointer
        sendPointer(.secondaryDown, point)
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.clickHold) { [weak self] in
            self?.sendPointer(.secondaryUp, point)
        }
    }

    @objc private func menuGesture() {
        onMenuGesture?()
    }
}
