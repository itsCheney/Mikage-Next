import XCTest
import UIKit
@testable import Mikage
@testable import VNCore

/// Records calls instead of driving the real runtime. `start` never returns a
/// window, so these tests cover AppModel's routing and history bookkeeping, not
/// the engine lifecycle itself.
@MainActor
final class FakeKRKRSession: KRKRSession {
    var state: KRKRSessionState = .idle
    var onFinished: ((Result<Void, Error>) -> Void)?
    var onWarning: ((Error) -> Void)?
    var onReturningToLibrary: (() -> Void)?
    var onMarkFinished: (() -> Void)?

    private(set) var startedConfigurations: [KRKRLaunchConfiguration] = []
    private(set) var stopCount = 0
    private(set) var foregroundChanges: [Bool] = []

    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) async throws {
        startedConfigurations.append(configuration)
        state = .running
    }

    func requestStop() { stopCount += 1; state = .idle }
    func setForeground(_ foreground: Bool) { foregroundChanges.append(foreground) }
    func showMenuOverlay() {}
    func hideMenuOverlay() {}
    func snapshot() -> UIImage? { nil }
}

@MainActor
final class AppModelTests: XCTestCase {
    private var sandbox: URL!
    private var repository: LibraryRepository!
    private var defaults: UserDefaults!
    private var defaultsSuite: String!
    private var session: FakeKRKRSession!

    override func setUpWithError() throws {
        sandbox = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let documents = sandbox.appendingPathComponent("Documents", isDirectory: true)
        try FileManager.default.createDirectory(at: documents, withIntermediateDirectories: true)
        repository = LibraryRepository(
            documentsRoot: documents,
            metadataRoot: sandbox.appendingPathComponent("Support", isDirectory: true)
        )
        defaultsSuite = "moe.cheney233.mikage.tests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: defaultsSuite)
        session = FakeKRKRSession()
    }

    override func tearDownWithError() throws {
        defaults.removePersistentDomain(forName: defaultsSuite)
        try? FileManager.default.removeItem(at: sandbox)
    }

    private func makeModel() -> AppModel {
        AppModel(
            repository: repository,
            session: session,
            defaults: defaults,
            tokens: MemoryTokenStore(),
            configureDiagnostics: false
        )
    }

    private func addGame(named name: String, engine: EngineID = .kirikiri) throws {
        try repository.ensureStructure()
        let folder = repository.engineRoot(for: engine).appendingPathComponent(name)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try Data("// sample".utf8).write(to: folder.appendingPathComponent("startup.tjs"))
    }

    func testRecordPlaybackAccumulatesTimeAndLaunchCount() throws {
        try addGame(named: "Example")
        let model = makeModel()
        _ = try repository.scan()
        let game = try XCTUnwrap(repository.load().first)

        model.recordPlayback(of: game, duration: 30)
        model.recordPlayback(of: game, duration: 45)

        let stored = try XCTUnwrap(repository.load().first)
        XCTAssertEqual(stored.playTime, 75)
        XCTAssertEqual(stored.launchCount, 2)
        XCTAssertNotNil(stored.lastPlayedAt)
        XCTAssertNil(model.alert)
    }

    /// The write must not rebuild the file from this actor's stale list.
    func testRecordPlaybackKeepsGamesAddedByAConcurrentScan() throws {
        try addGame(named: "Example")
        let model = makeModel()
        _ = try repository.scan()
        let game = try XCTUnwrap(repository.load().first)

        try addGame(named: "Added Later")
        _ = try repository.scan()

        model.recordPlayback(of: game, duration: 10)

        let stored = try repository.load()
        XCTAssertEqual(stored.count, 2)
        XCTAssertTrue(stored.contains { $0.folderName == "Added Later" })
        XCTAssertEqual(stored.first { $0.id == game.id }?.playTime, 10)
    }

    func testRecordPlaybackReportsAMissingRecordInsteadOfFailingSilently() throws {
        let model = makeModel()
        let absent = GameRecord(title: "Gone", engine: .kirikiri, folderName: "Gone")
        model.recordPlayback(of: absent, duration: 5)
        XCTAssertNotNil(model.alert)
    }

