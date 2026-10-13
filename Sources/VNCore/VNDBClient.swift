import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// A visual novel as returned by VNDB's Kana API (v2).
public struct VNDBVisualNovel: Decodable, Equatable, Sendable {
    public struct Title: Decodable, Equatable, Sendable {
        public var lang: String
        public var title: String
        public var latin: String?
        public var main: Bool?
    }

    public struct Image: Decodable, Equatable, Sendable {
        public var url: String
        /// 0 safe, 1 suggestive, 2 explicit (averaged over votes).
        public var sexual: Double?
        public var violence: Double?
        public var dims: [Int]?

        /// Anything rated suggestive or worse counts as sensitive.
        public var isSensitive: Bool {
            (sexual ?? 0) >= 0.5 || (violence ?? 0) >= 1
        }

        public var candidate: ArtworkCandidate {
            var aspect: Double?
            if let dims, dims.count == 2, dims[1] > 0 {
                aspect = Double(dims[0]) / Double(dims[1])
            }
            return ArtworkCandidate(url: url, sensitive: isSensitive, aspectRatio: aspect)
        }
    }

    public struct Developer: Decodable, Equatable, Sendable {
        public var name: String
        public var original: String?
    }

    public struct Tag: Decodable, Equatable, Sendable {
        public var name: String?
        public var rating: Double?
        public var spoiler: Int?
    }

    public var id: String
    /// The main title as shown on VNDB, usually romanized.
    public var title: String
    /// The same title in its original script, when that differs.
    public var alttitle: String?
    public var titles: [Title]
    public var released: String?
    public var description: String?
    public var image: Image?
    public var screenshots: [Image]
    public var developers: [Developer]
    public var tags: [Tag]

    private enum CodingKeys: String, CodingKey {
        case id, title, alttitle, titles, released, description, image, screenshots, developers, tags
    }

    public init(
        id: String,
        title: String,
        alttitle: String? = nil,
        titles: [Title] = [],
        released: String? = nil,
        description: String? = nil,
        image: Image? = nil,
        screenshots: [Image] = [],
        developers: [Developer] = [],
        tags: [Tag] = []
    ) {
        self.id = id
        self.title = title
        self.alttitle = alttitle
        self.titles = titles
        self.released = released
        self.description = description
        self.image = image
        self.screenshots = screenshots
        self.developers = developers
        self.tags = tags
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(String.self, forKey: .id)
        title = try container.decode(String.self, forKey: .title)
        alttitle = try container.decodeIfPresent(String.self, forKey: .alttitle)
        titles = (try? container.decodeIfPresent([Title].self, forKey: .titles)) ?? []
        // `released` is a date string, "TBA", or null.
        released = try? container.decodeIfPresent(String.self, forKey: .released)
        description = try container.decodeIfPresent(String.self, forKey: .description)
        image = try? container.decodeIfPresent(Image.self, forKey: .image)
        screenshots = (try? container.decodeIfPresent([Image].self, forKey: .screenshots)) ?? []
        developers = (try? container.decodeIfPresent([Developer].self, forKey: .developers)) ?? []
        tags = (try? container.decodeIfPresent([Tag].self, forKey: .tags)) ?? []
    }

    /// The title in its original script, falling back to the main title.
    public var originalTitle: String {
        alttitle?.nonEmpty ?? title
    }

    /// Every title the VN is known by, for matching against Bangumi.
    public var allTitles: [String] {
        var result = [title]
        if let alttitle { result.append(alttitle) }
        for entry in titles {
            result.append(entry.title)
            if let latin = entry.latin { result.append(latin) }
        }
        return result
    }

    public func title(inLanguagePrefix prefix: String) -> String? {
        titles.first { $0.lang.hasPrefix(prefix) }?.title.nonEmpty
    }

    public var developerNames: String? {
        let names = developers.map { $0.original?.nonEmpty ?? $0.name }
        return names.isEmpty ? nil : names.joined(separator: ", ")
    }

    /// Highest-rated tags, skipping spoilers.
    public func topTags(limit: Int) -> [String] {
        tags.filter { ($0.spoiler ?? 0) == 0 && $0.name != nil }
            .sorted { ($0.rating ?? 0) > ($1.rating ?? 0) }
            .prefix(limit)
            .compactMap(\.name)
    }

    /// The description without VNDB's BBCode-style markup.
    public var plainDescription: String? {
        guard let description else { return nil }
        return Self.stripMarkup(description)
    }

