import XCTest
@testable import VNCore

final class LibraryTests: XCTestCase {
    private var sandbox: URL!
    private var documents: URL!
    private var metadata: URL!
    private var repository: LibraryRepository!

    override func setUpWithError() throws {
        sandbox = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        documents = sandbox.appendingPathComponent("Documents", isDirectory: true)
        metadata = sandbox.appendingPathComponent("Application Support/Mikage", isDirectory: true)
        try FileManager.default.createDirectory(at: documents, withIntermediateDirectories: true)
        repository = LibraryRepository(documentsRoot: documents, metadataRoot: metadata)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: sandbox)
    }

    private func directory(_ relativePath: String) throws -> URL {
        let result = sandbox.appendingPathComponent(relativePath, isDirectory: true)
        try FileManager.default.createDirectory(at: result, withIntermediateDirectories: true)
        return result
    }

    private func write(_ bytes: [UInt8], named name: String, in folder: URL) throws {
        try Data(bytes).write(to: folder.appendingPathComponent(name))
    }

    private func script(in folder: URL) throws {
        try Data("// sample".utf8).write(to: folder.appendingPathComponent("startup.tjs"))
    }

    private func xp3(named name: String, in folder: URL) throws {
        try write([0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20, 0x0A, 0x1A, 0x8B, 0x67, 0x01], named: name, in: folder)
    }

    private func png(named name: String, in folder: URL) throws {
        let encoded = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
        try XCTUnwrap(Data(base64Encoded: encoded)).write(to: folder.appendingPathComponent(name))
    }

    func testCreatesVisibleEngineDirectoriesAndHiddenMetadata() throws {
        try repository.ensureStructure()
        XCTAssertTrue(FileManager.default.fileExists(atPath: documents.appendingPathComponent("krkr").path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: documents.appendingPathComponent("ons").path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: documents.appendingPathComponent("renpy").path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: documents.appendingPathComponent("artemis").path))
        XCTAssertTrue(repository.libraryURL.path.hasPrefix(metadata.path))
    }

    func testScanUsesDirectChildrenOnlyAndKeepsInvalidKRKRVisible() throws {
        try repository.ensureStructure()
        let wrapper = repository.engineRoot(for: .kirikiri).appendingPathComponent("Wrapper")
        let nested = wrapper.appendingPathComponent("Nested/Game")
        try FileManager.default.createDirectory(at: nested, withIntermediateDirectories: true)
        try script(in: nested)

        let snapshot = try repository.scan()
        XCTAssertEqual(snapshot.active.map(\.folderName), ["Wrapper"])
        XCTAssertEqual(snapshot.active.first?.availability, .invalid("未找到有效的 startup.tjs 或 XP3"))
    }

    func testStartupScriptWinsAndXP3UsesFixedPriority() throws {
        let folder = try directory("Game")
        try xp3(named: "data.xp3", in: folder)
        try xp3(named: "启动游戏.xp3", in: folder)
        XCTAssertEqual(try GameScanner.launchTarget(in: folder, engine: .kirikiri)?.relativePath, "启动游戏.xp3")

        try script(in: folder)
        let target = try GameScanner.launchTarget(in: folder, engine: .kirikiri)
        XCTAssertEqual(target?.kind, .directory)
        XCTAssertEqual(target?.relativePath, ".")
    }

    func testUnknownXP3FallsBackToNaturalOrderAndRejectsDisguisedArchive() throws {
        let folder = try directory("Game")
        try xp3(named: "part10.xp3", in: folder)
        try xp3(named: "part2.xp3", in: folder)
        try Data("invalid".utf8).write(to: folder.appendingPathComponent("启动游戏.xp3"))
        XCTAssertEqual(try GameScanner.launchTarget(in: folder, engine: .kirikiri)?.relativePath, "part2.xp3")
    }

    func testCoverPriorityAndRootOnlyScanning() throws {
        let folder = try directory("Example")
        let nested = folder.appendingPathComponent("images", isDirectory: true)
        try FileManager.default.createDirectory(at: nested, withIntermediateDirectories: true)
        try png(named: "Example.png", in: folder)
        try png(named: "cover.png", in: folder)
        try png(named: "folder.png", in: nested)
        XCTAssertEqual(try GameScanner.detectedCover(in: folder), "cover.png")
    }

    func testScanPreservesUUIDAndHistoryThenRestoresMissingGame() throws {
        try repository.ensureStructure()
        let gameFolder = repository.engineRoot(for: .kirikiri).appendingPathComponent("Example")
        try FileManager.default.createDirectory(at: gameFolder, withIntermediateDirectories: true)
        try script(in: gameFolder)

        var first = try repository.scan().active[0]
        first.playTime = 42
        first.lastPlayedAt = Date(timeIntervalSince1970: 100)
        try repository.save([first])

        try FileManager.default.removeItem(at: gameFolder)
        let missing = try repository.scan()
        XCTAssertTrue(missing.active.isEmpty)
        XCTAssertEqual(missing.missing.first?.id, first.id)

        try FileManager.default.createDirectory(at: gameFolder, withIntermediateDirectories: true)
        try script(in: gameFolder)
        let restored = try repository.scan().active[0]
        XCTAssertEqual(restored.id, first.id)
        XCTAssertEqual(restored.playTime, 42)
    }

    func testImportPreservesFolderNameAndRejectsNormalizedCollision() throws {
        let source = try directory("Upload/Example")
        try script(in: source)
        try repository.importFolder(source, engine: .kirikiri)
        let imported = repository.engineRoot(for: .kirikiri).appendingPathComponent("Example")
        XCTAssertTrue(FileManager.default.fileExists(atPath: imported.appendingPathComponent("startup.tjs").path))
        XCTAssertThrowsError(try repository.importFolder(source, engine: .kirikiri)) {
            XCTAssertEqual($0 as? LibraryError, .duplicateName("Example"))
        }
        let differentlyCased = try directory("Another/eXaMpLe")
        try script(in: differentlyCased)
        XCTAssertThrowsError(try repository.importFolder(differentlyCased, engine: .kirikiri)) {
            XCTAssertEqual($0 as? LibraryError, .duplicateName("eXaMpLe"))
        }
    }

    func testEngineDetectionReturnsAllMatches() throws {
        let source = try directory("Mixed")
        try script(in: source)
        try Data("ons".utf8).write(to: source.appendingPathComponent("0.txt"))
        XCTAssertEqual(try GameScanner.detectEngines(in: source), [.kirikiri, .onscripter])
    }

    func testUnsupportedEngineIsVisibleButCannotLaunch() throws {
        try repository.ensureStructure()
        let ons = repository.engineRoot(for: .onscripter).appendingPathComponent("ONS Game")
        try FileManager.default.createDirectory(at: ons, withIntermediateDirectories: true)
        try Data("ons".utf8).write(to: ons.appendingPathComponent("0.txt"))
        let game = try repository.scan().active[0]
        XCTAssertEqual(game.engine, .onscripter)
        XCTAssertEqual(game.availability, .unsupported)
        XCTAssertFalse(game.availability.canLaunch)
    }

    func testOldUUIDLibraryIsIgnoredAndUntouched() throws {
        let old = documents.appendingPathComponent("Mikage/Games/old-uuid", isDirectory: true)
        try FileManager.default.createDirectory(at: old, withIntermediateDirectories: true)
        try script(in: old)
        XCTAssertTrue(try repository.scan().active.isEmpty)
        XCTAssertTrue(FileManager.default.fileExists(atPath: old.path))
    }

    func testForgetMissingRecordDoesNotDeleteExternalFolder() throws {
        let source = try directory("External/Game")
        try script(in: source)
        var record = GameRecord(title: "Game", engine: .kirikiri, folderName: "Game", availability: .missing)
        record.playTime = 10
        try repository.save([record])
        try repository.forget(record)
        XCTAssertTrue(try repository.load().isEmpty)
        XCTAssertTrue(FileManager.default.fileExists(atPath: source.path))
    }

    func testRelinkCopiesExternalFolderAndPreservesHiddenIdentity() throws {
        let source = try directory("External/Renamed Game")
        try script(in: source)
        var record = GameRecord(
            title: "Old Game",
            engine: .kirikiri,
            folderName: "Old Game",
            availability: .missing
        )
        record.playTime = 25
        try repository.save([record])

        try repository.relink(record, to: source)
        let linked = try repository.scan().active[0]
        XCTAssertEqual(linked.id, record.id)
        XCTAssertEqual(linked.folderName, "Renamed Game")
        XCTAssertEqual(linked.playTime, 25)
        XCTAssertTrue(FileManager.default.fileExists(
            atPath: repository.engineRoot(for: .kirikiri)
                .appendingPathComponent("Renamed Game/startup.tjs").path
        ))
    }

    func testTraversalAndSymlinkEscapeRejected() throws {
        let root = try directory("Root")
        let outside = try directory("Outside")
        XCTAssertThrowsError(try GameScanner.containedURL("../Outside", in: root))
        XCTAssertThrowsError(try GameScanner.containedURL("/etc/passwd", in: root))
        try FileManager.default.createSymbolicLink(
            at: root.appendingPathComponent("link"),
            withDestinationURL: outside
        )
        XCTAssertThrowsError(try GameScanner.containedURL("link/file", in: root))
    }

    func testCorruptV2LibraryIsNotTreatedAsEmpty() throws {
        try repository.ensureStructure()
        try Data("broken".utf8).write(to: repository.libraryURL)
        XCTAssertThrowsError(try repository.load())
    }

    func testScanReusesRecordedSizeUntilTheFolderChanges() throws {
        try repository.ensureStructure()
        let folder = repository.engineRoot(for: .kirikiri).appendingPathComponent("Example")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try script(in: folder)

        let first = try repository.scan().active[0]
        XCTAssertGreaterThan(first.byteCount, 0)
        XCTAssertNotNil(first.sizeMeasuredAt)

        // Rescanning an untouched folder keeps the recorded size and stamp.
        let unchanged = try repository.scan().active[0]
        XCTAssertEqual(unchanged.byteCount, first.byteCount)
        XCTAssertEqual(unchanged.sizeMeasuredAt, first.sizeMeasuredAt)

        // A new file in the game folder bumps its modification date, so the
        // size must be measured again.
        try Data(repeating: 0x41, count: 4096).write(to: folder.appendingPathComponent("extra.dat"))
        let grown = try repository.scan().active[0]
        XCTAssertEqual(grown.byteCount, first.byteCount + 4096)
        XCTAssertNotEqual(grown.sizeMeasuredAt, first.sizeMeasuredAt)
    }

    func testSetCustomCoverUpdatesOneRecordWithoutRescanning() throws {
        try repository.ensureStructure()
        let folder = repository.engineRoot(for: .kirikiri).appendingPathComponent("Example")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try script(in: folder)
        let game = try repository.scan().active[0]

        // A stale record in the file would be rewritten by a rescan; it must
        // survive, proving the cover write does not scan.
        var missing = GameRecord(title: "Gone", engine: .kirikiri, folderName: "Gone")
        missing.availability = .missing
        try repository.save(try repository.load() + [missing])

        let updated = try repository.setCustomCover(for: game.id, jpegData: Data("jpg".utf8))

        XCTAssertEqual(updated.id, game.id)
        XCTAssertEqual(updated.customCoverName, "\(game.id.uuidString).jpg")
        let stored = try repository.load()
        XCTAssertEqual(stored.count, 2)
        XCTAssertTrue(stored.contains { $0.folderName == "Gone" })
        XCTAssertNotNil(repository.coverURL(for: updated))
    }

    func testSetCustomCoverRejectsAnUnknownRecord() throws {
        try repository.ensureStructure()
        XCTAssertThrowsError(
            try repository.setCustomCover(for: UUID(), jpegData: Data("jpg".utf8))
        ) {
            XCTAssertEqual($0 as? LibraryError, .recordNotFound)
        }
    }

    func testMutateRereadsSoConcurrentScanCannotLoseHistory() throws {
        try repository.ensureStructure()
        let gameFolder = repository.engineRoot(for: .kirikiri).appendingPathComponent("Example")
        try FileManager.default.createDirectory(at: gameFolder, withIntermediateDirectories: true)
        try script(in: gameFolder)
        let first = try repository.scan().active[0]

        // A second game appears and is picked up by a scan that overlaps the
        // history write. Rebuilding from a stale snapshot would drop one of the
        // two changes; both must survive.
        let second = repository.engineRoot(for: .kirikiri).appendingPathComponent("Later")
        try FileManager.default.createDirectory(at: second, withIntermediateDirectories: true)
        try script(in: second)

        let scanner = LibraryRepository(documentsRoot: documents, metadataRoot: metadata)
        let finished = expectation(description: "scan")
        DispatchQueue.global().async {
            _ = try? scanner.scan()
            finished.fulfill()
        }
        try repository.mutate { records in
            guard let index = records.firstIndex(where: { $0.id == first.id }) else { return }
            records[index].playTime += 30
            records[index].launchCount += 1
        }
        wait(for: [finished], timeout: 5)

        let reloaded = try repository.load()
        let updated = try XCTUnwrap(reloaded.first { $0.id == first.id })
        XCTAssertEqual(updated.playTime, 30)
        XCTAssertEqual(updated.launchCount, 1)
        XCTAssertTrue(reloaded.contains { $0.folderName == "Later" })
    }

    func testHistoryFieldsSurviveRescanAndLegacyLibraryDecodes() throws {
        try repository.ensureStructure()
        let gameFolder = repository.engineRoot(for: .kirikiri).appendingPathComponent("Example")
        try FileManager.default.createDirectory(at: gameFolder, withIntermediateDirectories: true)
        try script(in: gameFolder)
        var record = try repository.scan().active[0]
        record.playTime = 90
        record.launchCount = 3
        try repository.save([record])
        let rescanned = try repository.scan().active[0]
        XCTAssertEqual(rescanned.playTime, 90)
        XCTAssertEqual(rescanned.launchCount, 3)

        // A library written before launchCount existed must still load.
        var document = try XCTUnwrap(
            JSONSerialization.jsonObject(with: Data(contentsOf: repository.libraryURL))
                as? [String: Any]
        )
        var games = try XCTUnwrap(document["games"] as? [[String: Any]])
        games[0].removeValue(forKey: "launchCount")
        document["games"] = games
        try JSONSerialization.data(withJSONObject: document).write(to: repository.libraryURL)
        XCTAssertEqual(try repository.load().first?.launchCount, 0)
        XCTAssertEqual(try repository.load().first?.playTime, 90)
    }
}
