import XCTest
@testable import VNCore

final class ONSSavesTests: XCTestCase {
    func testSpeakerComesFromTagOrText() {
        XCTAssertEqual(ONSLine(name: "春人", text: "「至少，比在这里等一整晚要好。」"),
                       ONSLine(name: "春人", text: "「至少，比在这里等一整晚要好。」"))
        let bracketed = ONSLine(name: "", text: "【小雪】「末班车，好像不会来了呢。」")
        XCTAssertEqual(bracketed.name, "小雪")
        XCTAssertEqual(bracketed.text, "「末班车，好像不会来了呢。」")
        let inline = ONSLine(name: "", text: "小雪「……你是认真的？」")
        XCTAssertEqual(inline.name, "小雪")
        XCTAssertEqual(inline.text, "「……你是认真的？」")
        let narration = ONSLine(name: "", text: "她把围巾往上拉了拉，没有再说话。「嗯」")
        XCTAssertEqual(narration.name, "")
    }

    func testLineBreaksAndIndentsJoin() {
        XCTAssertEqual(ONSLine(name: "", text: "\u{3000}雪从清晨一直下到现在，\n车站的长椅上积了薄薄一层。\n").text,
                       "雪从清晨一直下到现在，车站的长椅上积了薄薄一层。")
        XCTAssertEqual(ONSLine(name: "", text: "It kept\nsnowing.").text, "It kept snowing.")
    }

    func testReadsSavesWithTheirDetails() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: folder) }
        let details = folder.appendingPathComponent(ONSSaveFiles.detailsFolderName)
        try FileManager.default.createDirectory(at: details, withIntermediateDirectories: true)
        try Data([1, 2, 3]).write(to: folder.appendingPathComponent("save1.dat"))
        try Data([1]).write(to: folder.appendingPathComponent("save12.dat"))
        try Data().write(to: folder.appendingPathComponent("envdata"))
        try Data().write(to: folder.appendingPathComponent("saveX.dat"))
        try Data(#"{"name":"","text":"【春人】「走吧。」"}"#.utf8).write(to: details.appendingPathComponent("save1.json"))
        try Data([0xFF]).write(to: details.appendingPathComponent("save1.jpg"))

        let saves = ONSSaveFiles.saves(in: folder)
        XCTAssertEqual(Set(saves.keys), [1, 12])
        XCTAssertEqual(saves[1]?.byteCount, 3)
        XCTAssertEqual(saves[1]?.line.name, "春人")
        XCTAssertNotNil(saves[1]?.thumbnailURL)
        XCTAssertEqual(saves[12]?.line.text, "")
        XCTAssertNil(saves[12]?.thumbnailURL)

        try ONSSaveFiles.delete(slot: 1, in: folder)
        XCTAssertEqual(Set(ONSSaveFiles.saves(in: folder).keys), [12])
        XCTAssertFalse(FileManager.default.fileExists(atPath: details.appendingPathComponent("save1.jpg").path))
    }
}
