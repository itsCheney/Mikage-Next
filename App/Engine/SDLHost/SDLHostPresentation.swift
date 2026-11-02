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
                await Task.yield()
                return
            }
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        AppDiagnostics.shared.windows("geometry.landscape.timeout")
        throw SDLHostError.orientationUnavailable
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

    /// Brings the app's window back once the engine's is gone.
    func showHostWindow() {
        swapGeneration += 1
        // The game's last frame, laid over the app's window and faded out,
        // so leaving dissolves into the launch stage instead of cutting.
        let lastFrame = shownEngineWindow.flatMap { window in
            window.isHidden ? nil : window.snapshotView(afterScreenUpdates: false)
        }
        shownEngineWindow = nil
        hostWindow?.isHidden = false
        hostWindow?.makeKeyAndVisible()
        if let lastFrame, let host = hostWindow {
            lastFrame.frame = host.bounds
            lastFrame.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            lastFrame.isUserInteractionEnabled = false
            host.addSubview(lastFrame)
            UIView.animate(withDuration: 0.4, delay: 0, options: [.curveEaseInOut]) {
                lastFrame.alpha = 0
            } completion: { _ in
                lastFrame.removeFromSuperview()
            }
        }
    }

    func restorePreviousOrientation() async {
        guard let scene = windowScene else { return }
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

            if layoutMatches {
                stableLayoutSamples += 1
                if stableLayoutSamples >= 3 { break }
            } else {
                stableLayoutSamples = 0
            }
            try? await Task.sleep(nanoseconds: 50_000_000)
        }
        hostWindow?.layoutIfNeeded()
        AppDiagnostics.shared.event("UIKit", "geometry.restore.completed", ["stableSamples": String(stableLayoutSamples)])
        try? await Task.sleep(nanoseconds: 100_000_000)
        windowScene = nil
        previousOrientation = .unknown
    }
}
