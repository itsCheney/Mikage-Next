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

    /// Remembers the app's window and the orientation to come back to.
    func begin(hostWindow window: UIWindow, scene: UIWindowScene) {
        hostWindow = window
        windowScene = scene
        previousOrientation = scene.interfaceOrientation
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

    /// Shows the engine's SDL window in place of the app's.
    func showEngineWindow(_ engineWindow: UIWindow) {
        engineWindow.makeKeyAndVisible()
        hostWindow?.isHidden = true
    }

    /// Brings the app's window back once the engine's is gone.
    func showHostWindow() {
        hostWindow?.isHidden = false
        hostWindow?.makeKeyAndVisible()
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
