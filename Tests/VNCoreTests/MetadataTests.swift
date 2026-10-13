import XCTest
@testable import VNCore

final class MetadataTests: XCTestCase {
    // MARK: Search text

    func testFolderNameNoiseIsRemovedFromSearchText() {
        XCTAssertEqual(MatchFinder.searchText(fromFolderName: "[汉化版] 夏影回廊 Ver1.02"), "夏影回廊")
        XCTAssertEqual(MatchFinder.searchText(fromFolderName: "【某汉化组】サマーポケッツ(v1.2.3)"), "サマーポケッツ")
        XCTAssertEqual(MatchFinder.searchText(fromFolderName: "Summer_Pockets_v1.0.2"), "Summer Pockets")
        XCTAssertEqual(MatchFinder.searchText(fromFolderName: "千恋万花 简体中文版"), "千恋万花")
    }

    func testTitlesThatMerelyContainVersionLettersSurvive() {
        XCTAssertEqual(MatchFinder.searchText(fromFolderName: "Forever 7"), "Forever 7")
        XCTAssertEqual(MatchFinder.searchText(fromFolderName: "[汉化版]"), "[汉化版]")
    }

    // MARK: Candidates

    func testResultsForTheSameWorkArePairedAcrossServices() {
        let vndb = [
            VNDBVisualNovel(id: "v1", title: "Natsukage Kairou", alttitle: "夏影回廊", released: "2019-08-23"),
            VNDBVisualNovel(id: "v2", title: "Natsukage Kairou FD", alttitle: "夏影回廊 FD", released: "2021-03-01")
        ]
        let bangumi = [
            BangumiSubject(id: 30, name: "夏影", date: "2015-01-01"),
            BangumiSubject(id: 10, name: "夏影回廊", nameCN: "夏影回廊", date: "2019-08-23")
        ]

        let candidates = MatchFinder.candidates(query: "夏影回廊", vndb: vndb, bangumi: bangumi)

        XCTAssertEqual(candidates.count, 3)
        XCTAssertEqual(candidates[0].vndb?.id, "v1")
        XCTAssertEqual(candidates[0].bangumi?.id, 10)
        XCTAssertTrue(MatchFinder.isConfident(candidates[0]))
        XCTAssertTrue(candidates.contains { $0.vndb?.id == "v2" && $0.bangumi == nil })
        XCTAssertTrue(candidates.contains { $0.bangumi?.id == 30 && $0.vndb == nil })
    }

    func testDistantReleaseYearsKeepSameNamedWorksApart() {
        let vndb = [VNDBVisualNovel(id: "v1", title: "Kanon", released: "1999-06-04")]
        let bangumi = [BangumiSubject(id: 5, name: "Kanon", date: "2006-10-05")]

        let candidates = MatchFinder.candidates(query: "Kanon", vndb: vndb, bangumi: bangumi)

        XCTAssertEqual(candidates.count, 2)
    }

    func testSimilarityIgnoresCaseWidthAndPunctuation() {
        XCTAssertEqual(MatchFinder.similarity(MatchFinder.key("Summer Pockets!"), MatchFinder.key("ｓｕｍｍｅｒ　ｐｏｃｋｅｔｓ")), 1)
        XCTAssertLessThan(MatchFinder.similarity(MatchFinder.key("Kanon"), MatchFinder.key("Clannad")), 0.3)
    }

    // MARK: Decoding

    func testVNDBResponseDecodes() throws {
        let json = """
        {"results":[{"id":"v17","title":"Ever17 -the out of infinity-","alttitle":"Ever17 -the out of infinity-",
        "titles":[{"lang":"ja","title":"Ever17","latin":null,"main":true},{"lang":"zh-Hans","title":"秋之回忆外传","latin":null,"main":false}],
        "released":"2002-08-29","description":"A [url=/c1]story[/url] set underwater.[spoiler]Twist[/spoiler]",
        "image":{"url":"https://t.vndb.org/cv/1.jpg","sexual":0,"violence":0,"dims":[256,363]},
        "screenshots":[{"url":"https://t.vndb.org/sf/1.jpg","sexual":1.6,"violence":0,"dims":[800,600]},
                       {"url":"https://t.vndb.org/sf/2.jpg","sexual":0,"violence":0,"dims":[800,600]}],
        "developers":[{"name":"KID","original":null}],
        "tags":[{"name":"Spoiler","rating":3,"spoiler":2},{"name":"Mystery","rating":2.8,"spoiler":0},{"name":"Sci-fi","rating":2.9,"spoiler":0}]}],
        "more":false}
        """
        struct Results: Decodable { var results: [VNDBVisualNovel] }
        let novel = try XCTUnwrap(JSONDecoder().decode(Results.self, from: Data(json.utf8)).results.first)

        XCTAssertEqual(novel.plainDescription, "A story set underwater.")
        XCTAssertEqual(novel.topTags(limit: 5), ["Sci-fi", "Mystery"])
        XCTAssertEqual(novel.developerNames, "KID")
        XCTAssertEqual(novel.title(inLanguagePrefix: "zh"), "秋之回忆外传")

        let metadata = LinkedMetadata.merged(vndb: novel, bangumi: nil)
        XCTAssertEqual(metadata.banner(hidingSensitive: true)?.url, "https://t.vndb.org/sf/2.jpg")
        XCTAssertEqual(metadata.banner(hidingSensitive: false)?.url, "https://t.vndb.org/sf/1.jpg")
        XCTAssertEqual(metadata.released, "2002-08-29")
        XCTAssertEqual(metadata.synopsisSource, .vndb)
    }

