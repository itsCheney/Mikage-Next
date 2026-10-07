import Foundation
import KRKRRuntime

/// Owns copies of borrowed C views; no pointer survives the profile scope.
struct KRKRLayerWorkSample {
    let origins: String
    let frameSamplesNS: String
    let transitionProfiles: String
    let transitionOverflow: String

    init(profile: inout MikageKRKRLayerWorkProfile) {
        let count = Int(profile.frameSampleCount)
        let copied = withUnsafePointer(to: &profile) { pointer -> (String, String, String, String) in
            let transitions = MikageKRKRLayerWorkProfileTransitions(pointer).map { String(cString: $0) } ?? ""
            let transitionOverflow = MikageKRKRLayerWorkProfileTransitionOverflow(pointer).map { String(cString: $0) } ?? ""
            let origins: String
            if let text = MikageKRKRLayerWorkProfileOrigins(pointer) {
                origins = String(cString: text)
            } else {
                origins = ""
            }
            guard let intervals = MikageKRKRLayerWorkProfileFrameIntervals(pointer),
                  let cpuWall = MikageKRKRLayerWorkProfileFrameCpuWall(pointer) else {
                return (origins, "", transitions, transitionOverflow)
            }
            let frames = (0..<count).map { "\(intervals[$0])/\(cpuWall[$0])" }.joined(separator: ",")
            return (origins, frames, transitions, transitionOverflow)
        }
        origins = copied.0
        frameSamplesNS = copied.1
        transitionProfiles = copied.2
        transitionOverflow = copied.3
    }
}
