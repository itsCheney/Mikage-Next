import UIKit

/// Reports the app becoming active or inactive, for pausing an engine's
/// stepping and audio while the app is not in front.
@MainActor
final class SDLHostLifecycleObserver {
    private var observers: [NSObjectProtocol] = []

    init(onForegroundChange: @escaping @MainActor @Sendable (Bool) -> Void) {
        let center = NotificationCenter.default
        observers.append(
            center.addObserver(
                forName: UIApplication.willResignActiveNotification,
                object: nil,
                queue: .main
            ) { _ in
                Task { @MainActor in onForegroundChange(false) }
            }
        )
        observers.append(
            center.addObserver(
                forName: UIApplication.didBecomeActiveNotification,
                object: nil,
                queue: .main
            ) { _ in
                Task { @MainActor in onForegroundChange(true) }
            }
        )
    }

    deinit {
        for observer in observers {
            NotificationCenter.default.removeObserver(observer)
        }
    }
}
