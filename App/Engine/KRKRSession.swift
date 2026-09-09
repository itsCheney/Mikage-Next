import Foundation
import UIKit

/// Contract for the native host. The default app deliberately does not pretend to run KRKR.
@MainActor
protocol KRKRSession: AnyObject {
    var state: KRKRSessionState { get }
    var onMenuRequested: (() -> Void)? { get set }
    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) throws
    func requestStop()
    func setForeground(_ foreground: Bool)
}

struct KRKRLaunchConfiguration {
    let gameDirectory: URL
    let entryPoint: URL
    let renderer: String
}

enum KRKRSessionState: Equatable {
    case idle, starting, running, stopping, failed(String)
}
