import Foundation
import XCTest
@testable import Mikage

final class AppBuildInfoTests: XCTestCase {
    func testBundleVersionAndRevisionSupplyBothDisplayLines() {
        let info = AppBuildInfo(infoDictionary: [
            "CFBundleShortVersionString": "1.2.3", "CFBundleVersion": "87",
            "MikageSourceRevision": "ABCDEF012345"
        ])
        XCTAssertEqual(info.versionLine, "MIKAGE NEXT · 1.2.3")
        XCTAssertEqual(info.headLine, "HEAD abcdef012345")
        XCTAssertEqual(info.build, "87")
        XCTAssertEqual(info.sourceRevision, "abcdef012345")
    }

    func testFullSHA1AndSHA256UseFirstTwelveDigits() {
        for length in [40, 64] {
            let revision = "ABCDEF012345" + String(repeating: "a", count: length - 12)
            XCTAssertEqual(AppBuildInfo.normalizedRevision(revision), "abcdef012345")
        }
        XCTAssertEqual(AppBuildInfo.normalizedRevision(" \nABCDEF012345\t"), "abcdef012345")
    }

    func testMissingOrPlaceholderMetadataIsExplicitlyUnknown() {
        for value in ["", "unknown", "HEAD", "$(MIKAGE_SOURCE_REVISION)", "abcdef", "abcdef012345-dirty",
                      "abcdef01234g", "000000000000", String(repeating: "0", count: 40), "abcdef0123456"] {
            let info = AppBuildInfo(infoDictionary: ["MikageSourceRevision": value])
            XCTAssertEqual(info.headLine, "HEAD 未知", value)
            XCTAssertEqual(info.sourceRevision, "unknown", value)
        }
        let missing = AppBuildInfo(infoDictionary: nil)
        XCTAssertEqual(missing.version, "unknown")
        XCTAssertEqual(missing.build, "unknown")
        XCTAssertEqual(missing.headLine, "HEAD 未知")
        XCTAssertEqual(AppBuildInfo(infoDictionary: ["CFBundleShortVersionString": "  "]).version, "unknown")
    }

    func testDefaultAndExplicitBundleUseSameMetadata() {
        let defaultInfo = AppBuildInfo()
        XCTAssertEqual(defaultInfo, AppBuildInfo(bundle: .main))
        XCTAssertEqual(defaultInfo, AppBuildInfo(infoDictionary: Bundle.main.infoDictionary))
    }
}
