import XCTest
import UIKit
@testable import Mikage

final class LibraryLayoutTests: XCTestCase {
    private let card = CGSize(width: 160, height: 90)

    func testFillRectCentersWithoutFocusOffset() {
        // 4:3 in 16:9: full width, taller than the card, centered.
        let rect = CardArtwork.fillRect(aspect: 4.0 / 3.0, in: card, focus: CGPoint(x: 0.5, y: 0.5))
        XCTAssertEqual(rect.width, 160, accuracy: 0.001)
        XCTAssertEqual(rect.height, 120, accuracy: 0.001)
        XCTAssertEqual(rect.minY, -15, accuracy: 0.001)
        XCTAssertEqual(rect.minX, 0, accuracy: 0.001)
    }

    func testFillRectFollowsFocusAndStaysInsideEdges() {
        let high = CardArtwork.fillRect(aspect: 4.0 / 3.0, in: card, focus: CGPoint(x: 0.5, y: 0.3))
        // 45 - 0.3 * 120 would leave a gap above the cover, so it clamps.
        XCTAssertEqual(high.minY, 0, accuracy: 0.001)
        let low = CardArtwork.fillRect(aspect: 4.0 / 3.0, in: card, focus: CGPoint(x: 0.5, y: 1))
        XCTAssertEqual(low.maxY, 90, accuracy: 0.001)
        // A banner wider than the card crops its sides instead.
        let wide = CardArtwork.fillRect(aspect: 3, in: card, focus: CGPoint(x: 0, y: 0.5))
        XCTAssertEqual(wide.height, 90, accuracy: 0.001)
        XCTAssertEqual(wide.minX, 0, accuracy: 0.001)
    }

    func testMasonryPlacesEachCardUnderTheShortestColumn() {
        let heights: [CGFloat] = [100, 50, 60, 40]
        let frames = MasonryLayout.frames(
            width: 212,
            columns: 2,
            spacing: 12,
            heights: heights.map { h in { _ in h } }
        )
        XCTAssertEqual(frames.map(\.width), [100, 100, 100, 100])
        XCTAssertEqual(frames.map(\.minX), [0, 112, 112, 0])
        XCTAssertEqual(frames.map(\.minY), [0, 0, 62, 112])
    }

    func testMasonryColumnCountMatchesAdaptiveGrid() {
        XCTAssertEqual(MasonryLayout.columnCount(width: 358, minimum: 150, spacing: 12), 2)
        XCTAssertEqual(MasonryLayout.columnCount(width: 100, minimum: 150, spacing: 12), 1)
        XCTAssertEqual(MasonryLayout.columnCount(width: 800, minimum: 180, spacing: 12), 4)
    }

    func testCoverLayoutDefaultsToOriginalAndSurvivesUnknownValues() throws {
        XCTAssertEqual(PlayerSettings().coverLayout, .original)
        let decoded = try JSONDecoder().decode(PlayerSettings.self, from: Data(#"{"coverLayout":"shelf"}"#.utf8))
        XCTAssertEqual(decoded.coverLayout, .original)
        let masonry = try JSONDecoder().decode(PlayerSettings.self, from: Data(#"{"coverLayout":"masonry"}"#.utf8))
        XCTAssertEqual(masonry.coverLayout, .masonry)
    }
}
