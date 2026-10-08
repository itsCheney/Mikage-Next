import Foundation
import KRKRRuntime

/// Owns copies of borrowed C views; no pointer survives the profile scope.
struct KRKRLayerWorkSample {
    let origins: String
    let frameSamplesNS: String
    let transitionProfiles: String
    let transitionOverflow: String
    let shrinkProfiles: String
    let shrinkOverflow: String
    let shrinkReadWaitSamplesNS: String

    init(profile: inout MikageKRKRLayerWorkProfile) {
        let frameCount = Int(profile.frameSampleCount)
        let waitCount = Int(profile.shrinkReadWaitSampleCount)
        let copied = withUnsafePointer(to: &profile) { pointer -> (String, String, String, String, String, String, String) in
            let transitions = MikageKRKRLayerWorkProfileTransitions(pointer).map { String(cString: $0) } ?? ""
            let transitionOverflow = MikageKRKRLayerWorkProfileTransitionOverflow(pointer).map { String(cString: $0) } ?? ""
            let shrinks = MikageKRKRLayerWorkProfileShrinks(pointer).map { String(cString: $0) } ?? ""
            let shrinkOverflow = MikageKRKRLayerWorkProfileShrinkOverflow(pointer).map { String(cString: $0) } ?? ""
            let origins = MikageKRKRLayerWorkProfileOrigins(pointer).map { String(cString: $0) } ?? ""
            let waits = MikageKRKRLayerWorkProfileShrinkReadWait(pointer).map { samples in
                (0..<waitCount).map { String(samples[$0]) }.joined(separator: ",")
            } ?? ""
            let frames: String
            if let intervals = MikageKRKRLayerWorkProfileFrameIntervals(pointer),
               let cpuWall = MikageKRKRLayerWorkProfileFrameCpuWall(pointer) {
                frames = (0..<frameCount).map { "\(intervals[$0])/\(cpuWall[$0])" }.joined(separator: ",")
            } else {
                frames = ""
            }
            return (origins, frames, transitions, transitionOverflow, shrinks, shrinkOverflow, waits)
        }
        origins = copied.0
        frameSamplesNS = copied.1
        transitionProfiles = copied.2
        transitionOverflow = copied.3
        shrinkProfiles = copied.4
        shrinkOverflow = copied.5
        shrinkReadWaitSamplesNS = copied.6
    }
}
