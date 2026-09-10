import Foundation
#if canImport(ImageIO)
import ImageIO
#endif

public enum EngineID: String, Codable, CaseIterable, Identifiable, Sendable {
    case kirikiri
    case onscripter
    case renpy
    case artemis

    public var id: String { rawValue }

    public var documentsDirectoryName: String {
        switch self {
        case .kirikiri: return "krkr"
        case .onscripter: return "ons"
        case .renpy: return "renpy"
        case .artemis: return "artemis"
        }
    }

    public var displayName: String {
        switch self {
        case .kirikiri: return "KiriKiri"
        case .onscripter: return "ONScripter"
        case .renpy: return "Ren’Py"
        case .artemis: return "Artemis"
        }
    }

    public var isLaunchSupported: Bool { self == .kirikiri }
}

public enum LaunchTargetKind: String, Codable, Sendable {
    case directory
    case archive
}

public enum GameAvailability: Codable, Equatable, Sendable {
    case ready
    case invalid(String)
    case unsupported
    case missing

    public var canLaunch: Bool {
        if case .ready = self { return true }
        return false
    }
}

public struct LibraryRepository: Sendable {
    private struct Document: Codable {
        var version = 2
        var games: [GameRecord]
    }

    public let documentsRoot: URL
    public let metadataRoot: URL

    public init(documentsRoot: URL, metadataRoot: URL) {
        self.documentsRoot = documentsRoot
        self.metadataRoot = metadataRoot
    }

    public var libraryURL: URL { metadataRoot.appendingPathComponent("library-v2.json") }
    public var coversURL: URL { metadataRoot.appendingPathComponent("Covers", isDirectory: true) }

    public func engineRoot(for engine: EngineID) -> URL {
        documentsRoot.appendingPathComponent(engine.documentsDirectoryName, isDirectory: true)
    }

    public func ensureStructure() throws {
        let fm = FileManager.default
        try fm.createDirectory(at: metadataRoot, withIntermediateDirectories: true)
        try fm.createDirectory(at: coversURL, withIntermediateDirectories: true)
        for engine in EngineID.allCases {
            try fm.createDirectory(at: engineRoot(for: engine), withIntermediateDirectories: true)
        }
    }

    public func load() throws -> [GameRecord] {
        guard FileManager.default.fileExists(atPath: libraryURL.path) else { return [] }
        let document = try JSONDecoder().decode(Document.self, from: Data(contentsOf: libraryURL))
        guard document.version == 2 else { throw LibraryError.unsupportedSchema(document.version) }
        return document.games
    }

    public func save(_ records: [GameRecord]) throws {
        try ensureStructure()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(Document(games: records)).write(to: libraryURL, options: .atomic)
    }

    @discardableResult
    public func scan() throws -> LibrarySnapshot {
        try ensureStructure()
        let previous = try load()
        var existingByKey: [String: GameRecord] = [:]
        for record in previous {
            existingByKey[key(engine: record.engine, folderName: record.folderName)] = record
        }
        var active: [GameRecord] = []

        for engine in EngineID.allCases {
            let root = engineRoot(for: engine)
            let children = try FileManager.default.contentsOfDirectory(
                at: root,
                includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey],
                options: [.skipsHiddenFiles]
            ).sorted {
                $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending
            }

            for child in children {
                let values = try child.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                guard values.isDirectory == true, values.isSymbolicLink != true else { continue }
                let recordKey = key(engine: engine, folderName: child.lastPathComponent)
                var scanned = try GameScanner.inspectGame(at: child, engine: engine)
                if let old = existingByKey.removeValue(forKey: recordKey) {
                    scanned = merging(scanned: scanned, metadata: old)
                }
                active.append(scanned)
            }
        }

