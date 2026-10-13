import Foundation

/// Where a game sits in the player's own list. The cases mirror the five
/// collection states Bangumi and VNDB share, so a status maps to both
/// services without loss.
public enum PlayStatus: String, Codable, CaseIterable, Identifiable, Sendable {
    case wish
    case playing
    case finished
    case onHold = "on-hold"
    case dropped

    public var id: String { rawValue }

    /// Bangumi collection type: 1 wish, 2 done, 3 doing, 4 on hold, 5 dropped.
    public var bangumiType: Int {
        switch self {
        case .wish: return 1
        case .finished: return 2
        case .playing: return 3
        case .onHold: return 4
        case .dropped: return 5
        }
    }

    /// VNDB's built-in list labels: 1 Playing, 2 Finished, 3 Stalled,
    /// 4 Dropped, 5 Wishlist.
    public var vndbLabel: Int {
        switch self {
        case .playing: return 1
        case .finished: return 2
        case .onHold: return 3
        case .dropped: return 4
        case .wish: return 5
        }
    }

    /// Every label a status can set; the others are cleared on each update so
    /// a VN never carries two of them.
    public static let vndbStatusLabels = [1, 2, 3, 4, 5]
}

/// The two metadata services a game can be linked to.
public enum MetadataService: String, Codable, CaseIterable, Identifiable, Sendable {
    case bangumi
    case vndb

    public var id: String { rawValue }

    public var displayName: String {
        switch self {
        case .bangumi: return "Bangumi"
        case .vndb: return "VNDB"
        }
    }
}

/// The player's own entry for a game: status, rating, comment and dates.
///
/// Kept separate from `GameLink` because it exists without a link too: a
/// status set on an unmatched game stays local and syncs once it is linked.
/// Playtime is not here; it stays in `GameRecord` and never leaves the device.
public struct ListEntry: Codable, Equatable, Sendable {
    public var status: PlayStatus?
    /// 1–10, the scale both services use (VNDB stores it ×10).
    public var rating: Int?
    public var comment: String?
    public var startedAt: Date?
    public var finishedAt: Date?
    /// Services with changes not yet written. Cleared per service on a
    /// successful sync, and kept across launches so nothing is lost offline.
    public var pendingSync: [MetadataService]
    public var lastSyncedAt: Date?

    public init(
        status: PlayStatus? = nil,
        rating: Int? = nil,
        comment: String? = nil,
        startedAt: Date? = nil,
        finishedAt: Date? = nil,
        pendingSync: [MetadataService] = [],
        lastSyncedAt: Date? = nil
    ) {
        self.status = status
        self.rating = rating.map(Self.clampRating)
        self.comment = comment
        self.startedAt = startedAt
        self.finishedAt = finishedAt
        self.pendingSync = pendingSync
        self.lastSyncedAt = lastSyncedAt
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        status = try? container.decodeIfPresent(PlayStatus.self, forKey: .status)
        rating = (try? container.decodeIfPresent(Int.self, forKey: .rating)).map(Self.clampRating)
        comment = try container.decodeIfPresent(String.self, forKey: .comment)
        startedAt = try container.decodeIfPresent(Date.self, forKey: .startedAt)
        finishedAt = try container.decodeIfPresent(Date.self, forKey: .finishedAt)
        pendingSync = (try? container.decodeIfPresent([MetadataService].self, forKey: .pendingSync)) ?? []
        lastSyncedAt = try container.decodeIfPresent(Date.self, forKey: .lastSyncedAt)
    }

    public var isEmpty: Bool {
        status == nil && rating == nil && (comment ?? "").isEmpty
            && startedAt == nil && finishedAt == nil
    }

    /// Marks the entry as changed for every service in `services`.
    public mutating func markPending(_ services: [MetadataService]) {
        for service in services where !pendingSync.contains(service) {
            pendingSync.append(service)
        }
    }

    public static func clampRating(_ value: Int) -> Int {
        min(max(value, 1), 10)
    }
}

/// A game's link to Bangumi and/or VNDB, with the details fetched from them.
public struct GameLink: Codable, Equatable, Sendable {
    /// VNDB id including its prefix, such as "v17".
    public var vndbID: String?
    public var bangumiID: Int?
    public var metadata: LinkedMetadata
    /// Downloaded artwork in the library's Covers folder.
    public var posterName: String?
    public var bannerName: String?
    /// Source URLs of the files above, so a refresh only downloads changes.
    public var posterSource: String?
    public var bannerSource: String?
    /// Card art the player picked from the services' images; overrides
    /// the automatic 16:9-then-4:3 choice.
    public var bannerChoice: String?
    /// Cover the player picked for the details page, when they picked
    /// one of the services' covers rather than a screenshot.
    public var posterChoice: String?

