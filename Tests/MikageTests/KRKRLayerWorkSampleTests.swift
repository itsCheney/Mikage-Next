import XCTest
import KRKRRuntime
@testable import Mikage

final class KRKRLayerWorkSampleTests: XCTestCase {
    func testEmptyBorrowedViewsAreReadable() {
        var profile = MikageKRKRLayerWorkProfile()
        let copied = KRKRLayerWorkSample(profile: &profile)
        XCTAssertEqual(copied.origins, "")
        XCTAssertEqual(copied.frameSamplesNS, "")
        XCTAssertEqual(copied.transitionProfiles, "")
        XCTAssertEqual(copied.transitionOverflow, "")
    }

    func testLargeOriginsAndFrameBoundaryRemainOwnedAfterProfileReset() {
        var profile = MikageKRKRLayerWorkProfile()
        profile.frameSampleCount = 2048
        let origins = String(repeating: "x", count: 12000)
        withUnsafePointer(to: &profile) { pointer in
            let destination = UnsafeMutablePointer(mutating: MikageKRKRLayerWorkProfileOrigins(pointer)!)
            origins.utf8CString.withUnsafeBufferPointer { bytes in
                destination.update(from: bytes.baseAddress!, count: bytes.count)
            }
            let intervals = UnsafeMutablePointer(mutating: MikageKRKRLayerWorkProfileFrameIntervals(pointer)!)
            let cpu = UnsafeMutablePointer(mutating: MikageKRKRLayerWorkProfileFrameCpuWall(pointer)!)
            intervals[0] = 123
            cpu[0] = 456
            intervals[2047] = UInt64.max
            cpu[2047] = 789
        }
        let copied = KRKRLayerWorkSample(profile: &profile)
        profile = MikageKRKRLayerWorkProfile()
        XCTAssertEqual(copied.origins, origins)
        let frames = copied.frameSamplesNS.split(separator: ",")
        XCTAssertEqual(frames.count, 2048)
        XCTAssertEqual(frames.first.map(String.init), "123/456")
        XCTAssertEqual(frames.last.map(String.init), "\(UInt64.max)/789")
    }

    func testInvalidFrameCountDoesNotReadBeyondArray() {
        var profile = MikageKRKRLayerWorkProfile()
        profile.frameSampleCount = 2049
        XCTAssertEqual(KRKRLayerWorkSample(profile: &profile).frameSamplesNS, "")
    }

    func testLargeTransitionViewsRemainOwnedAfterResetAndInvalidFrameCount() {
        var profile = MikageKRKRLayerWorkProfile()
        profile.transitionProfileVersion = 1
        profile.frameSampleCount = 2049
        let transitions = "[\"" + String(repeating: "x", count: 120000) + "\"]"
        let overflow = "{\"capacityRecords\":0,\"oversizeRecords\":0}"
        withUnsafePointer(to: &profile) { pointer in
            let destination = UnsafeMutablePointer(mutating: MikageKRKRLayerWorkProfileTransitions(pointer)!)
            transitions.utf8CString.withUnsafeBufferPointer { bytes in
                destination.update(from: bytes.baseAddress!, count: bytes.count)
            }
            let folded = UnsafeMutablePointer(mutating: MikageKRKRLayerWorkProfileTransitionOverflow(pointer)!)
            overflow.utf8CString.withUnsafeBufferPointer { bytes in
                folded.update(from: bytes.baseAddress!, count: bytes.count)
            }
        }
        let copied = KRKRLayerWorkSample(profile: &profile)
        profile = MikageKRKRLayerWorkProfile()
        XCTAssertEqual(copied.transitionProfiles, transitions)
        XCTAssertEqual(copied.transitionOverflow, overflow)
        XCTAssertEqual(copied.frameSamplesNS, "")
        profile.transitionProfileVersion = 99
        XCTAssertEqual(KRKRLayerWorkSample(profile: &profile).transitionProfiles, "")
    }
}
