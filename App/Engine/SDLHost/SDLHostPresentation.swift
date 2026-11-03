import UIKit

enum SDLHostError: LocalizedError {
    case orientationUnavailable

    var errorDescription: String? {
        switch self {
        case .orientationUnavailable:
            return String(localized: "Couldn’t switch to landscape. Make sure Orientation Lock allows landscape, rotate the device, then try again.")
        }
    }
}

/// Moves the app between its own window and an SDL engine's window: turns the
/// scene to landscape for the game, swaps the key window both ways, and puts
/// the orientation the player had back afterwards. Shared by every SDL engine.
@MainActor
final class SDLHostPresentation {
    private weak var hostWindow: UIWindow?
    private weak var windowScene: UIWindowScene?
    private var previousOrientation: UIInterfaceOrientation = .unknown
    /// When the launch began, so the app's launch stage stays up long enough
    /// to be read before the game fades in over it.
    private var beganAt = Date.distantPast
    /// The engine window on screen, faded out over the app on the way back.
    private weak var shownEngineWindow: UIWindow?

    /// The shortest time from the start of a launch until the game appears,
    /// rotation included. Most engines are ready well before this.
    private static let minimumLaunchStage: TimeInterval = 1.3

    /// Remembers the app's window and the orientation to come back to.
    func begin(hostWindow window: UIWindow, scene: UIWindowScene) {
        hostWindow = window
        windowScene = scene
        previousOrientation = scene.interfaceOrientation
        beganAt = Date()
    }