    public init(
        vndbID: String? = nil,
        bangumiID: Int? = nil,
        metadata: LinkedMetadata = LinkedMetadata(),
        posterName: String? = nil,
        bannerName: String? = nil,
        posterSource: String? = nil,
        bannerSource: String? = nil,
        bannerChoice: String? = nil,
        posterChoice: String? = nil
    ) {
        self.vndbID = vndbID
        self.bangumiID = bangumiID
        self.metadata = metadata
        self.posterName = posterName
        self.bannerName = bannerName
        self.posterSource = posterSource
        self.bannerSource = bannerSource
        self.bannerChoice = bannerChoice
        self.posterChoice = posterChoice
    }

    public var services: [MetadataService] {
        var result: [MetadataService] = []
        if bangumiID != nil { result.append(.bangumi) }
        if vndbID != nil { result.append(.vndb) }
        return result
    }

    public var bangumiURL: URL? {
        bangumiID.flatMap { URL(string: "https://bgm.tv/subject/\($0)") }
    }

    public var vndbURL: URL? {
        vndbID.flatMap { URL(string: "https://vndb.org/\($0)") }
    }
}

/// An image a service offers, with whether it is marked as sensitive.
public struct ArtworkCandidate: Codable, Equatable, Sendable {
    public var url: String
    public var sensitive: Bool
    /// Width over height, when the service reports dimensions.
    public var aspectRatio: Double?

    public init(url: String, sensitive: Bool, aspectRatio: Double? = nil) {
        self.url = url
        self.sensitive = sensitive
        self.aspectRatio = aspectRatio
    }
}

/// Details merged from both services. Bangumi supplies the Chinese title,
/// synopsis and tags; VNDB supplies developer, release date and screenshots.
/// Each falls back to the other service when its preferred source has none.
public struct LinkedMetadata: Codable, Equatable, Sendable {
    /// Title in the original script, such as Japanese.
    public var originalTitle: String?
    /// VNDB's romanized main title.
    public var romanizedTitle: String?
    /// Bangumi's Chinese title.
    public var chineseTitle: String?
    public var developer: String?
    /// ISO date as the service gives it: "2019-08-23", "2019-08" or "2019".
    public var released: String?
    public var synopsis: String?
    public var synopsisSource: MetadataService?
    public var tags: [String]
    public var posters: [ArtworkCandidate]
    public var screenshots: [ArtworkCandidate]
    public var fetchedAt: Date?
    /// How the details were put together. Details from an older format are
    /// fetched again, since sensitivity flags and poster order changed.
    public var format: Int

    /// Format 2: VNDB's cover leads, and only explicit images count as
    /// sensitive (Bangumi's R18 flag marks the game, not its cover).
    public static let currentFormat = 2

    public var isStale: Bool { format < Self.currentFormat }

    public init(
        originalTitle: String? = nil,
        romanizedTitle: String? = nil,
        chineseTitle: String? = nil,
        developer: String? = nil,
        released: String? = nil,
        synopsis: String? = nil,
        synopsisSource: MetadataService? = nil,
        tags: [String] = [],
        posters: [ArtworkCandidate] = [],
        screenshots: [ArtworkCandidate] = [],
        fetchedAt: Date? = nil,
        format: Int = LinkedMetadata.currentFormat
    ) {
        self.originalTitle = originalTitle
        self.romanizedTitle = romanizedTitle
        self.chineseTitle = chineseTitle
        self.developer = developer
        self.released = released
        self.synopsis = synopsis
        self.synopsisSource = synopsisSource
        self.tags = tags
        self.posters = posters
        self.screenshots = screenshots
        self.fetchedAt = fetchedAt
        self.format = format
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        originalTitle = try container.decodeIfPresent(String.self, forKey: .originalTitle)
        romanizedTitle = try container.decodeIfPresent(String.self, forKey: .romanizedTitle)
        chineseTitle = try container.decodeIfPresent(String.self, forKey: .chineseTitle)
        developer = try container.decodeIfPresent(String.self, forKey: .developer)
        released = try container.decodeIfPresent(String.self, forKey: .released)
        synopsis = try container.decodeIfPresent(String.self, forKey: .synopsis)
        synopsisSource = try? container.decodeIfPresent(MetadataService.self, forKey: .synopsisSource)
        tags = try container.decodeIfPresent([String].self, forKey: .tags) ?? []
        posters = try container.decodeIfPresent([ArtworkCandidate].self, forKey: .posters) ?? []
        screenshots = try container.decodeIfPresent([ArtworkCandidate].self, forKey: .screenshots) ?? []
        fetchedAt = try container.decodeIfPresent(Date.self, forKey: .fetchedAt)
        format = (try? container.decodeIfPresent(Int.self, forKey: .format)) ?? 1
    }