    func testBangumiSubjectDecodesBothInfoboxShapes() throws {
        let json = """
        {"id":12,"name":"夏影回廊","name_cn":"夏影回廊","date":"2019-08-23","summary":"暑假回到海边小镇。",
        "images":{"large":"http://lain.bgm.tv/pic/cover/l/1.jpg","common":"https://lain.bgm.tv/pic/cover/c/1.jpg","medium":"https://lain.bgm.tv/pic/cover/m/1.jpg"},
        "tags":[{"name":"夏日","count":10},{"name":"纯爱","count":30}],
        "infobox":[{"key":"开发","value":"Lumina Works"},{"key":"别名","value":[{"v":"Natsukage"},{"k":"en","v":"Summer Corridor"}]}],
        "nsfw":false}
        """
        let subject = try JSONDecoder().decode(BangumiSubject.self, from: Data(json.utf8))

        XCTAssertEqual(subject.posterURL, "https://lain.bgm.tv/pic/cover/l/1.jpg")
        XCTAssertEqual(subject.topTags(limit: 1), ["纯爱"])
        XCTAssertEqual(subject.infoboxValue(for: ["开发"]), "Lumina Works")
        XCTAssertEqual(subject.infoboxValue(for: ["别名"]), "Natsukage, Summer Corridor")

        let metadata = LinkedMetadata.merged(vndb: nil, bangumi: subject)
        XCTAssertEqual(metadata.chineseTitle, "夏影回廊")
        XCTAssertEqual(metadata.developer, "Lumina Works")
        XCTAssertEqual(metadata.synopsisSource, .bangumi)
        XCTAssertEqual(metadata.releaseYear, "2019")
    }

    // MARK: List updates

    func testVNDBPatchSetsOneStatusLabelAndScalesTheVote() {
        let entry = ListEntry(status: .finished, rating: 8, comment: "Good")
        let body = VNDBClient.listPatch(for: entry, includeRating: true, includeDates: false)

        XCTAssertEqual(body["labels_set"] as? [Int], [2])
        XCTAssertEqual(body["labels_unset"] as? [Int], [1, 3, 4, 5])
        XCTAssertEqual(body["vote"] as? Int, 80)
        XCTAssertEqual(body["notes"] as? String, "Good")
        XCTAssertNil(body["started"])
    }

    func testVNDBPatchClearsFieldsExplicitly() {
        let body = VNDBClient.listPatch(for: ListEntry(), includeRating: true, includeDates: true)

        XCTAssertNil(body["labels_set"])
        XCTAssertEqual(body["labels_unset"] as? [Int], [1, 2, 3, 4, 5])
        XCTAssertTrue(body["vote"] is NSNull)
        XCTAssertTrue(body["started"] is NSNull)
    }

    func testBangumiCollectionNeedsAStatus() {
        XCTAssertNil(BangumiClient.collectionBody(for: ListEntry(rating: 7), includeRating: true))
        let body = BangumiClient.collectionBody(for: ListEntry(status: .playing), includeRating: false)
        XCTAssertEqual(body?["type"] as? Int, 3)
        XCTAssertNil(body?["rate"])
    }

    func testRatingsAreClampedToTheSharedScale() {
        XCTAssertEqual(ListEntry(rating: 0).rating, 1)
        XCTAssertEqual(ListEntry(rating: 14).rating, 10)
    }

    // MARK: Library

    func testRecordsWithoutLinkFieldsStillDecode() throws {
        let json = """
        {"id":"\(UUID().uuidString)","title":"Old","engine":"kirikiri","folderName":"Old",
        "byteCount":0,"addedAt":0,"playTime":0,"availability":{"ready":{}}}
        """
        let record = try JSONDecoder().decode(GameRecord.self, from: Data(json.utf8))

        XCTAssertNil(record.link)
        XCTAssertTrue(record.listEntry.isEmpty)
    }

    func testScanKeepsLinkAndListEntry() throws {
        let sandbox = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: sandbox) }
        let repository = LibraryRepository(
            documentsRoot: sandbox.appendingPathComponent("Documents"),
            metadataRoot: sandbox.appendingPathComponent("Support")
        )
        try repository.ensureStructure()
        let folder = repository.engineRoot(for: .kirikiri).appendingPathComponent("Game")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try Data("// sample".utf8).write(to: folder.appendingPathComponent("startup.tjs"))
        let game = try XCTUnwrap(repository.scan().active.first)

        try repository.update(game.id) {
            $0.link = GameLink(vndbID: "v1", metadata: LinkedMetadata(originalTitle: "Game"))
            $0.listEntry = ListEntry(status: .playing, rating: 9)
        }
        let rescanned = try XCTUnwrap(repository.scan().active.first)

        XCTAssertEqual(rescanned.link?.vndbID, "v1")
        XCTAssertEqual(rescanned.listEntry.status, .playing)
        XCTAssertEqual(rescanned.listEntry.rating, 9)
    }
}
