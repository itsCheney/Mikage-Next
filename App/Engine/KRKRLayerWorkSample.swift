import Foundation
import KRKRRuntime

/// Owns copies of borrowed C views; no pointer survives the profile scope.
struct KRKRLayerWorkSample {
    let origins: String
    let frameSamplesNS: String

    init(profile: inout MikageKRKRLayerWorkProfile) {
        let count = Int(profile.frameSampleCount)
        let copied = withUnsafePointer(to: &profile) { pointer -> (String, String) in
            let origins: String
            if let text = MikageKRKRLayerWorkProfileOrigins(pointer) {
                origins = String(cString: text)
            } else {
                origins = ""
            }
            guard let intervals = MikageKRKRLayerWorkProfileFrameIntervals(pointer),
                  let cpuWall = MikageKRKRLayerWorkProfileFrameCpuWall(pointer) else {
                return (origins, "")
            }
            let frames = (0..<count).map { "\(intervals[$0])/\(cpuWall[$0])" }.joined(separator: ",")
            return (origins, frames)
        }
        origins = copied.0
        frameSamplesNS = copied.1
    }
}