    /// The four-digit year of `released`, if it has one.
    public var releaseYear: String? {
        guard let released, released.count >= 4 else { return nil }
        let year = String(released.prefix(4))
        return Int(year) == nil ? nil : year
    }

    /// The first poster, skipping sensitive ones when asked.
    public func poster(hidingSensitive: Bool) -> ArtworkCandidate? {
        posters.first { !hidingSensitive || !$0.sensitive }
    }

    /// Art for library cards and the continue hero: the first landscape
    /// image, 16:9 before 4:3 before other wide shapes, skipping sensitive
    /// ones when asked. Nil when no image is known to be landscape, so the
    /// cards fall back to the poster.
    public func banner(hidingSensitive: Bool) -> ArtworkCandidate? {
        cardArt(hidingSensitive: hidingSensitive).first { Self.cardRank($0) <= 2 }
    }

    /// Every image either service offers, best card art first: 16:9, then
    /// 4:3, other landscape shapes, unknown sizes, then portrait covers.
    /// Screenshots come before posters at the same rank.
    public func cardArt(hidingSensitive: Bool) -> [ArtworkCandidate] {
        var seen = Set<String>()
        return (screenshots + posters)
            .filter { (!hidingSensitive || !$0.sensitive) && seen.insert($0.url).inserted }
            .enumerated()
            .sorted { lhs, rhs in
                let (a, b) = (Self.cardRank(lhs.element), Self.cardRank(rhs.element))
                return a != b ? a < b : lhs.offset < rhs.offset
            }
            .map(\.element)
    }

    public static func cardRank(_ candidate: ArtworkCandidate) -> Int {
        guard let aspect = candidate.aspectRatio else { return 3 }
        if abs(aspect - 16.0 / 9.0) < 0.1 { return 0 }
        if abs(aspect - 4.0 / 3.0) < 0.08 { return 1 }
        if aspect >= 1.2 { return 2 }
        return 4
    }

    /// Combines details from the two services by the preference above.
    public static func merged(
        vndb: VNDBVisualNovel?,
        bangumi: BangumiSubject?,
        fetchedAt: Date = Date()
    ) -> LinkedMetadata {
        var result = LinkedMetadata(fetchedAt: fetchedAt)
        result.originalTitle = vndb?.originalTitle ?? bangumi?.name.nonEmpty
        result.romanizedTitle = vndb?.title.nonEmpty
        result.chineseTitle = bangumi?.nameCN?.nonEmpty ?? vndb?.title(inLanguagePrefix: "zh")
        result.developer = vndb?.developerNames ?? bangumi?.infoboxValue(for: ["开发", "开发商", "制作", "品牌"])
        result.released = vndb?.released.flatMap(Self.isoDate) ?? bangumi?.date?.nonEmpty
        if let summary = bangumi?.summary?.trimmingCharacters(in: .whitespacesAndNewlines), !summary.isEmpty {
            result.synopsis = summary
            result.synopsisSource = .bangumi
        } else if let description = vndb?.plainDescription, !description.isEmpty {
            result.synopsis = description
            result.synopsisSource = .vndb
        }
        let bangumiTags = bangumi?.topTags(limit: 6) ?? []
        result.tags = bangumiTags.isEmpty ? (vndb?.topTags(limit: 6) ?? []) : bangumiTags
        // VNDB's cover leads, with its own sensitivity rating; Bangumi's
        // package art follows. Bangumi's R18 flag describes the game rather
        // than the cover, which is the same art sold in shops.
        if let image = vndb?.image {
            result.posters.append(image.candidate)
        }
        if let poster = bangumi?.posterURL {
            result.posters.append(ArtworkCandidate(url: poster, sensitive: false))
        }
        result.screenshots = vndb?.screenshots.map(\.candidate) ?? []
        return result
    }

    /// VNDB uses "TBA" and other placeholders for unknown dates.
    private static func isoDate(_ value: String) -> String? {
        guard let first = value.first, first.isNumber else { return nil }
        return value
    }
}

extension String {
    var nonEmpty: String? {
        let trimmed = trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }
}
