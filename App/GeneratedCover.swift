import SwiftUI
import VNCore

/// Art for a game with no cover of its own, drawn from its folder name.
/// Paper, a colored band and a serif title, set vertically for Japanese and
/// Chinese titles. The same view makes the large library card (title,
/// engine) and the small stacked tiles in the new-games toast (one or two
/// letters).
struct GeneratedCover: View {
    let title: String
    let engine: String

    init(game: GameRecord) {
        title = Self.title(fromFolderName: game.folderName)
        engine = game.engine.displayName
    }

    var body: some View {
        GeometryReader { proxy in
            typeset(size: proxy.size, small: proxy.size.width < 90)
        }
        .clipped()
        .accessibilityHidden(true)
    }

    // MARK: Typeset

    private func typeset(size: CGSize, small: Bool) -> some View {
        let w = size.width
        let band = Self.color(hue, 0.52, 0.46)
        let ink = Color(red: 0x2B / 255, green: 0x26 / 255, blue: 0x21 / 255)
        let serif = Font.Design.serif
        return ZStack(alignment: .topLeading) {
            Color(red: 0xF6 / 255, green: 0xF2 / 255, blue: 0xEA / 255)
            band.frame(width: max(4, w * 0.06))
                .frame(maxHeight: .infinity)
            if small {
                Text(verbatim: initials)
                    .font(.system(size: w * (initials.count > 1 ? 0.36 : 0.48), weight: .bold, design: serif))
                    .foregroundStyle(ink)
                    .padding(.leading, w * 0.08)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                if isCJK {
                    // Vertical setting, right to left, like a package spine.
                    VerticalText(text: title, size: max(14, w * 0.075), color: ink)
                        .padding(.trailing, w * 0.07)
                        .padding(.vertical, size.height * 0.1)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                } else {
                    Text(verbatim: title)
                        .font(.system(size: max(15, w * 0.083), weight: .bold, design: serif))
                        .foregroundStyle(ink)
                        .lineLimit(2)
                        .minimumScaleFactor(0.7)
                        .padding(.leading, w * 0.12)
                        .padding(.trailing, w * 0.06)
                        .padding(.top, size.height * 0.16)
                }
                Text(verbatim: engine.uppercased())
                    .font(.system(size: max(9, w * 0.032), weight: .bold))
                    .kerning(1)
                    .foregroundStyle(Self.color(hue, 0.52, 0.34))
                    .padding(.leading, w * 0.12)
                    .padding(.bottom, w * 0.045)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)
            }
        }
    }

    // MARK: Text

    /// The folder name without group tags, versions and patch notes.
    static func title(fromFolderName folderName: String) -> String {
        var title = MatchFinder.searchText(fromFolderName: folderName)
        // Patch language tags stay in search text but never belong on a cover.
        if let expression = try? NSRegularExpression(pattern: #"(?i)(?<![a-z])(chs|cht|jpn?|eng?)(?![a-z])"#) {
            let range = NSRange(title.startIndex..., in: title)
            title = expression.stringByReplacingMatches(in: title, range: range, withTemplate: " ")
        }
        title = title.split(whereSeparator: \.isWhitespace).joined(separator: " ")
        if title.isEmpty { return folderName }
        // All-lowercase folder names read better capitalized.
        if title == title.lowercased(), title.unicodeScalars.allSatisfy(\.isASCII) {
            return title.capitalized
        }
        return title
    }

    private var isCJK: Bool {
        title.unicodeScalars.contains {
            (0x3040...0x30FF).contains($0.value) || (0x3400...0x9FFF).contains($0.value)
        }
    }

    /// The first character of a Japanese or Chinese title, else the first
    /// letters of its first two words.
    private var initials: String {
        let words = title.split(separator: " ")
        if isCJK || words.count < 2 {
            return title.first.map { String($0).uppercased() } ?? "?"
        }
        return (String(words[0].prefix(1)) + String(words[1].prefix(1))).uppercased()
    }

    /// A stable hue per title, so a game keeps its colors between launches.
    private var hue: Double {
        var hash: UInt32 = 2_166_136_261
        for scalar in title.unicodeScalars {
            hash ^= scalar.value
            hash = hash &* 16_777_619
        }
        return Double(hash % 360)
    }

    private static func color(_ hue: Double, _ saturation: Double, _ lightness: Double) -> Color {
        // HSL to HSB, which is what Color takes.
        let h = hue.truncatingRemainder(dividingBy: 360) / 360
        let v = lightness + saturation * min(lightness, 1 - lightness)
        let s = v == 0 ? 0 : 2 * (1 - lightness / v)
        return Color(hue: h, saturation: s, brightness: v)
    }
}

/// Characters stacked top to bottom in columns that run right to left.
private struct VerticalText: View {
    let text: String
    let size: CGFloat
    let color: Color

    var body: some View {
        GeometryReader { proxy in
            let perColumn = max(1, Int(proxy.size.height / (size * 1.12)))
            let characters = Array(text.filter { !$0.isWhitespace })
            let columns = stride(from: 0, to: characters.count, by: perColumn).map {
                Array(characters[$0..<min($0 + perColumn, characters.count)])
            }
            HStack(alignment: .top, spacing: size * 0.25) {
                ForEach(Array(columns.enumerated().reversed()), id: \.offset) { _, column in
                    VStack(spacing: size * 0.12) {
                        ForEach(Array(column.enumerated()), id: \.offset) { _, character in
                            Text(verbatim: String(character))
                                .font(.system(size: size, weight: .bold, design: .serif))
                                .foregroundStyle(color)
                        }
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
        }
    }
}