    /// Removes `[url=…]text[/url]`, `[spoiler]…[/spoiler]` and similar tags,
    /// keeping link text and dropping spoiler contents.
    static func stripMarkup(_ text: String) -> String {
        var result = text
        let replacements: [(String, String)] = [
            (#"\[spoiler\][\s\S]*?\[/spoiler\]"#, ""),
            (#"\[url=[^\]]*\]([\s\S]*?)\[/url\]"#, "$1"),
            (#"\[/?(b|i|u|s|raw|quote|code)\]"#, ""),
            (#"\n{3,}"#, "\n\n")
        ]
        for (pattern, template) in replacements {
            guard let expression = try? NSRegularExpression(pattern: pattern, options: [.caseInsensitive]) else { continue }
            result = expression.stringByReplacingMatches(
                in: result,
                range: NSRange(result.startIndex..., in: result),
                withTemplate: template
            )
        }
        return result.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

/// Client for VNDB's Kana API. Searching needs no account; list updates need
/// a token with the `listwrite` permission, created at vndb.org/u/tokens.
public struct VNDBClient: Sendable {
    public static let baseURL = URL(string: "https://api.vndb.org/kana")!
    public static let tokenPageURL = URL(string: "https://vndb.org/u/tokens")!

    static let fields = [
        "id", "title", "alttitle", "titles.lang", "titles.title", "titles.latin", "titles.main",
        "released", "description",
        "image.url", "image.sexual", "image.violence", "image.dims",
        "screenshots.url", "screenshots.sexual", "screenshots.violence", "screenshots.dims",
        "developers.name", "developers.original",
        "tags.name", "tags.rating", "tags.spoiler"
    ].joined(separator: ",")

    private let http: MetadataHTTP
    private let token: String?

    public init(token: String? = nil, session: URLSession = .shared) {
        self.token = token?.nonEmpty
        http = MetadataHTTP(service: .vndb, session: session)
    }

    private var authorization: String? { token.map { "token \($0)" } }

    private struct Results: Decodable {
        var results: [VNDBVisualNovel]
    }

    public func search(_ query: String, limit: Int = 8) async throws -> [VNDBVisualNovel] {
        try await fetch(filters: ["search", "=", query], sort: "searchrank", limit: limit)
    }

    public func visualNovel(id: String) async throws -> VNDBVisualNovel? {
        try await fetch(filters: ["id", "=", id], sort: nil, limit: 1).first
    }

    private func fetch(filters: [Any], sort: String?, limit: Int) async throws -> [VNDBVisualNovel] {
        var body: [String: Any] = ["filters": filters, "fields": Self.fields, "results": limit]
        if let sort { body["sort"] = sort }
        let request = try http.request(
            Self.baseURL.appendingPathComponent("vn"),
            method: "POST",
            authorization: authorization,
            json: body
        )
        return try http.decode(Results.self, from: try await http.send(request)).results
    }

    /// Confirms the token and that it may write the user's list.
    public func account() async throws -> ServiceAccount {
        struct AuthInfo: Decodable {
            var id: String
            var username: String
            var permissions: [String]
        }
        let request = try http.request(
            Self.baseURL.appendingPathComponent("authinfo"),
            authorization: authorization
        )
        let info = try http.decode(AuthInfo.self, from: try await http.send(request))
        guard info.permissions.contains("listwrite") else {
            throw MetadataError.unauthorized(.vndb)
        }
        return ServiceAccount(service: .vndb, username: info.username, userID: info.id)
    }

    /// The user's existing list entry for a VN, or nil when it isn't listed.
    public func listEntry(userID: String, vnID: String) async throws -> ListEntry? {
        struct Results: Decodable {
            struct Item: Decodable {
                struct Label: Decodable { var id: Int }
                var vote: Int?
                var notes: String?
                var started: String?
                var finished: String?
                var labels: [Label]?
            }
            var results: [Item]
        }
        let request = try http.request(
            Self.baseURL.appendingPathComponent("ulist"),
            method: "POST",
            authorization: authorization,
            json: [
                "user": userID,
                "filters": ["id", "=", vnID],
                "fields": "vote,notes,started,finished,labels.id",
                "results": 1
            ] as [String: Any]
        )
        guard let item = try http.decode(Results.self, from: try await http.send(request)).results.first else {
            return nil
        }
        let labels = Set((item.labels ?? []).map(\.id))
        return ListEntry(
            status: PlayStatus.allCases.first { labels.contains($0.vndbLabel) },
            rating: item.vote.map { Int(($0 + 5) / 10) },
            comment: item.notes?.nonEmpty,
            startedAt: ListDay.date(item.started),
            finishedAt: ListDay.date(item.finished)
        )
    }

    /// Writes status, and optionally rating, comment and dates, to the list.
    /// A cleared status removes all five status labels but keeps the entry.
    public func updateList(
        vnID: String,
        entry: ListEntry,
        includeRating: Bool,
        includeDates: Bool
    ) async throws {
        try await http.send(try http.request(
            Self.baseURL.appendingPathComponent("ulist").appendingPathComponent(vnID),
            method: "PATCH",
            authorization: authorization,
            json: Self.listPatch(for: entry, includeRating: includeRating, includeDates: includeDates)
        ))
    }

    static func listPatch(for entry: ListEntry, includeRating: Bool, includeDates: Bool) -> [String: Any] {
        var body: [String: Any] = [:]
        if let status = entry.status {
            body["labels_set"] = [status.vndbLabel]
            body["labels_unset"] = PlayStatus.vndbStatusLabels.filter { $0 != status.vndbLabel }
        } else {
            body["labels_unset"] = PlayStatus.vndbStatusLabels
        }
        // NSNull clears a field; a missing key leaves it unchanged.
        if includeRating {
            body["vote"] = entry.rating.map { $0 * 10 as Any } ?? NSNull()
            body["notes"] = entry.comment?.nonEmpty.map { $0 as Any } ?? NSNull()
        }
        if includeDates {
            body["started"] = entry.startedAt.map { Self.day($0) as Any } ?? NSNull()
            body["finished"] = entry.finishedAt.map { Self.day($0) as Any } ?? NSNull()
        }
        return body
    }

    /// VNDB stores list dates as plain days, in the player's own calendar.
    static func day(_ date: Date) -> String {
        let components = Calendar(identifier: .gregorian).dateComponents(in: .current, from: date)
        return String(format: "%04d-%02d-%02d", components.year ?? 0, components.month ?? 1, components.day ?? 1)
    }
}
