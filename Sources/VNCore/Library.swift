import Foundation

public struct GameRecord: Codable, Identifiable, Equatable, Sendable {
    public let id: UUID
    public var title: String
    public var directory: String
    public var entryPoint: String
    public var byteCount: Int64
    public var addedAt: Date
    public var lastPlayedAt: Date?
    public var playTime: TimeInterval
    public var coverName: String?
    public var engine: String

    public init(id: UUID = UUID(), title: String, directory: String, entryPoint: String,
                byteCount: Int64 = 0, addedAt: Date = Date()) {
        self.id = id; self.title = title; self.directory = directory
        self.entryPoint = entryPoint; self.byteCount = byteCount; self.addedAt = addedAt
        self.playTime = 0; self.engine = "kirikiri"
    }
}

public enum LibraryError: LocalizedError {
    case noGame, ambiguousRoots, unsafePath, symbolicLink, unsupportedSchema(Int)
    public var errorDescription: String? {
        switch self {
        case .noGame: return "没有找到 startup.tjs 或 XP3 资源包，请选择游戏所在文件夹。"
        case .ambiguousRoots: return "文件夹包含多个可能的游戏，请分别选择具体游戏文件夹。"
        case .unsafePath: return "文件路径超出了游戏目录。"
        case .symbolicLink: return "游戏目录包含符号链接，暂不支持导入。"
        case .unsupportedSchema(let version): return "游戏库版本 \(version) 暂不受支持，请升级应用。"
        }
    }
}

public enum GameScanner {
    /// Candidate detection only; an XP3 signature does not prove game compatibility.
    public static func roots(in folder: URL, maxDepth: Int = 3) throws -> [URL] {
        let fm = FileManager.default
        var visited = 0
        func scan(_ directory: URL, _ depth: Int) throws -> [URL] {
            visited += 1
            guard visited <= 10_000 else { throw LibraryError.ambiguousRoots }
            guard try directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else { throw LibraryError.symbolicLink }
            let entries = try fm.contentsOfDirectory(at: directory,
                includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey], options: [.skipsHiddenFiles])
            for entry in entries {
                let values = try entry.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey])
                guard values.isSymbolicLink != true else { throw LibraryError.symbolicLink }
                guard values.isRegularFile == true else { continue }
                if entry.lastPathComponent.lowercased() == "startup.tjs" { return [directory] }
                if entry.pathExtension.lowercased() == "xp3", try isXP3(entry) { return [directory] }
            }
            guard depth < maxDepth else { return [] }
            return try entries.flatMap { entry -> [URL] in
                let values = try entry.resourceValues(forKeys: [.isDirectoryKey])
                return values.isDirectory == true ? try scan(entry, depth + 1) : []
            }
        }
        return try scan(folder, 0)
    }

    public static func isXP3(_ url: URL) throws -> Bool {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        return try file.read(upToCount: 11) == Data([0x58,0x50,0x33,0x0D,0x0A,0x20,0x0A,0x1A,0x8B,0x67,0x01])
    }

    public static func containedURL(_ relative: String, in root: URL) throws -> URL {
        guard !relative.isEmpty, !relative.hasPrefix("/"), !relative.contains("\\"),
              !relative.split(separator: "/").contains("..") else { throw LibraryError.unsafePath }
        let base = root.standardizedFileURL.resolvingSymlinksInPath()
        // Foundation may leave intermediate links unresolved when the leaf does not exist.
        // Check each existing component before constructing a writable path.
        var candidate = base
        for component in relative.split(separator: "/") {
            candidate.appendPathComponent(String(component))
            do {
                let attributes = try FileManager.default.attributesOfItem(atPath: candidate.path)
                if attributes[.type] as? FileAttributeType == .typeSymbolicLink { throw LibraryError.unsafePath }
            } catch let error as NSError where error.domain == NSCocoaErrorDomain && error.code == NSFileNoSuchFileError {
                // New paths are allowed; existing ancestors have been checked.
            }
        }
        let result = candidate.standardizedFileURL
        guard result.path.hasPrefix(base.path + "/") else { throw LibraryError.unsafePath }
        return result
    }
}

public struct LibraryRepository: Sendable {
    private struct Document: Codable { var version = 1; var games: [GameRecord] }
    public let root: URL
    public init(root: URL) { self.root = root }
    public var gamesURL: URL { root.appendingPathComponent("Games", isDirectory: true) }

    public func load() throws -> [GameRecord] {
        let url = root.appendingPathComponent("library.json")
        guard FileManager.default.fileExists(atPath: url.path) else { return [] }
        let document = try JSONDecoder().decode(Document.self, from: Data(contentsOf: url))
        guard document.version == 1 else { throw LibraryError.unsupportedSchema(document.version) }
        return document.games
    }

    public func save(_ records: [GameRecord]) throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(Document(games: records)).write(to: root.appendingPathComponent("library.json"), options: .atomic)
    }

    /// Copy into staging; only publish the directory once a complete copy succeeds.
    public func importFolder(_ source: URL) throws -> GameRecord {
        let fm = FileManager.default
        let candidates = try GameScanner.roots(in: source)
        guard !candidates.isEmpty else { throw LibraryError.noGame }
        guard candidates.count == 1 else { throw LibraryError.ambiguousRoots }
        let gameRoot = candidates[0]
        let id = UUID(), stagingRoot = root.appendingPathComponent("Incoming", isDirectory: true)
        try fm.createDirectory(at: stagingRoot, withIntermediateDirectories: true)
        try fm.createDirectory(at: gamesURL, withIntermediateDirectories: true)
        let staging = stagingRoot.appendingPathComponent(id.uuidString, isDirectory: true)
        let destination = gamesURL.appendingPathComponent(id.uuidString, isDirectory: true)
        defer { try? fm.removeItem(at: staging) }
        // Validate the entire tree, including directories below the detected root.
        guard let walker = fm.enumerator(at: gameRoot,
            includingPropertiesForKeys: [.isSymbolicLinkKey, .isRegularFileKey, .fileSizeKey],
            errorHandler: { _, _ in false }) else { throw LibraryError.noGame }
        var size: Int64 = 0
        for case let file as URL in walker {
            let values = try file.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey, .fileSizeKey])
            guard values.isSymbolicLink != true else { throw LibraryError.symbolicLink }
            if values.isRegularFile == true { size += Int64(values.fileSize ?? 0) }
        }
        try fm.copyItem(at: gameRoot, to: staging)
        try fm.moveItem(at: staging, to: destination)
        // Passing the directory lets KRKR resolve its own startup script and archives.
        return GameRecord(id: id, title: gameRoot.lastPathComponent, directory: id.uuidString,
                          entryPoint: ".", byteCount: size)
    }

    public func directory(for game: GameRecord) throws -> URL {
        try GameScanner.containedURL(game.directory, in: gamesURL)
    }

    public func removeImportedDirectory(for game: GameRecord) throws {
        try FileManager.default.removeItem(at: directory(for: game))
    }
}
