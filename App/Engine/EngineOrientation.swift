import UIKit

/// Turns the scene to landscape for a game and back afterwards. Shared by
/// every engine session.
@MainActor
enum EngineOrientation {
    static func prepareLandscape(
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
        throw KRKRSessionError.orientationUnavailable
    }

    /// Returns the scene to the orientation it had before the game, and waits
    /// until the host window has laid out for it.
    static func restore(
        scene: UIWindowScene,
        to targetOrientation: UIInterfaceOrientation,
        hostWindow: UIWindow?
    ) async {
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
    }
}
