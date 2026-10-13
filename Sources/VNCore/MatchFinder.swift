import Foundation

/// One possible work for a game folder, from VNDB, Bangumi or both.
public struct MatchCandidate: Identifiable, Equatable, Sendable {
    public var vndb: VNDBVisualNovel?
    public var bangumi: BangumiSubject?
    /// 0–1 similarity between the search text and the closest title.
    public var score: Double

    public init(vndb: VNDBVisualNovel? = nil, bangumi: BangumiSubject? = nil, score: Double = 0) {
        self.vndb = vndb
        self.bangumi = bangumi
        self.score = score
    }

    public var id: String {
        "\(vndb?.id ?? "-")|\(bangumi.map { String($0.id) } ?? "-")"
    }

    public var allTitles: [String] {
        (bangumi?.allTitles ?? []) + (vndb?.allTitles ?? [])
    }

    /// The Chinese title when Bangumi has one, else the original title.
    public var title: String {
        bangumi?.nameCN?.nonEmpty ?? vndb?.originalTitle ?? bangumi?.name ?? ""
    }

    /// The original title, when it differs from `title`.
    public var originalTitle: String? {
        let original = vndb?.originalTitle ?? bangumi?.name
        return original == title ? nil : original
    }

    public var year: String? {
        if let released = vndb?.released, released.count >= 4, Int(released.prefix(4)) != nil {
            return String(released.prefix(4))
        }
        if let date = bangumi?.date, date.count >= 4, Int(date.prefix(4)) != nil {
            return String(date.prefix(4))
        }
        return nil
    }

    public var thumbnailURL: String? {
        bangumi?.thumbnailURL ?? vndb?.image?.url
    }

    public var thumbnailIsSensitive: Bool {
        bangumi != nil ? (bangumi?.nsfw ?? false) : (vndb?.image?.isSensitive ?? false)
    }
}

public enum MatchFinder {
    /// Words that release groups and archives add to folder names but that
    /// are never part of a title.
    private static let noiseWords = [
        "完全汉化", "简体中文版", "繁體中文版", "繁体中文版", "中文版", "汉化版", "漢化版", "汉化", "漢化",
        "官方中文", "官中", "硬盘版", "硬碟版", "免安装版", "免安裝版", "免DVD", "DL版", "PK版",
        "steam版", "精翻", "机翻", "AI翻译"
    ]

    /// Turns a folder name such as "[汉化版] 夏影回廊 Ver1.02" into search
    /// text such as "夏影回廊".
    public static func searchText(fromFolderName folderName: String) -> String {
        var text = folderName.precomposedStringWithCanonicalMapping
        let patterns = [
            // Group tags in square and lenticular brackets.
            #"\[[^\]]*\]"#, #"【[^】]*】"#, #"〔[^〕]*〕"#,
            // Parenthesized notes that carry a version, year or noise word.
            #"[(（][^)）]*(?:\d|汉化|漢化|中文|版|ver|patch|DL|R18|18禁)[^)）]*[)）]"#,
            // Versions: "Ver1.02", "v1.0.3", "ver 2".
            // The lookbehind keeps words such as "Forever 2" intact without
            // needing a word boundary, which CJK text next to "Ver" lacks.
            #"(?i)(?<![a-z])ver\.?\s*\d+(?:\.\d+)*[a-z]?"#,
            #"(?i)(?<![a-z])v\d+(?:\.\d+)+[a-z]?"#
        ]
        for pattern in patterns {
            text = replace(pattern, in: text, with: " ")
        }
        for word in noiseWords {
            text = text.replacingOccurrences(of: word, with: " ", options: [.caseInsensitive])
        }
        text = text.replacingOccurrences(of: "_", with: " ")
        text = replace(#"\s+"#, in: text, with: " ")
        let trimmed = text.trimmingCharacters(in: CharacterSet(charactersIn: " -~・·.　").union(.whitespacesAndNewlines))
        // Never return nothing: a folder made only of noise still searches.
        return trimmed.isEmpty ? folderName : trimmed
    }

    /// Pairs VNDB and Bangumi results for the same work and ranks them by
    /// how closely their titles match `query`.
    public static func candidates(
        query: String,
        vndb: [VNDBVisualNovel],
        bangumi: [BangumiSubject]
    ) -> [MatchCandidate] {
        var remaining = bangumi
        var result: [MatchCandidate] = []
        for novel in vndb {
            let keys = Set(novel.allTitles.map(key))
            var candidate = MatchCandidate(vndb: novel)
            if let index = remaining.firstIndex(where: { subject in
                !keys.isDisjoint(with: subject.allTitles.map(key)) && yearsAgree(novel.released, subject.date)
            }) {
                candidate.bangumi = remaining.remove(at: index)
            }
            result.append(candidate)
        }
        result += remaining.map { MatchCandidate(bangumi: $0) }

        let queryKey = key(query)
        for index in result.indices {
            let best = result[index].allTitles.map { similarity(queryKey, key($0)) }.max() ?? 0
            // A work both services agree on is a slightly safer bet.
            let bothBonus = result[index].vndb != nil && result[index].bangumi != nil ? 0.05 : 0
            result[index].score = min(1, best + bothBonus)
        }
        // Stable: equal scores keep the services' own ranking.
        return result.enumerated()
            .sorted { lhs, rhs in
                lhs.element.score != rhs.element.score
                    ? lhs.element.score > rhs.element.score
                    : lhs.offset < rhs.offset
            }
            .map(\.element)
    }

    /// Whether the first candidate is close enough to call a best match.
    public static func isConfident(_ candidate: MatchCandidate) -> Bool {
        candidate.score >= 0.6
    }

    /// Case-, width- and punctuation-insensitive form for comparing titles.
    public static func key(_ title: String) -> String {
        let folded = title.precomposedStringWithCompatibilityMapping
            .folding(options: [.caseInsensitive, .widthInsensitive], locale: nil)
        return String(folded.unicodeScalars.filter {
            CharacterSet.alphanumerics.contains($0)
        }.map { Character($0) })
    }

    /// Dice coefficient over character bigrams; single characters compare
    /// directly. Short CJK titles make bigrams a better fit than words.
    public static func similarity(_ a: String, _ b: String) -> Double {
        if a.isEmpty || b.isEmpty { return 0 }
        if a == b { return 1 }
        let lhs = Array(a), rhs = Array(b)
        if lhs.count < 2 || rhs.count < 2 {
            return (a.contains(b) || b.contains(a)) ? 0.5 : 0
        }
        var counts: [String: Int] = [:]
        for index in 0..<(lhs.count - 1) {
            counts[String(lhs[index...index + 1]), default: 0] += 1
        }
        var shared = 0
        for index in 0..<(rhs.count - 1) {
            let pair = String(rhs[index...index + 1])
            if let count = counts[pair], count > 0 {
                shared += 1
                counts[pair] = count - 1
            }
        }
        return Double(2 * shared) / Double(lhs.count + rhs.count - 2)
    }

    /// Years only rule a pair out when both are known and far apart; a
    /// release and its Bangumi entry can differ by a year across regions.
    private static func yearsAgree(_ lhs: String?, _ rhs: String?) -> Bool {
        guard let lhs, let rhs, let a = Int(lhs.prefix(4)), let b = Int(rhs.prefix(4)) else { return true }
        return abs(a - b) <= 1
    }

    private static func replace(_ pattern: String, in text: String, with template: String) -> String {
        guard let expression = try? NSRegularExpression(pattern: pattern) else { return text }
        return expression.stringByReplacingMatches(
            in: text,
            range: NSRange(text.startIndex..., in: text),
            withTemplate: template
        )
    }
}