        let missing = existingByKey.values.map { record -> GameRecord in
            var result = record
            result.availability = .missing
            return result
        }.sorted {
            $0.title.localizedStandardCompare($1.title) == .orderedAscending
        }
        try save(active + missing)
        return LibrarySnapshot(active: active, missing: missing)
    }

    public func importFolder(_ source: URL, engine: EngineID) throws {
        try ensureStructure()
        let folderName = source.lastPathComponent.precomposedStringWithCanonicalMapping
        guard !folderName.isEmpty, folderName != ".", folderName != "..",
              !folderName.contains("/"), !folderName.contains("\\"),
              !folderName.hasPrefix(".") else {
            throw LibraryError.invalidFolderName
        }
        try validateImportTree(source)
        let root = engineRoot(for: engine)
        if try containsFolder(named: folderName, in: root) {
            throw LibraryError.duplicateName(folderName)
        }

        let staging = root.appendingPathComponent(".incoming-\(UUID().uuidString)", isDirectory: true)
        let destination = root.appendingPathComponent(folderName, isDirectory: true)
        defer { try? FileManager.default.removeItem(at: staging) }
        try FileManager.default.copyItem(at: source, to: staging)
        try FileManager.default.moveItem(at: staging, to: destination)
    }

    public func relink(_ record: GameRecord, to source: URL) throws {
        var records = try load()
        guard records.contains(where: { $0.id == record.id }) else {
            throw LibraryError.recordNotFound
        }
        try validateImportTree(source)
        let detected = try GameScanner.detectEngines(in: source)
        if detected.count == 1, let detectedEngine = detected.first, detectedEngine != record.engine {
            throw LibraryError.engineMismatch(expected: record.engine, detected: detectedEngine)
        }

        let root = engineRoot(for: record.engine).standardizedFileURL
        let sourceURL = source.standardizedFileURL
        let destination: URL
        if sourceURL.deletingLastPathComponent() == root {
            destination = sourceURL
        } else {
            try importFolder(source, engine: record.engine)
            destination = root.appendingPathComponent(source.lastPathComponent, isDirectory: true)
        }

        let targetKey = key(engine: record.engine, folderName: destination.lastPathComponent)
        records.removeAll {
            $0.id != record.id && key(engine: $0.engine, folderName: $0.folderName) == targetKey
        }
        guard let index = records.firstIndex(where: { $0.id == record.id }) else {
            throw LibraryError.recordNotFound
        }
        records[index].folderName = destination.lastPathComponent
        records[index].title = destination.lastPathComponent
        records[index].availability = .missing
        try save(records)
    }

    public func forget(_ record: GameRecord) throws {
        var records = try load()
        records.removeAll { $0.id == record.id }
        try save(records)
        if let name = record.customCoverName {
            try? FileManager.default.removeItem(
                at: try GameScanner.containedURL(name, in: coversURL)
            )
        }
    }

    public func directory(for game: GameRecord) throws -> URL {
        try GameScanner.containedURL(game.folderName, in: engineRoot(for: game.engine))
    }

    public func entryPoint(for game: GameRecord) throws -> URL {
        let directory = try directory(for: game)
        guard let target = game.launchTarget else { throw LibraryError.noGame }
        return target == "." ? directory : try GameScanner.containedURL(target, in: directory)
    }

    public func coverURL(for game: GameRecord) -> URL? {
        if let custom = game.customCoverName,
           let customURL = try? GameScanner.containedURL(custom, in: coversURL),
           FileManager.default.fileExists(atPath: customURL.path) {
            return customURL
        }
        guard let detected = game.detectedCoverPath,
              let directory = try? directory(for: game),
              let detectedURL = try? GameScanner.containedURL(detected, in: directory),
              FileManager.default.fileExists(atPath: detectedURL.path) else {
            return nil
        }
        return detectedURL
    }

    public func setCustomCover(for id: UUID, jpegData: Data) throws -> LibrarySnapshot {
        var records = try load()
        guard let index = records.firstIndex(where: { $0.id == id }) else {
            throw LibraryError.recordNotFound
        }
        try ensureStructure()
        let name = "\(id.uuidString).jpg"
        try jpegData.write(to: coversURL.appendingPathComponent(name), options: .atomic)
        records[index].customCoverName = name
        try save(records)
        return try scan()
    }

    private func containsFolder(named name: String, in root: URL) throws -> Bool {
        let expected = GameScanner.normalized(name)
        return try FileManager.default.contentsOfDirectory(
            at: root,
            includingPropertiesForKeys: nil,
            options: []
        ).contains {
            GameScanner.normalized($0.lastPathComponent) == expected
        }
    }

    private func validateImportTree(_ source: URL) throws {
        let rootValues = try source.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard rootValues.isDirectory == true else { throw LibraryError.noGame }
        guard rootValues.isSymbolicLink != true else { throw LibraryError.symbolicLink }
        guard let walker = FileManager.default.enumerator(
            at: source,
            includingPropertiesForKeys: [.isSymbolicLinkKey],
            options: []
        ) else { throw LibraryError.noGame }
        for case let item as URL in walker {
            if try item.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true {
                throw LibraryError.symbolicLink
            }
        }
    }

    private func key(engine: EngineID, folderName: String) -> String {
        "\(engine.rawValue):\(GameScanner.normalized(folderName))"
    }

    private func merging(scanned: GameRecord, metadata: GameRecord) -> GameRecord {
        GameRecord(
            id: metadata.id,
            title: scanned.title,
            engine: scanned.engine,
            folderName: scanned.folderName,
            launchTargetKind: scanned.launchTargetKind,
            launchTarget: scanned.launchTarget,
            byteCount: scanned.byteCount,
            addedAt: metadata.addedAt,
            lastPlayedAt: metadata.lastPlayedAt,
            playTime: metadata.playTime,
            detectedCoverPath: scanned.detectedCoverPath,
            customCoverName: metadata.customCoverName,
            availability: scanned.availability
        )
    }
}

