import UIKit
import VNCore

/// The session the player talks to. It hands each launch to the runtime of
/// the game's engine and forwards everything else to whichever one is
/// running.
@MainActor
final class EngineSessionRouter: KRKRSession {
    private let sessions: [EngineID: any KRKRSession]
    private var active: any KRKRSession

    var onFinished: ((Result<Void, Error>) -> Void)?
    var onWarning: ((Error) -> Void)?
    var onReturningToLibrary: (() -> Void)?
    var onMarkFinished: (() -> Void)?

    init(sessions: [EngineID: any KRKRSession]) {
        precondition(sessions[.kirikiri] != nil, "The KiriKiri session is required.")
        self.sessions = sessions
        active = sessions[.kirikiri]!
    }

    var state: KRKRSessionState { active.state }

    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) async throws {
        guard let session = sessions[configuration.engine] else {
            throw LibraryError.noGame
        }
        guard active.state == .idle || isFailed(active.state) else {
            throw KRKRSessionError.alreadyRunning
        }
        active = session
        session.onFinished = onFinished
        session.onWarning = onWarning
        session.onReturningToLibrary = onReturningToLibrary
        session.onMarkFinished = onMarkFinished
        // The engine session owns them from here and drops them when it ends.
        onFinished = nil
        onWarning = nil
        onReturningToLibrary = nil
        onMarkFinished = nil
        try await session.start(configuration: configuration, in: viewController)
    }

    func requestStop() { active.requestStop() }
    func setForeground(_ foreground: Bool) { active.setForeground(foreground) }
    func showMenuOverlay() { active.showMenuOverlay() }
    func hideMenuOverlay() { active.hideMenuOverlay() }
    func snapshot() -> UIImage? { active.snapshot() }

    private func isFailed(_ state: KRKRSessionState) -> Bool {
        if case .failed = state { return true }
        return false
    }
}