    func prepareLandscape(
        scene: UIWindowScene,
        window: UIWindow,
        controller: UIViewController
    ) async throws {
        controller.setNeedsUpdateOfSupportedInterfaceOrientations()
        window.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
        let preferences = UIWindowScene.GeometryPreferences.iOS(
            interfaceOrientations: [.landscapeLeft, .landscapeRight]
        )
        AppDiagnostics.shared.event("UIKit", "geometry.landscape.request")
        scene.requestGeometryUpdate(preferences) { error in
            AppDiagnostics.shared.event("UIKit", "geometry.landscape.rejected", ["error": error.localizedDescription])
        }
        UIViewController.attemptRotationToDeviceOrientation()

        for _ in 0..<80 {
            let sceneLandscape = scene.interfaceOrientation.isLandscape
            let sceneBounds = scene.coordinateSpace.bounds
            let windowBounds = window.bounds
            if sceneLandscape,
               sceneBounds.width > sceneBounds.height,
               windowBounds.width > windowBounds.height {
                AppDiagnostics.shared.windows("geometry.landscape.ready")
                // Let the launch stage redraw at landscape size and the
                // rotation finish before the engine starts loading on the
                // main thread, or the stage stays drawn at portrait size
                // beside black until the game covers it.
                Self.fitPresentedViews(in: window)
                try? await Task.sleep(nanoseconds: Self.rotationSettle)
                Self.fitPresentedViews(in: window)
                return
            }
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        AppDiagnostics.shared.windows("geometry.landscape.timeout")
        throw SDLHostError.orientationUnavailable
    }

    /// About as long as the system's rotation animation.
    private static let rotationSettle: UInt64 = 400_000_000

    /// Sizes the app's root and presented views (the player's full-screen
    /// cover) to the window and lays them out now. A window that was hidden
    /// or not key while the scene turned can keep its views at the old
    /// orientation's size, which shows as the launch stage drawn for the
    /// other orientation with black beside it.
    private static func fitPresentedViews(in window: UIWindow) {
        let bounds = window.bounds
        var controller = window.rootViewController
        while let current = controller {
            if let view = current.viewIfLoaded,
               current === window.rootViewController || current.modalPresentationStyle == .fullScreen
                || current.modalPresentationStyle == .overFullScreen {
                // The presentation's container views fill the window too.
                var container = view.superview
                while let parent = container, parent !== window {
                    if parent.frame != bounds { parent.frame = bounds }
                    container = parent.superview
                }
                if view.frame != bounds { view.frame = bounds }
                view.setNeedsLayout()
                view.layoutIfNeeded()
            }
            controller = current.presentedViewController
        }
        window.layoutIfNeeded()
    }

    /// Counts window swaps, so a fade that finishes after the player has
    /// already left doesn't hide the app's window again.
    private var swapGeneration = 0

    /// Shows the engine's window in place of the app's, fading it in over
    /// the launch stage the app's window shows meanwhile.
    func showEngineWindow(_ engineWindow: UIWindow) {
        swapGeneration += 1
        let generation = swapGeneration
        let host = hostWindow
        shownEngineWindow = engineWindow
        engineWindow.alpha = 0
        engineWindow.makeKeyAndVisible()
        let delay = max(0.05, Self.minimumLaunchStage - Date().timeIntervalSince(beganAt))
        UIView.animate(withDuration: 0.5, delay: delay, options: [.curveEaseInOut, .allowUserInteraction]) {
            engineWindow.alpha = 1
        } completion: { [weak self] _ in
            guard let self, generation == self.swapGeneration, !engineWindow.isHidden else { return }
            host?.isHidden = true
        }
    }

    /// Covers the screen while the scene turns back from landscape after a
    /// game: the game's last frame, filling whatever shape the screen has.
    /// A plain UIKit window, so it follows the rotation exactly, while the
    /// app's window (hidden all game) catches up underneath.
    private var curtain: UIWindow?

    /// Brings the app's window back once the engine's is gone.
    func showHostWindow() {
        swapGeneration += 1
        let lastFrame = shownEngineWindow.flatMap { window in
            window.isHidden ? nil : window.snapshotView(afterScreenUpdates: false)
        }
        shownEngineWindow = nil
        if let scene = windowScene ?? hostWindow?.windowScene {
            curtain?.isHidden = true
            let window = UIWindow(windowScene: scene)
            window.windowLevel = .normal + 1
            window.backgroundColor = .black
            window.rootViewController = CurtainController(content: lastFrame)
            window.isUserInteractionEnabled = false
            window.isHidden = false
            curtain = window
        }
        hostWindow?.isHidden = false
        hostWindow?.makeKeyAndVisible()
        if let host = hostWindow { Self.fitPresentedViews(in: host) }
    }

    /// Fades the curtain out over the app, now laid out for its orientation.
    private func liftCurtain() {
        guard let window = curtain else { return }
        curtain = nil
        UIView.animate(withDuration: 0.4, delay: 0, options: [.curveEaseInOut]) {
            window.alpha = 0
        } completion: { _ in
            window.isHidden = true
            window.rootViewController = nil
        }
    }

    func restorePreviousOrientation() async {
        guard let scene = windowScene else {
            liftCurtain()
            return
        }
        let targetOrientation = previousOrientation
        let mask: UIInterfaceOrientationMask
        switch targetOrientation {
        case .portrait:
            mask = .portrait
        case .portraitUpsideDown:
            mask = .portraitUpsideDown
        case .landscapeLeft:
            mask = .landscapeLeft
        case .landscapeRight:
            mask = .landscapeRight
        default:
            mask = UIDevice.current.userInterfaceIdiom == .pad ? .all : .portrait
        }
        hostWindow?.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
        let preferences = UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: mask)
        AppDiagnostics.shared.event("UIKit", "geometry.restore.request", ["orientation": String(targetOrientation.rawValue)])
        scene.requestGeometryUpdate(preferences) { error in
            AppDiagnostics.shared.event("UIKit", "geometry.restore.rejected", ["error": error.localizedDescription])
        }
        UIViewController.attemptRotationToDeviceOrientation()

        let expectsPortrait = targetOrientation.isPortrait ||
            (targetOrientation == .unknown && UIDevice.current.userInterfaceIdiom != .pad)
        let expectsLandscape = targetOrientation.isLandscape
        var stableLayoutSamples = 0
        for _ in 0..<60 {
            let sceneBounds = scene.coordinateSpace.bounds
            let windowBounds = hostWindow?.bounds ?? .zero
            let layoutMatches: Bool
            if expectsPortrait {
                layoutMatches = scene.interfaceOrientation.isPortrait &&
                    sceneBounds.height > sceneBounds.width &&
                    windowBounds.height > windowBounds.width
            } else if expectsLandscape {
                layoutMatches = scene.interfaceOrientation.isLandscape &&
                    sceneBounds.width > sceneBounds.height &&
                    windowBounds.width > windowBounds.height
            } else {
                layoutMatches = true
            }

            if let host = hostWindow { Self.fitPresentedViews(in: host) }
            if layoutMatches {
                stableLayoutSamples += 1
                if stableLayoutSamples >= 3 { break }
            } else {
                stableLayoutSamples = 0
            }
            try? await Task.sleep(nanoseconds: 50_000_000)
        }
        if let host = hostWindow { Self.fitPresentedViews(in: host) }
        AppDiagnostics.shared.event("UIKit", "geometry.restore.completed", ["stableSamples": String(stableLayoutSamples)])
        // One more frame for SwiftUI to draw the app at its new size before
        // the curtain lifts off it.
        try? await Task.sleep(nanoseconds: 100_000_000)
        liftCurtain()
        windowScene = nil
        previousOrientation = .unknown
    }
}

/// The curtain's content: the game's last frame scaled to fill the screen,
/// centered, whichever way the screen faces.
private final class CurtainController: UIViewController {
    private let content: UIView?

    init(content: UIView?) {
        self.content = content
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .all }
    override var prefersStatusBarHidden: Bool { true }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        if let content {
            content.isUserInteractionEnabled = false
            view.addSubview(content)
        }
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        guard let content, content.bounds.width > 0, content.bounds.height > 0 else { return }
        let bounds = view.bounds
        let scale = max(bounds.width / content.bounds.width, bounds.height / content.bounds.height)
        content.transform = CGAffineTransform(scaleX: scale, y: scale)
        content.center = CGPoint(x: bounds.midX, y: bounds.midY)
    }
}