public struct GameRecord: Codable, Identifiable, Equatable, Sendable {
    public let id: UUID
    public var title: String
    public var engine: EngineID
    public var folderName: String
    public var launchTargetKind: LaunchTargetKind?
    public var launchTarget: String?
    public var byteCount: Int64
    public var addedAt: Date
    public var lastPlayedAt: Date?
    public var playTime: TimeInterval
    public var detectedCoverPath: String?
    public var customCoverName: String?
    public var availability: GameAvailability

    public init(
        id: UUID = UUID(),
        title: String,
        engine: EngineID,
        folderName: String,
        launchTargetKind: LaunchTargetKind? = nil,
        launchTarget: String? = nil,
        byteCount: Int64 = 0,
        addedAt: Date = Date(),
        lastPlayedAt: Date? = nil,
        playTime: TimeInterval = 0,
        detectedCoverPath: String? = nil,
        customCoverName: String? = nil,
        availability: GameAvailability = .unsupported
    ) {
        self.id = id
        self.title = title
        self.engine = engine
        self.folderName = folderName
        self.launchTargetKind = launchTargetKind
        self.launchTarget = launchTarget
        self.byteCount = byteCount
        self.addedAt = addedAt
        self.lastPlayedAt = lastPlayedAt
        self.playTime = playTime
        self.detectedCoverPath = detectedCoverPath
        self.customCoverName = customCoverName
        self.availability = availability
    }
}

public struct LibrarySnapshot: Equatable, Sendable {
    public var active: [GameRecord]
    public var missing: [GameRecord]

    public init(active: [GameRecord], missing: [GameRecord]) {
        self.active = active
        self.missing = missing
    }
}

public enum LibraryError: LocalizedError, Equatable {
    case noGame
    case unsafePath
    case symbolicLink
    case unsupportedSchema(Int)
    case duplicateName(String)
    case invalidFolderName
    case engineMismatch(expected: EngineID, detected: EngineID)
    case recordNotFound

    public var errorDescription: String? {
        switch self {
        case .noGame:
            return "没有识别到支持的游戏结构，请手动选择引擎。"
        case .unsafePath:
            return "游戏文件路径超出了允许的目录。"
        case .symbolicLink:
            return "游戏目录包含符号链接，暂不支持导入。"
        case .unsupportedSchema(let version):
            return "游戏库版本 \(version) 暂不受支持，请升级应用。"
        case .duplicateName(let name):
            return "“\(name)”已存在于目标引擎目录中，请先重命名文件夹。"
        case .invalidFolderName:
            return "游戏文件夹名称无效。"
        case .engineMismatch(let expected, let detected):
            return "所选文件夹识别为 \(detected.displayName)，无法关联到 \(expected.displayName) 游戏。"
        case .recordNotFound:
            return "游戏记录已经不存在。"
        }
    }
}

