import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// A subject as returned by Bangumi's API v0.
public struct BangumiSubject: Decodable, Equatable, Sendable {
    public struct Images: Decodable, Equatable, Sendable {
        public var large: String?
        public var common: String?
        public var medium: String?
    }

    public struct Tag: Decodable, Equatable, Sendable {
        public var name: String
        public var count: Int?
    }

    /// One infobox row. Values are either a string or a list of `{v}` objects.
    public struct InfoboxItem: Decodable, Equatable, Sendable {
        public var key: String
        public var values: [String]

        private enum CodingKeys: String, CodingKey { case key, value }
        private struct Entry: Decodable { var v: String? }

        public init(key: String, values: [String]) {
            self.key = key
            self.values = values
        }

        public init(from decoder: Decoder) throws {
            let container = try decoder.container(keyedBy: CodingKeys.self)
            key = try container.decode(String.self, forKey: .key)
            if let text = try? container.decode(String.self, forKey: .value) {
                values = [text]
            } else if let entries = try? container.decode([Entry].self, forKey: .value) {
                values = entries.compactMap(\.v)
            } else {
                values = []
            }
        }
    }

    public var id: Int
    public var name: String
    public var nameCN: String?
    public var date: String?
    public var summary: String?
    public var images: Images?
    /// The search endpoint sometimes returns a single image URL instead.
    public var image: String?
    public var tags: [Tag]
    public var infobox: [InfoboxItem]
    public var nsfw: Bool?

    private enum CodingKeys: String, CodingKey {
        case id, name, date, summary, images, image, tags, infobox, nsfw
        case nameCN = "name_cn"
    }

    public init(
        id: Int,
        name: String,
        nameCN: String? = nil,
        date: String? = nil,
        summary: String? = nil,
        images: Images? = nil,
        image: String? = nil,
        tags: [Tag] = [],
        infobox: [InfoboxItem] = [],
        nsfw: Bool? = nil
    ) {
        self.id = id
        self.name = name
        self.nameCN = nameCN
        self.date = date
        self.summary = summary
        self.images = images
        self.image = image
        self.tags = tags
        self.infobox = infobox
        self.nsfw = nsfw
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(Int.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        nameCN = try container.decodeIfPresent(String.self, forKey: .nameCN)
        date = try? container.decodeIfPresent(String.self, forKey: .date)
        summary = try container.decodeIfPresent(String.self, forKey: .summary)
        images = try? container.decodeIfPresent(Images.self, forKey: .images)
        image = try? container.decodeIfPresent(String.self, forKey: .image)
        tags = (try? container.decodeIfPresent([Tag].self, forKey: .tags)) ?? []
        infobox = (try? container.decodeIfPresent([InfoboxItem].self, forKey: .infobox)) ?? []
        nsfw = try? container.decodeIfPresent(Bool.self, forKey: .nsfw)
    }

    /// The package cover at the largest size offered.
    public var posterURL: String? {
        Self.https(images?.large?.nonEmpty ?? images?.common?.nonEmpty ?? image?.nonEmpty)
    }

    /// A smaller cover for lists such as the match sheet.
    public var thumbnailURL: String? {
        Self.https(images?.medium?.nonEmpty ?? images?.common?.nonEmpty ?? image?.nonEmpty)
    }

    public var allTitles: [String] {
        [name, nameCN].compactMap { $0?.nonEmpty }
    }

    public func topTags(limit: Int) -> [String] {
        tags.sorted { ($0.count ?? 0) > ($1.count ?? 0) }
            .prefix(limit)
            .map(\.name)
    }

    /// The first non-empty infobox value under any of `keys`.
    public func infoboxValue(for keys: [String]) -> String? {
        for key in keys {
            if let item = infobox.first(where: { $0.key == key }) {
                let joined = item.values.compactMap { $0.nonEmpty }.joined(separator: ", ")
                if !joined.isEmpty { return joined }
            }
        }
        return nil
    }

    /// Older records still hand out plain-HTTP image URLs.
    private static func https(_ url: String?) -> String? {
        guard let url else { return nil }
        return url.hasPrefix("http://") ? "https://" + url.dropFirst("http://".count) : url
    }
}

/// Client for Bangumi's API v0. Searching works without an account, but
/// R18 subjects only appear for a signed-in user, and collections need a
/// personal access token from next.bgm.tv/demo/access-token.
public struct BangumiClient: Sendable {
    public static let baseURL = URL(string: "https://api.bgm.tv")!
    public static let tokenPageURL = URL(string: "https://next.bgm.tv/demo/access-token")!
    /// Subject type 4 is games, which includes visual novels.
    static let gameSubjectType = 4

    private let http: MetadataHTTP
    private let token: String?

    public init(token: String? = nil, session: URLSession = .shared) {
        self.token = token?.nonEmpty
        http = MetadataHTTP(service: .bangumi, session: session)
    }

    private var authorization: String? { token.map { "Bearer \($0)" } }

    private struct SearchResults: Decodable {
        var data: [BangumiSubject]
    }

    public func search(_ query: String, limit: Int = 8) async throws -> [BangumiSubject] {
        var components = URLComponents(
            url: Self.baseURL.appendingPathComponent("v0/search/subjects"),
            resolvingAgainstBaseURL: false
        )!
        components.queryItems = [URLQueryItem(name: "limit", value: String(limit))]
        let request = try http.request(
            components.url!,
            method: "POST",
            authorization: authorization,
            json: ["keyword": query, "filter": ["type": [Self.gameSubjectType]]] as [String: Any]
        )
        return try http.decode(SearchResults.self, from: try await http.send(request)).data
    }

