import XCTest
@testable import VNCore

final class LibraryTests: XCTestCase {
    var sandbox: URL!
    override func setUpWithError() throws {
        sandbox = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: sandbox, withIntermediateDirectories: true)
    }
    override func tearDownWithError() throws { try FileManager.default.removeItem(at: sandbox) }
    func directory(_ name: String) throws -> URL {
        let result = sandbox.appendingPathComponent(name)
        try FileManager.default.createDirectory(at: result, withIntermediateDirectories: true)
        return result
    }
    func script(in folder: URL) throws { try Data("// sample".utf8).write(to: folder.appendingPathComponent("startup.tjs")) }

    func testNestedFolderImportAndPersistence() throws {
        let input = try directory("Upload/Game"), output = try directory("Library")
        try script(in: input)
        let repository = LibraryRepository(root: output)
        let game = try repository.importFolder(input.deletingLastPathComponent())
        XCTAssertEqual(game.title, "Game")
        XCTAssertTrue(FileManager.default.fileExists(atPath: try repository.directory(for: game).appendingPathComponent("startup.tjs").path))
        try repository.save([game]); XCTAssertEqual(try repository.load(), [game])
        XCTAssertTrue(FileManager.default.fileExists(atPath: input.path))
    }
    func testRejectsDisguisedXP3() throws {
        let input = try directory("Invalid")
        try Data("not an archive".utf8).write(to: input.appendingPathComponent("data.xp3"))
        XCTAssertEqual(try GameScanner.roots(in: input), [])
    }
    func testRecognizesXP3Signature() throws {
        let input = try directory("Valid")
        try Data([0x58,0x50,0x33,0x0D,0x0A,0x20,0x0A,0x1A,0x8B,0x67,0x01]).write(to: input.appendingPathComponent("data.xp3"))
        XCTAssertEqual(try GameScanner.roots(in: input), [input])
    }
    func testAmbiguousImportDoesNotPublishAnything() throws {
        let a = try directory("Upload/A"), b = try directory("Upload/B")
        try script(in: a); try script(in: b)
        let repository = LibraryRepository(root: try directory("Library"))
        XCTAssertThrowsError(try repository.importFolder(a.deletingLastPathComponent()))
        XCTAssertEqual(try repository.load(), [])
    }
    func testTraversalAndSymlinkEscapeRejected() throws {
        let root = try directory("Root"), outside = try directory("Outside")
        XCTAssertThrowsError(try GameScanner.containedURL("../Outside", in: root))
        XCTAssertThrowsError(try GameScanner.containedURL("/etc/passwd", in: root))
        try FileManager.default.createSymbolicLink(at: root.appendingPathComponent("link"), withDestinationURL: outside)
        XCTAssertThrowsError(try GameScanner.containedURL("link/file", in: root))
    }
    func testTwoInstallsHaveIsolatedDirectories() throws {
        let input = try directory("Game"); try script(in: input)
        let repository = LibraryRepository(root: try directory("Library"))
        let a = try repository.importFolder(input), b = try repository.importFolder(input)
        XCTAssertNotEqual(a.id, b.id)
        XCTAssertNotEqual(try repository.directory(for: a), try repository.directory(for: b))
    }
    func testCorruptLibraryIsNotTreatedAsEmpty() throws {
        let root = try directory("Library")
        try Data("broken".utf8).write(to: root.appendingPathComponent("library.json"))
        XCTAssertThrowsError(try LibraryRepository(root: root).load())
    }
}