public enum GameScanner {
    private static let xp3Signature = Data([0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20, 0x0A, 0x1A, 0x8B, 0x67, 0x01])
    private static let preferredXP3Names = ["启动游戏.xp3", "startup.xp3", "start.xp3", "boot.xp3", "data.xp3"]
    private static let preferredCoverNames = ["cover", "folder", "poster", "thumbnail", "icon"]
    private static let coverExtensions: Set<String> = ["png", "jpg", "jpeg", "heic", "webp"]

    public static func inspectGame(at directory: URL, engine: EngineID) throws -> GameRecord {
        let values = try directory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true else { throw LibraryError.noGame }
        guard values.isSymbolicLink != true else { throw LibraryError.symbolicLink }

        let launch = try launchTarget(in: directory, engine: engine)
        let availability: GameAvailability
        if !engine.isLaunchSupported {
            availability = .unsupported
        } else if launch == nil {
            availability = .invalid("未找到有效的 startup.tjs 或 XP3")
        } else {
            availability = .ready
        }

        return GameRecord(
            title: directory.lastPathComponent,
            engine: engine,
            folderName: directory.lastPathComponent,
            launchTargetKind: launch?.kind,
            launchTarget: launch?.relativePath,
            byteCount: try byteCount(in: directory),
            detectedCoverPath: try detectedCover(in: directory),
            availability: availability
        )
    }

    public static func launchTarget(in directory: URL, engine: EngineID) throws -> (kind: LaunchTargetKind, relativePath: String)? {
        guard engine == .kirikiri else { return nil }
        let entries = try rootFiles(in: directory)
        if entries.contains(where: { normalized($0.lastPathComponent) == normalized("startup.tjs") }) {
            return (.directory, ".")
        }

        var archives: [URL] = []
        for entry in entries where normalized(entry.pathExtension) == "xp3" {
            if try isXP3(entry) { archives.append(entry) }
        }
        guard !archives.isEmpty else { return nil }

        for preferredName in preferredXP3Names {
            if let match = archives.first(where: { normalized($0.lastPathComponent) == normalized(preferredName) }) {
                return (.archive, match.lastPathComponent)
            }
        }
        let selected = archives.sorted {
            $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending
        }[0]
        return (.archive, selected.lastPathComponent)
    }

    public static func detectedCover(in directory: URL) throws -> String? {
        let candidates = try rootFiles(in: directory).filter {
            coverExtensions.contains(normalized($0.pathExtension)) && isSupportedImage($0)
        }
        guard !candidates.isEmpty else { return nil }

        for basename in preferredCoverNames {
            if let match = candidates.first(where: {
                normalized($0.deletingPathExtension().lastPathComponent) == normalized(basename)
            }) {
                return match.lastPathComponent
            }
        }
        let folderBase = normalized(directory.lastPathComponent)
        if let match = candidates.first(where: {
            normalized($0.deletingPathExtension().lastPathComponent) == folderBase
        }) {
            return match.lastPathComponent
        }
        return candidates.sorted {
            $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending
        }.first?.lastPathComponent
    }

    public static func detectEngines(in directory: URL) throws -> [EngineID] {
        let entries = try rootFiles(in: directory)
        let names = Set(entries.map { normalized($0.lastPathComponent) })
        var matches: [EngineID] = []

        if try launchTarget(in: directory, engine: .kirikiri) != nil {
            matches.append(.kirikiri)
        }
        if !names.isDisjoint(with: ["0.txt", "00.txt", "nscript.dat", "nscr_sec.dat", "arc.nsa"]) {
            matches.append(.onscripter)
        }

        let gameDirectory = directory.appendingPathComponent("game", isDirectory: true)
        let gameScripts = (try? rootFiles(in: gameDirectory)) ?? []
        let hasRenPyScript = gameScripts.contains {
            ["rpy", "rpyc"].contains(normalized($0.pathExtension))
        }
        let hasRenPyArchive = (entries + gameScripts).contains {
            normalized($0.pathExtension) == "rpa"
        }
        if hasRenPyScript || hasRenPyArchive { matches.append(.renpy) }

        return matches
    }

