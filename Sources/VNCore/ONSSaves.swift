import Foundation

/// A line of ONScripter text as Mikage shows it in the backlog and save
/// lists: the speaker apart from what they say, on one line.
public struct ONSLine: Equatable, Sendable {
    public let name: String
    public let text: String

    /// `name` is the line's [name] tag, if the game uses them. Without one,
    /// a leading 【name】, or a short name right before 「 or 『, is taken as
    /// the speaker.
    public init(name: String, text: String) {
        var speaker = name.trimmingCharacters(in: .whitespacesAndNewlines)
        var body = Self.joinLines(text)
        if speaker.isEmpty, let split = Self.splitSpeaker(body) {
            speaker = split.name
            body = split.text
        }
        self.name = speaker
        self.text = body
    }

    /// Line breaks in the game's text window are layout, not meaning:
    /// Chinese and Japanese text runs on, other text gets a space.
    static func joinLines(_ text: String) -> String {
        var result = ""
        for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
            let piece = line.trimmingCharacters(in: .whitespaces)
            guard !piece.isEmpty else { continue }
            if let last = result.unicodeScalars.last, let first = piece.unicodeScalars.first,
               last.isASCII || first.isASCII {
                result += " "
            }
            result += piece
        }
        return result
    }

    private static let maxNameLength = 12

    static func splitSpeaker(_ text: String) -> (name: String, text: String)? {
        if text.hasPrefix("【"), let close = text.firstIndex(of: "】") {
            let name = text[text.index(after: text.startIndex)..<close]
            let rest = text[text.index(after: close)...].trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty, name.count <= maxNameLength, !rest.isEmpty else { return nil }
            return (String(name), rest)
        }
        guard let open = text.firstIndex(where: { $0 == "「" || $0 == "『" }),
              open > text.startIndex else { return nil }
        let name = text[..<open].trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty, name.count <= maxNameLength,
              !name.contains(where: { $0.isWhitespace || $0.isPunctuation }) else { return nil }
        return (name, String(text[open...]))
    }
}

/// An ONScripter save: the game's own saveN.dat in its savedata folder,
/// with the screen and line the Mikage runtime writes beside it in
/// savedata/Mikage.
public struct ONSSave: Identifiable, Equatable, Sendable {
    public let slot: Int
    public let date: Date
    public let byteCount: Int64
    /// Empty for saves made before Mikage wrote details.
    public let line: ONSLine
    public let thumbnailURL: URL?
    public var id: Int { slot }
    /// Written by the in-game Quick Save, in a slot of its own.
    public var isQuickSave: Bool { slot == ONSSaveFiles.quickSaveSlot }
}

public enum ONSSaveFiles {
    public static let detailsFolderName = "Mikage"
    /// Quick Save's slot, outside the game's own save menu
    /// (MikageONSQuickSaveSlot).
    public static let quickSaveSlot = 1000

    public static func dataURL(slot: Int, in saves: URL) -> URL {
        saves.appendingPathComponent("save\(slot).dat")
    }

    /// The saves in a savedata folder, by slot.
    public static func saves(in directory: URL) -> [Int: ONSSave] {
        let fm = FileManager.default
        let keys: [URLResourceKey] = [.contentModificationDateKey, .fileSizeKey, .isRegularFileKey]
        guard let files = try? fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: keys) else {
            return [:]
        }
        let details = directory.appendingPathComponent(detailsFolderName, isDirectory: true)
        var result: [Int: ONSSave] = [:]
        for file in files {
            guard let slot = slot(fromFileName: file.lastPathComponent),
                  let values = try? file.resourceValues(forKeys: Set(keys)),
                  values.isRegularFile == true else { continue }
            let thumbnail = details.appendingPathComponent("save\(slot).jpg")
            result[slot] = ONSSave(
                slot: slot,
                date: values.contentModificationDate ?? .distantPast,
                byteCount: Int64(values.fileSize ?? 0),
                line: line(at: details.appendingPathComponent("save\(slot).json")),
                thumbnailURL: fm.fileExists(atPath: thumbnail.path) ? thumbnail : nil
            )
        }
        return result
    }

    /// Deletes the save and what Mikage and ONScripter keep with it.
    public static func delete(slot: Int, in directory: URL) throws {
        let fm = FileManager.default
        try fm.removeItem(at: dataURL(slot: slot, in: directory))
        let details = directory.appendingPathComponent(detailsFolderName, isDirectory: true)
        // ONScripter writes a second copy in sav/ when that folder exists.
        for extra in [directory.appendingPathComponent("sav/save\(slot).dat"),
                      details.appendingPathComponent("save\(slot).jpg"),
                      details.appendingPathComponent("save\(slot).json")]
        where fm.fileExists(atPath: extra.path) {
            try? fm.removeItem(at: extra)
        }
    }

    static func slot(fromFileName name: String) -> Int? {
        guard name.hasPrefix("save"), name.hasSuffix(".dat") else { return nil }
        let digits = name.dropFirst(4).dropLast(4)
        guard !digits.isEmpty, digits.count <= 4, digits.allSatisfy({ $0.isASCII && $0.isNumber }),
              let slot = Int(digits), slot > 0 else { return nil }
        return slot
    }

    private struct Details: Decodable {
        var name: String?
        var text: String?
    }

    private static func line(at url: URL) -> ONSLine {
        guard let data = try? Data(contentsOf: url),
              let details = try? JSONDecoder().decode(Details.self, from: data) else {
            return ONSLine(name: "", text: "")
        }
        return ONSLine(name: details.name ?? "", text: details.text ?? "")
    }
}