    func testNegativeDurationCannotReduceStoredPlayTime() throws {
        try addGame(named: "Example")
        let model = makeModel()
        _ = try repository.scan()
        let game = try XCTUnwrap(repository.load().first)

        model.recordPlayback(of: game, duration: 60)
        model.recordPlayback(of: game, duration: -600)

        XCTAssertEqual(try repository.load().first?.playTime, 60)
        XCTAssertEqual(try repository.load().first?.launchCount, 2)
    }

    func testUnsupportedEngineIsRefusedBeforeReachingTheRuntime() throws {
        try addGame(named: "ONS Game", engine: .onscripter)
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        model.launch(game)

        XCTAssertNil(model.player)
        XCTAssertNotNil(model.alert)
        XCTAssertThrowsError(try model.launchConfiguration(for: game))
    }

    func testLaunchConfigurationCarriesTheStoredRendererRawValue() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        model.settings.renderer = .softwareMetal
        let game = try XCTUnwrap(model.games.first)

        let configuration = try model.launchConfiguration(for: game)

        XCTAssertEqual(configuration.renderer, .softwareMetal)
        XCTAssertEqual(configuration.renderer.rawValue, "software-metal")
        XCTAssertTrue(configuration.respectSilentMode)
        XCTAssertFalse(configuration.experimentalEmote)
        XCTAssertEqual(configuration.gameTitle, "Example")
    }

    func testPatchVideoSkipListIsOffByDefaultAndOptIn() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        XCTAssertFalse(model.settings.skipPatchVideos)
        XCTAssertNil(try model.launchConfiguration(for: game).skippedMovies)

        model.settings.skipPatchVideos = true
        let list = try XCTUnwrap(try model.launchConfiguration(for: game).skippedMovies)
        XCTAssertTrue(list.split(separator: "\n").contains("signature.wmv"))
    }

    func testExperimentalEmoteIsCapturedForEachLaunch() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        XCTAssertFalse(model.settings.experimentalEmote)
        let first = try model.launchConfiguration(for: game)
        XCTAssertFalse(first.experimentalEmote)
        model.settings.experimentalEmote = true
        let second = try model.launchConfiguration(for: game)
        XCTAssertTrue(second.experimentalEmote)
        XCTAssertFalse(first.experimentalEmote)
        model.settings.experimentalEmote = false
        XCTAssertFalse(try model.launchConfiguration(for: game).experimentalEmote)
    }

    func testSettingsPersistRawValuesAndReloadAcrossInstances() throws {
        let model = makeModel()
        model.settings.renderer = .softwareMetal
        model.settings.respectSilentMode = false
        model.settings.sort = .title
        model.settings.experimentalEmote = true

        let data = try XCTUnwrap(defaults.data(forKey: "settings.v1"))
        let json = try XCTUnwrap(
            JSONSerialization.jsonObject(with: data) as? [String: Any]
        )
        XCTAssertEqual(json["renderer"] as? String, "software-metal")
        XCTAssertEqual(json["respectSilentMode"] as? Bool, false)
        XCTAssertEqual(json["sort"] as? String, "title")
        XCTAssertEqual(json["experimentalEmote"] as? Bool, true)

        let reloaded = makeModel()
        XCTAssertEqual(reloaded.settings.renderer, .softwareMetal)
        XCTAssertFalse(reloaded.settings.respectSilentMode)
        XCTAssertEqual(reloaded.settings.sort, .title)
        XCTAssertTrue(reloaded.settings.experimentalEmote)
    }

    func testRemovedEmotePerformanceOptionsStillDecode() throws {
        // The Emote performance experiments were removed. Settings saved while
        // they existed must still load, keeping every other preference.
        let saved = try JSONDecoder().decode(PlayerSettings.self, from: Data("""
            {"experimentalEmote":true,"emoteNodeCache":true,"emoteCaptureCache":true,"emoteLocalUpdate":true,\
            "emoteRegionCopy":true,"emoteAsyncAlpha":true,"emoteExperimentalBounds":true,"emoteLocalPoseCache":true}
            """.utf8))
        XCTAssertTrue(saved.experimentalEmote)
        let reencoded = String(decoding: try JSONEncoder().encode(saved), as: UTF8.self)
        XCTAssertFalse(reencoded.contains("emoteNodeCache"))
        XCTAssertFalse(reencoded.contains("emoteAsyncAlpha"))
    }

    func testLegacyDisplayTextAndUnknownValuesMigrate() throws {
        let legacy: [String: Any] = [
            "renderer": "OpenGL ES", "appearance": "浅色",
            "sort": "名称", "background": "纯黑",
            "floatingButton": false, "idleOpacity": 0.5,
            "threeFingerMenu": false, "performance": true, "listLayout": true
        ]
        defaults.set(try JSONSerialization.data(withJSONObject: legacy), forKey: "settings.v1")

        let model = makeModel()

        // The OpenGL option no longer exists, so it falls back to the default.
        XCTAssertEqual(model.settings.renderer, .metal)
        XCTAssertEqual(model.settings.appearance, .light)
        XCTAssertEqual(model.settings.sort, .title)
        XCTAssertEqual(model.settings.background, .black)
        // Unrelated fields must survive the migration.
        XCTAssertEqual(model.settings.idleOpacity, 0.5)
        XCTAssertTrue(model.settings.performance)
        XCTAssertFalse(model.settings.floatingButton)
        XCTAssertFalse(model.settings.experimentalEmote)
    }

    func testPartialSettingsFileKeepsDefaultsForAbsentKeys() throws {
        defaults.set(
            try JSONSerialization.data(withJSONObject: ["performance": true]),
            forKey: "settings.v1"
        )
        let model = makeModel()
        XCTAssertTrue(model.settings.performance)
        XCTAssertEqual(model.settings.renderer, .metal)
        XCTAssertEqual(model.settings.idleOpacity, PlayerSettings().idleOpacity)
        XCTAssertTrue(model.settings.respectSilentMode)
        XCTAssertFalse(model.settings.experimentalEmote)
    }

    func testInvalidExperimentalEmotePreferenceKeepsOtherSettings() throws {
        defaults.set(
            try JSONSerialization.data(withJSONObject: ["experimentalEmote": "unknown", "performance": true]),
            forKey: "settings.v1"
        )
        let model = makeModel()
        XCTAssertFalse(model.settings.experimentalEmote)
        XCTAssertTrue(model.settings.performance)
    }

    func testRefreshRecoversFromAnUnreadableLibraryOnceRepaired() throws {
        try addGame(named: "Example")
        try repository.ensureStructure()
        try Data("broken".utf8).write(to: repository.libraryURL)

        let model = makeModel()
        XCTAssertNotNil(model.alert)

        // Still unreadable: refresh reports it and changes nothing.
        settle { await model.refreshLibrary() }
        XCTAssertTrue(model.games.isEmpty)

        try FileManager.default.removeItem(at: repository.libraryURL)
        model.alert = nil
        settle { await model.refreshLibrary() }

        XCTAssertNil(model.alert)
        XCTAssertEqual(model.games.map(\.folderName), ["Example"])
    }

    func testLaunchRoutesASupportedGameToThePlayerWithoutStartingTheRuntime() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        model.launch(game)

        // PlayerView owns the start call; AppModel only routes.
        XCTAssertEqual(model.player?.id, game.id)
        XCTAssertNil(model.alert)
        XCTAssertTrue(session.startedConfigurations.isEmpty)
    }

    func testUpdateCoverStoresTheImageAndKeepsTheRecordInPlace() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        let image = UIGraphicsImageRenderer(size: CGSize(width: 8, height: 8)).image { context in
            UIColor.systemTeal.setFill()
            context.fill(CGRect(x: 0, y: 0, width: 8, height: 8))
        }
        settle { await model.updateCover(game, image: image) }

        XCTAssertNil(model.alert)
        XCTAssertEqual(model.games.count, 1)
        let updated = try XCTUnwrap(model.games.first)
        XCTAssertEqual(updated.id, game.id)
        XCTAssertEqual(updated.customCoverName, "\(game.id.uuidString).jpg")
        XCTAssertNotNil(model.coverURL(updated))
    }

    func testSortPreferenceOrdersTheFilteredList() throws {
        try addGame(named: "Beta")
        try addGame(named: "Alpha")
        let model = makeModel()
        settle { await model.refreshLibrary() }

        model.settings.sort = .title
        XCTAssertEqual(model.filteredGames.map(\.title), ["Alpha", "Beta"])

        model.query = "alp"
        XCTAssertEqual(model.filteredGames.map(\.title), ["Alpha"])
    }

    // MARK: Bangumi and VNDB

    func testASessionMovesAGameWithoutStatusToPlayingAndOffersUndo() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        model.finishedPlaying(game, duration: 600, markFinished: false)

        let playing = try XCTUnwrap(model.record(game.id))
        XCTAssertEqual(playing.listEntry.status, .playing)
        XCTAssertNotNil(playing.listEntry.startedAt)
        XCTAssertEqual(playing.launchCount, 1)
        // Not linked, so nothing waits to be sent.
        XCTAssertEqual(playing.listEntry.pendingSync, [])
        XCTAssertEqual(model.toast?.gameID, game.id)

        model.undoToast()

        XCTAssertNil(model.toast)
        XCTAssertNil(model.record(game.id)?.listEntry.status)
        XCTAssertNil(model.record(game.id)?.listEntry.startedAt)
        XCTAssertNil(try repository.load().first?.listEntry.status)
    }

    func testShortSessionsAndFinishedGamesKeepTheirStatus() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        model.finishedPlaying(game, duration: 20, markFinished: false)
        XCTAssertNil(model.record(game.id)?.listEntry.status)
        XCTAssertNil(model.toast)

        model.setStatus(.finished, for: game)
        model.finishedPlaying(game, duration: 600, markFinished: false)
        XCTAssertEqual(model.record(game.id)?.listEntry.status, .finished)
        XCTAssertNil(model.toast)
    }

    func testAutomaticStatusCanBeTurnedOff() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)

        model.settings.autoStatus = false
        model.finishedPlaying(game, duration: 600, markFinished: false)

        XCTAssertNil(model.record(game.id)?.listEntry.status)
    }

    func testChangesToALinkedGameWaitForAConnectedAccount() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)
        model.updateRecord(game.id) { $0.link = GameLink(vndbID: "v1") }

        model.saveRating(8, comment: " ", finished: true, for: game)

        let entry = try XCTUnwrap(model.record(game.id)?.listEntry)
        XCTAssertEqual(entry.status, .finished)
        XCTAssertEqual(entry.rating, 8)
        XCTAssertNil(entry.comment)
        XCTAssertNotNil(entry.finishedAt)
        XCTAssertEqual(entry.pendingSync, [.vndb])
        // No VNDB account yet, so it is not counted as waiting.
        XCTAssertEqual(model.pendingSyncCount, 0)
    }

    func testLinkedGamesShowTheChosenTitleAndAreSearchableByIt() throws {
        try addGame(named: "Example")
        let model = makeModel()
        settle { await model.refreshLibrary() }
        let game = try XCTUnwrap(model.games.first)
        model.updateRecord(game.id) {
            $0.link = GameLink(metadata: LinkedMetadata(
                originalTitle: "なつかげ回廊",
                romanizedTitle: "Natsukage Kairou",
                chineseTitle: "夏影回廊"
            ))
        }
        let linked = try XCTUnwrap(model.record(game.id))

        model.settings.titleDisplay = .romanized
        XCTAssertEqual(model.displayTitle(linked), "Natsukage Kairou")
        model.settings.titleDisplay = .chinese
        XCTAssertEqual(model.displayTitle(linked), "夏影回廊")
        model.settings.titleDisplay = .folder
        XCTAssertEqual(model.displayTitle(linked), "Example")

        model.query = "natsukage"
        XCTAssertEqual(model.filteredGames.map(\.id), [game.id])
    }

    func testNewSettingsDefaultWhenAbsent() throws {
        defaults.set(Data(#"{"renderer":"metal"}"#.utf8), forKey: "settings.v1")
        let model = makeModel()

        XCTAssertTrue(model.settings.autoStatus)
        XCTAssertTrue(model.settings.hideSensitiveImages)
        XCTAssertEqual(model.settings.titleDisplay, .automatic)
        XCTAssertEqual(model.settings.bannerArt, .screenshot)
    }

    /// Runs a MainActor async call to completion from a synchronous test.
    private func settle(_ operation: @escaping @MainActor () async -> Void) {
        let finished = expectation(description: "async")
        Task { @MainActor in
            await operation()
            finished.fulfill()
        }
        wait(for: [finished], timeout: 10)
    }
}