    public static func isXP3(_ url: URL) throws -> Bool {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        return try file.read(upToCount: xp3Signature.count) == xp3Signature
    }

    public static func containedURL(_ relative: String, in root: URL) throws -> URL {
        guard !relative.isEmpty, !relative.hasPrefix("/"), !relative.contains("\\"),
              !relative.split(separator: "/").contains("..") else { throw LibraryError.unsafePath }
        let base = root.standardizedFileURL.resolvingSymlinksInPath()
        var candidate = base
        for component in relative.split(separator: "/") {
            candidate.appendPathComponent(String(component))
            do {
                let attributes = try FileManager.default.attributesOfItem(atPath: candidate.path)
                if attributes[.type] as? FileAttributeType == .typeSymbolicLink {
                    throw LibraryError.unsafePath
                }
            } catch let error as NSError where
                error.domain == NSCocoaErrorDomain && error.code == NSFileNoSuchFileError {
                // Missing leaves are valid when constructing a future destination.
            }
        }
        let result = candidate.standardizedFileURL
        guard result.path.hasPrefix(base.path + "/") else { throw LibraryError.unsafePath }
        return result
    }

    public static func normalized(_ value: String) -> String {
        value.precomposedStringWithCanonicalMapping
            .folding(options: [.caseInsensitive], locale: Locale(identifier: "en_US_POSIX"))
    }

    private static func rootFiles(in directory: URL) throws -> [URL] {
        var isDirectory = ObjCBool(false)
        guard FileManager.default.fileExists(
            atPath: directory.path,
            isDirectory: &isDirectory
        ), isDirectory.boolValue else {
            return []
        }
        return try FileManager.default.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey],
            options: [.skipsHiddenFiles]
        ).filter {
            let values = try? $0.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            return values?.isRegularFile == true && values?.isSymbolicLink != true
        }
    }

    private static func byteCount(in directory: URL) throws -> Int64 {
        guard let walker = FileManager.default.enumerator(
            at: directory,
            includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey],
            options: [.skipsHiddenFiles]
        ) else { return 0 }
        var total: Int64 = 0
        for case let file as URL in walker {
            let values = try file.resourceValues(
                forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey]
            )
            if values.isSymbolicLink == true {
                walker.skipDescendants()
            } else if values.isRegularFile == true {
                total += Int64(values.fileSize ?? 0)
            }
        }
        return total
    }

    private static func isSupportedImage(_ url: URL) -> Bool {
#if canImport(ImageIO)
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              CGImageSourceGetCount(source) > 0 else {
            return false
        }
        return CGImageSourceCreateImageAtIndex(source, 0, nil) != nil
#else
        guard let handle = try? FileHandle(forReadingFrom: url) else { return false }
        defer { try? handle.close() }
        guard let data = try? handle.read(upToCount: 16), !data.isEmpty else {
            return false
        }
        let bytes = [UInt8](data)
        if bytes.starts(with: [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]) { return true }
        if bytes.starts(with: [0xFF, 0xD8, 0xFF]) { return true }
        if bytes.count >= 12,
           String(bytes: bytes[0..<4], encoding: .ascii) == "RIFF",
           String(bytes: bytes[8..<12], encoding: .ascii) == "WEBP" { return true }
        if bytes.count >= 12, String(bytes: bytes[4..<8], encoding: .ascii) == "ftyp" {
            let brand = String(bytes: bytes[8..<12], encoding: .ascii) ?? ""
            return ["heic", "heix", "hevc", "hevx", "mif1", "msf1"].contains(brand)
        }
        return false
#endif
    }
}
