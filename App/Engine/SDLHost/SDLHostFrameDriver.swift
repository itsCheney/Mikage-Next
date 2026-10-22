import QuartzCore

/// Steps an SDL engine once per display frame on the main run loop.
@MainActor
final class SDLHostFrameDriver {
    @MainActor
    private final class Target: NSObject {
        weak var owner: SDLHostFrameDriver?
        init(owner: SDLHostFrameDriver) { self.owner = owner }
        @objc func tick() { owner?.tick() }
    }

    private var displayLink: CADisplayLink?
    private var target: Target?
    private var step: (@MainActor () -> Void)?
    private var isStepping = false

    func start(preferredFramesPerSecond fps: Float, step: @escaping @MainActor () -> Void) {
        stop()
        self.step = step
        let target = Target(owner: self)
        let link = CADisplayLink(target: target, selector: #selector(Target.tick))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: fps, maximum: fps, preferred: fps)
        link.add(to: .main, forMode: .common)
        self.target = target
        displayLink = link
    }

    func stop() {
        displayLink?.invalidate()
        displayLink = nil
        target = nil
        step = nil
    }

    private func tick() {
        // A synchronous SDL dialog pumps the run loop, which can fire another
        // display-link callback before the current script invocation returns.
        guard !isStepping else { return }
        isStepping = true
        defer { isStepping = false }
        step?()
    }
}