    public func subject(id: Int) async throws -> BangumiSubject {
        let request = try http.request(
            Self.baseURL.appendingPathComponent("v0/subjects/\(id)"),
            authorization: authorization
        )
        return try http.decode(BangumiSubject.self, from: try await http.send(request))
    }

    public func account() async throws -> ServiceAccount {
        struct Me: Decodable {
            var username: String
            var nickname: String?
        }
        let request = try http.request(
            Self.baseURL.appendingPathComponent("v0/me"),
            authorization: authorization
        )
        let me = try http.decode(Me.self, from: try await http.send(request))
        return ServiceAccount(service: .bangumi, username: me.username)
    }

    /// The user's existing collection entry, or nil when it isn't collected.
    public func collection(username: String, subjectID: Int) async throws -> ListEntry? {
        struct Collection: Decodable {
            var type: Int
            var rate: Int?
            var comment: String?
        }
        let request = try http.request(
            Self.baseURL.appendingPathComponent("v0/users/\(username)/collections/\(subjectID)"),
            authorization: authorization
        )
        let data: Data
        do {
            data = try await http.send(request)
        } catch MetadataError.http(_, 404, _) {
            return nil
        }
        let collection = try http.decode(Collection.self, from: data)
        return ListEntry(
            status: PlayStatus.allCases.first { $0.bangumiType == collection.type },
            rating: collection.rate.flatMap { $0 > 0 ? $0 : nil },
            comment: collection.comment?.nonEmpty
        )
    }

    /// Creates or updates the collection entry. API v0 cannot remove a
    /// collection; an entry without a status goes through `BangumiWeb`.
    /// - Returns: false when there was nothing Bangumi can store.
    @discardableResult
    public func updateCollection(
        subjectID: Int,
        entry: ListEntry,
        includeRating: Bool
    ) async throws -> Bool {
        guard let body = Self.collectionBody(for: entry, includeRating: includeRating) else {
            return false
        }
        try await http.send(try http.request(
            Self.baseURL.appendingPathComponent("v0/users/-/collections/\(subjectID)"),
            method: "POST",
            authorization: authorization,
            json: body
        ))
        return true
    }

    static func collectionBody(for entry: ListEntry, includeRating: Bool) -> [String: Any]? {
        guard let status = entry.status else { return nil }
        var body: [String: Any] = ["type": status.bangumiType]
        if includeRating {
            // 0 clears the rating.
            body["rate"] = entry.rating ?? 0
            body["comment"] = entry.comment ?? ""
        }
        return body
    }
}

/// Bangumi's website, used only for what API v0 cannot do: removing a
/// subject from the collection. Third-party clients (Bangumi for Android,
/// czy0729's Bangumi) do the same through this undocumented endpoint:
/// `GET https://bgm.tv/subject/{id}/remove?gh={formhash}` with a signed-in
/// web session. Kept apart from `BangumiClient` because it depends on page
/// markup that can change without notice.
public enum BangumiWeb {
    public static let baseURL = URL(string: "https://bgm.tv")!
    public static let loginURL = URL(string: "https://bgm.tv/login")!

    public static func subjectURL(_ subjectID: Int) -> URL {
        baseURL.appendingPathComponent("subject/\(subjectID)")
    }

    public static func removeURL(subjectID: Int, formhash: String) -> URL {
        var components = URLComponents(
            url: baseURL.appendingPathComponent("subject/\(subjectID)/remove"),
            resolvingAgainstBaseURL: false
        )!
        components.queryItems = [URLQueryItem(name: "gh", value: formhash)]
        return components.url!
    }

    /// The session's formhash from a page's markup: the subject's own
    /// remove link first, then the logout link every signed-in page has.
    /// Nil means the page was served signed out. (The sign-in form has a
    /// formhash field too, so form fields are deliberately not used.)
    public static func formhash(in html: String, subjectID: Int? = nil) -> String? {
        var patterns: [String] = []
        if let subjectID {
            patterns.append(#"/subject/\#(subjectID)/remove\?gh=([A-Za-z0-9]+)"#)
        }
        patterns.append(#"/logout/([A-Za-z0-9]+)"#)
        for pattern in patterns {
            guard let expression = try? NSRegularExpression(pattern: pattern),
                  let match = expression.firstMatch(in: html, range: NSRange(html.startIndex..., in: html)),
                  let range = Range(match.range(at: 1), in: html) else { continue }
            return String(html[range])
        }
        return nil
    }

    /// The formhash of the OAuth approval form, as czy0729/Bangumi reads
    /// it. Nil for a signed-out page, whose sign-in form (posting to
    /// /FollowTheRabbit) has a formhash of its own.
    public static func authorizeFormhash(in html: String) -> String? {
        guard !html.contains("FollowTheRabbit"),
              let input = html.range(of: #"<input\b[^>]*\bname\s*=\s*["']?formhash\b[^>]*>"#, options: [.regularExpression, .caseInsensitive]) else {
            return nil
        }
        let tag = String(html[input])
        guard let expression = try? NSRegularExpression(pattern: #"\bvalue\s*=\s*["']?([A-Za-z0-9]+)"#, options: .caseInsensitive),
              let match = expression.firstMatch(in: tag, range: NSRange(tag.startIndex..., in: tag)),
              let range = Range(match.range(at: 1), in: tag) else { return nil }
        return String(tag[range])
    }
}
