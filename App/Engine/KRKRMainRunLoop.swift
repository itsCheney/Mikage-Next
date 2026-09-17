import CoreFoundation
import Foundation

enum KRKRMainRunLoop {
    @MainActor
    static func perform<T>(_ operation: @escaping () -> T) async -> T {
        // SDL message boxes synchronously run a nested UIKit run loop. Starting
        // from a MainActor/DispatchQueue.main job prevents that loop from
        // draining main-queue touch callbacks. Suspend the job and enter the
        // engine from a run-loop block instead; UIKit still stays on main.
        await withCheckedContinuation { continuation in
            let runLoop = CFRunLoopGetMain()
            CFRunLoopPerformBlock(runLoop, RunLoop.Mode.common.rawValue as CFString) {
                continuation.resume(returning: operation())
            }
            CFRunLoopWakeUp(runLoop)
        }
    }
}
