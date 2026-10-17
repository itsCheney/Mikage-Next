import Foundation
#if canImport(ImageIO)
import ImageIO
#endif

public enum EngineID: String, Codable, CaseIterable, Identifiable, Sendable {
    case kirikiri
    case onscripter
    case renpy
    case artemis
    case siglus

    public var id: String { rawValue }

    public var documentsDirectoryName: String {
        switch self {
        case .kirikiri: return "krkr"
        case .onscripter: return "ons"
        case .renpy: return "renpy"
        case .artemis: return "artemis"
        case .siglus: return "siglus"
        }
    }

    public var displayName: String {
        switch self {
        case .kirikiri: return "KiriKiri"
        case .onscripter: return "ONScripter"
        case .renpy: return "Ren’Py"
        case .artemis: return "Artemis"
        case .siglus: return "Siglus"
        }
    }

    public var isLaunchSupported: Bool { true }
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

    /// Saves of games removed with "keep saves", one folder per record id.
    public var keptSavesRoot: URL { metadataRoot.appendingPathComponent("KeptSaves", isDirectory: true) }

    /// The folder games write their saves to, inside the game folder.
    public static let savesFolderName = "savedata"

    public func keptSavesURL(for id: UUID) -> URL {
        keptSavesRoot.appendingPathComponent(id.uuidString, isDirectory: true)
    }

    /// Whether this game's folder was removed with its saves kept for later.
    public func hasKeptSaves(_ game: GameRecord) -> Bool {
        FileManager.default.fileExists(atPath: keptSavesURL(for: game.id).path)
    }

    public func savesURL(for game: GameRecord) throws -> URL {
        try directory(for: game).appendingPathComponent(Self.savesFolderName, isDirectory: true)
    }

    /// Game folders sit directly in Documents; each one's engine is
    /// detected from its files.
    public var gamesRoot: URL { documentsRoot }

    /// Files in Documents that tell the player where games go, in English and
    /// Chinese. They are there while the library has no games of the
    /// player's own, and removed once it has.
    public static let dropTipNames = ["DROP_GAMES_HERE", "把游戏放在这里"]
    static let dropTipText = """
    Put each game's folder here, directly in this folder, then return to Mikage.
    Mikage finds the engine (KiriKiri, ONScripter, Artemis, Ren’Py or Siglus) by itself.
    This file goes away once a game is found.

    把每个游戏的文件夹直接放在这里，然后回到 Mikage。
    Mikage 会自动识别引擎（KiriKiri、ONScripter、Artemis、Ren’Py 或 Siglus）。
    找到游戏后，这个文件会自动删除。

    """

    /// Written once the engine folders of older versions (Documents/krkr,
    /// ons, renpy, artemis) have been emptied into Documents.
    var flatLayoutMarkerURL: URL { metadataRoot.appendingPathComponent("flat-documents-layout") }

    public func ensureStructure() throws {
        let fm = FileManager.default
        try fm.createDirectory(at: metadataRoot, withIntermediateDirectories: true)
        try fm.createDirectory(at: coversURL, withIntermediateDirectories: true)
        try fm.createDirectory(at: gamesRoot, withIntermediateDirectories: true)
    }

    /// Moves every game out of the per-engine folders older versions used
    /// into Documents itself, keeps the records pointing at them, and deletes
    /// the engine folders. Runs once; a name already taken in Documents gets
    /// a number appended.
    func migrateEngineFoldersIfNeeded() throws {
        let fm = FileManager.default
        guard !fm.fileExists(atPath: flatLayoutMarkerURL.path) else { return }
        var records = try load()
        var renamed = false
        for engine in EngineID.allCases {
            let folder = documentsRoot.appendingPathComponent(engine.documentsDirectoryName, isDirectory: true)
            var isDirectory = ObjCBool(false)
            guard fm.fileExists(atPath: folder.path, isDirectory: &isDirectory), isDirectory.boolValue else { continue }
            for child in try fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil, options: []) {
                let name = child.lastPathComponent
                // Interrupted imports and Finder metadata are not games.
                if name.hasPrefix(".incoming-") || name == ".DS_Store" {
                    try? fm.removeItem(at: child)
                    continue
                }
                let target = uniqueName(for: name, in: gamesRoot)
                try fm.moveItem(at: child, to: gamesRoot.appendingPathComponent(target))
                guard target != name else { continue }
                let oldKey = GameScanner.normalized(name)
                for index in records.indices {
                    guard records[index].engine == engine,
                          GameScanner.normalized(records[index].folderName) == oldKey else { continue }
                    records[index].folderName = target
                    renamed = true
                }
            }
            try fm.removeItem(at: folder)
        }
        if renamed { try save(records) }
        try fm.createDirectory(at: metadataRoot, withIntermediateDirectories: true)
        try Data().write(to: flatLayoutMarkerURL)
    }

    /// `name`, or `name 2`, `name 3`... when that is taken in `root`.
    private func uniqueName(for name: String, in root: URL) -> String {
        let taken = Set(((try? FileManager.default.contentsOfDirectory(atPath: root.path)) ?? []).map(GameScanner.normalized))
        guard taken.contains(GameScanner.normalized(name)) else { return name }
        var number = 2
        while taken.contains(GameScanner.normalized("\(name) \(number)")) { number += 1 }
        return "\(name) \(number)"
    }

    /// Adds the drop tips while no game of the player's own is in the
    /// library (built-in games don't count), and removes them once there is.
    private func updateDropTips(active: [GameRecord]) {
        let fm = FileManager.default
        let hasOwnGame = active.contains { game in
            guard let folder = try? self.directory(for: game) else { return true }
            return !fm.fileExists(atPath: folder.appendingPathComponent(Self.builtInMarkerName).path)
        }
        for name in Self.dropTipNames {
            let url = documentsRoot.appendingPathComponent(name)
            if hasOwnGame {
                try? fm.removeItem(at: url)
            } else if !fm.fileExists(atPath: url.path) {
                try? Data(Self.dropTipText.utf8).write(to: url)
            }
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

    /// Serializes read-modify-write access to `library-v2.json`.
    ///
    /// Every mutating operation runs under this lock, so a caller that reads the
    /// file, edits records and saves cannot interleave with a concurrent scan
    /// and lose the other's write. The lock is keyed on the metadata root, which
    /// means separate repository instances over the same directory — the app's
    /// MainActor value and the detached copies it hands to background tasks —
    /// still exclude each other.
    private static let locks = NSMapTable<NSString, NSRecursiveLock>.strongToStrongObjects()
    private static let locksGuard = NSLock()

    private var fileLock: NSRecursiveLock {
        let key = metadataRoot.standardizedFileURL.path as NSString
        Self.locksGuard.lock()
        defer { Self.locksGuard.unlock() }
        if let existing = Self.locks.object(forKey: key) { return existing }
        let created = NSRecursiveLock()
        Self.locks.setObject(created, forKey: key)
        return created
    }

    /// Reads the current records, applies `transform` and writes the result.
    /// Prefer this over `load` + `save` from callers that hold stale state.
    @discardableResult
    public func mutate<T>(_ transform: (inout [GameRecord]) throws -> T) throws -> T {
        let lock = fileLock
        lock.lock()
        defer { lock.unlock() }
        var records = try load()
        let result = try transform(&records)
        try save(records)
        return result
    }

    @discardableResult
    public func scan() throws -> LibrarySnapshot {
        let lock = fileLock
        lock.lock()
        defer { lock.unlock() }
        try ensureStructure()
        try migrateEngineFoldersIfNeeded()
        let previous = try load()
        var existingByKey: [String: GameRecord] = [:]
        // Older libraries kept a folder name once per engine folder; the
        // extra records stay in the library as missing games.
        var sameNameRecords: [GameRecord] = []
        for record in previous {
            let recordKey = key(folderName: record.folderName)
            if existingByKey[recordKey] == nil {
                existingByKey[recordKey] = record
            } else {
                sameNameRecords.append(record)
            }
        }
        var active: [GameRecord] = []

        let children = try FileManager.default.contentsOfDirectory(
            at: gamesRoot,
            includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey],
            options: [.skipsHiddenFiles]
        ).sorted {
            $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending
        }

        do {
            for var child in children {
                let values = try child.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                guard values.isDirectory == true, values.isSymbolicLink != true else { continue }
                let recordKey = key(folderName: child.lastPathComponent)
                // A folder the library already knows keeps its engine (it may
                // have been chosen by hand) unless its files now say another.
                // An unknown folder is a game only if an engine recognizes it.
                let detected = try GameScanner.detectEngines(in: child)
                let known = existingByKey[recordKey]
                let engine: EngineID
                if let known, detected.isEmpty || detected.contains(known.engine) {
                    engine = known.engine
                } else if let first = detected.first {
                    engine = first
                } else {
                    continue
                }
                let old = existingByKey.removeValue(forKey: recordKey)
                // A game removed with its saves kept is back: return the saves
                // before measuring, so the size includes them.
                if let old, restoreKeptSaves(for: old.id, into: child) {
                    child.removeAllCachedResourceValues()
                }
                let modifiedAt = try child.resourceValues(forKeys: [.contentModificationDateKey])
                    .contentModificationDate

                // Adding, removing or replacing a file updates its parent's
                // modification date, so an unchanged date means this folder's
                // direct contents are unchanged. Nested writes do not touch it,
                // so this is a heuristic: it can keep a stale total until the
                // next visible change, which is preferable to walking tens of
                // thousands of files on every foreground scan. A launch always
                // creates savedata/, so play does refresh it.
                var reusableSize: Int64?
                if let old, let modifiedAt, let measuredAt = old.sizeMeasuredAt,
                   measuredAt == modifiedAt, old.availability != .missing {
                    reusableSize = old.byteCount
                }

                var scanned = try GameScanner.inspectGame(
                    at: child,
                    engine: engine,
                    reusableSize: reusableSize
                )
                scanned.sizeMeasuredAt = modifiedAt
                if let old {
                    scanned = merging(scanned: scanned, metadata: old)
                }
                active.append(scanned)
            }
        }

        let missing = (Array(existingByKey.values) + sameNameRecords).map { record -> GameRecord in
            var result = record
            result.availability = .missing
            return result
        }.sorted {
            $0.title.localizedStandardCompare($1.title) == .orderedAscending
        }
        try save(active + missing)
        updateDropTips(active: active)
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
        let root = gamesRoot
        if try containsFolder(named: folderName, in: root) {
            throw LibraryError.duplicateName(folderName)
        }

        let staging = root.appendingPathComponent(".incoming-\(UUID().uuidString)", isDirectory: true)
        let destination = root.appendingPathComponent(folderName, isDirectory: true)
        defer { try? FileManager.default.removeItem(at: staging) }
        try FileManager.default.copyItem(at: source, to: staging)
        try FileManager.default.moveItem(at: staging, to: destination)
        // The engine chosen at import, which the scan keeps for this folder.
        try mutate { records in
            guard !records.contains(where: { key(folderName: $0.folderName) == key(folderName: folderName) }) else { return }
            records.append(GameRecord(title: folderName, engine: engine, folderName: folderName, availability: .missing))
        }
    }

    /// Remembers which built-in games were copied in, so one the player
    /// deletes is not copied back.
    public var builtInGamesURL: URL { metadataRoot.appendingPathComponent("builtin-games.json") }

    /// Copies the games shipped in the app (`<root>/<engine folder>/<game>`,
    /// e.g. `renpy/The Question`) into the library, each one once.
    ///
    /// A folder already in the library with the same name is left alone and
    /// counts as installed. Returns the games copied in this call.
    @discardableResult
    public func installBuiltInGames(from root: URL) throws -> [String] {
        let lock = fileLock
        lock.lock()
        defer { lock.unlock() }
        let fm = FileManager.default
        guard fm.fileExists(atPath: root.path) else { return [] }
        try ensureStructure()
        var installed = Set<String>()
        if fm.fileExists(atPath: builtInGamesURL.path) {
            installed = Set(try JSONDecoder().decode([String].self, from: Data(contentsOf: builtInGamesURL)))
        }

        var copied: [String] = []
        var recorded = installed
        for engine in EngineID.allCases {
            let engineSource = root.appendingPathComponent(engine.documentsDirectoryName, isDirectory: true)
            guard fm.fileExists(atPath: engineSource.path) else { continue }
            let games = try fm.contentsOfDirectory(
                at: engineSource,
                includingPropertiesForKeys: [.isDirectoryKey],
                options: [.skipsHiddenFiles]
            ).sorted { $0.lastPathComponent < $1.lastPathComponent }
            for game in games {
                guard try game.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true else { continue }
                let name = "\(engine.documentsDirectoryName)/\(game.lastPathComponent)"
                let destination = gamesRoot.appendingPathComponent(game.lastPathComponent, isDirectory: true)
                guard !installed.contains(name) else {
                    try addMissingFiles(from: game, to: destination)
                    continue
                }
                recorded.insert(name)
                if try containsFolder(named: game.lastPathComponent, in: gamesRoot) { continue }
                try importFolder(game, engine: engine)
                try Data().write(to: destination.appendingPathComponent(Self.builtInMarkerName))
                copied.append(name)
            }
        }

        if recorded != installed {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(recorded.sorted()).write(to: builtInGamesURL, options: .atomic)
        }
        return copied
    }

    /// Marks a game folder as a copy of a built-in game, so later app
    /// versions can add files to it.
    static let builtInMarkerName = ".mikage-builtin"

    /// Copies files a newer app version added to a built-in game into the
    /// player's copy. Files already there, including the player's saves and
    /// Ren'Py's compiled scripts, are left as they are.
    private func addMissingFiles(from source: URL, to destination: URL) throws {
        let fm = FileManager.default
        let marker = destination.appendingPathComponent(Self.builtInMarkerName)
        if !fm.fileExists(atPath: marker.path) {
            // Copies made before the marker existed: claim the folder only
            // when its script is still the bundled one byte for byte.
            let script = "game/script.rpy"
            guard let bundled = try? Data(contentsOf: source.appendingPathComponent(script)),
                  let installed = try? Data(contentsOf: destination.appendingPathComponent(script)),
                  bundled == installed else { return }
            try Data().write(to: marker)
        }
        guard let walker = fm.enumerator(at: source, includingPropertiesForKeys: [.isDirectoryKey]) else { return }
        let base = source.resolvingSymlinksInPath().path
        for case let item as URL in walker {
            let path = item.resolvingSymlinksInPath().path
            guard path.hasPrefix(base + "/") else { continue }
            let target = destination.appendingPathComponent(String(path.dropFirst(base.count + 1)))
            if try item.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true {
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
            } else if !fm.fileExists(atPath: target.path) {
                try fm.copyItem(at: item, to: target)
            }
        }
    }

    public func relink(_ record: GameRecord, to source: URL) throws {
        let lock = fileLock
        lock.lock()
        defer { lock.unlock() }
        var records = try load()
        guard records.contains(where: { $0.id == record.id }) else {
            throw LibraryError.recordNotFound
        }
        try validateImportTree(source)
        let detected = try GameScanner.detectEngines(in: source)
        if detected.count == 1, let detectedEngine = detected.first, detectedEngine != record.engine {
            throw LibraryError.engineMismatch(expected: record.engine, detected: detectedEngine)
        }

        let root = gamesRoot.standardizedFileURL
        let sourceURL = source.standardizedFileURL
        let destination: URL
        if sourceURL.deletingLastPathComponent() == root {
            destination = sourceURL
        } else {
            try importFolder(source, engine: record.engine)
            destination = root.appendingPathComponent(source.lastPathComponent, isDirectory: true)
        }

        // importFolder may have added a record for the new folder; this one
        // takes its place.
        records = try load()
        let targetKey = key(folderName: destination.lastPathComponent)
        records.removeAll {
            $0.id != record.id && key(folderName: $0.folderName) == targetKey
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
        try mutate { records in
            records.removeAll { $0.id == record.id }
        }
        for file in [record.customCoverName, record.link?.posterName, record.link?.bannerName] {
            guard let name = file, let url = try? GameScanner.containedURL(name, in: coversURL) else { continue }
            try? FileManager.default.removeItem(at: url)
        }
        try? FileManager.default.removeItem(at: keptSavesURL(for: record.id))
    }

    /// Deletes the game folder but keeps its saves and its record.
    ///
    /// The saves move to `keptSavesRoot` and the next scan lists the record
    /// as missing. When a folder with the same name is imported or relinked,
    /// the scan matches it to this record and moves the saves back.
    public func offload(_ game: GameRecord) throws {
        let lock = fileLock
        lock.lock()
        defer { lock.unlock() }
        let fm = FileManager.default
        let directory = try directory(for: game)
        guard fm.fileExists(atPath: directory.path) else { throw LibraryError.noGame }
        try fm.createDirectory(at: keptSavesRoot, withIntermediateDirectories: true)
        let kept = keptSavesURL(for: game.id)
        let saves = directory.appendingPathComponent(Self.savesFolderName, isDirectory: true)
        if fm.fileExists(atPath: saves.path) {
            // Saves kept from an earlier removal were not restored because
            // the game made new ones; the newer saves win.
            if fm.fileExists(atPath: kept.path) { try fm.removeItem(at: kept) }
            try fm.moveItem(at: saves, to: kept)
        } else if !fm.fileExists(atPath: kept.path) {
            // An empty folder still marks the record as removed on purpose.
            try fm.createDirectory(at: kept, withIntermediateDirectories: true)
        }
        try fm.removeItem(at: directory)
    }

    /// Deletes the game folder, its saves and its record.
    public func delete(_ game: GameRecord) throws {
        let directory = try directory(for: game)
        if FileManager.default.fileExists(atPath: directory.path) {
            try FileManager.default.removeItem(at: directory)
        }
        try forget(game)
    }

    /// Moves saves kept by `offload` back into the game folder, unless the
    /// folder already has saves of its own. Returns whether anything moved.
    @discardableResult
    func restoreKeptSaves(for id: UUID, into directory: URL) -> Bool {
        let fm = FileManager.default
        let kept = keptSavesURL(for: id)
        guard fm.fileExists(atPath: kept.path) else { return false }
        let saves = directory.appendingPathComponent(Self.savesFolderName, isDirectory: true)
        if fm.fileExists(atPath: saves.path) {
            let contents = (try? fm.contentsOfDirectory(atPath: saves.path)) ?? []
            guard contents.isEmpty, (try? fm.removeItem(at: saves)) != nil else { return false }
        }
        return (try? fm.moveItem(at: kept, to: saves)) != nil
    }

    /// Downloaded Bangumi or VNDB artwork, if the file is still present.
    public func artworkURL(named name: String?) -> URL? {
        guard let name, let url = try? GameScanner.containedURL(name, in: coversURL),
              FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url
    }

    /// Stores downloaded artwork under `name` in the Covers folder.
    public func writeArtwork(_ data: Data, named name: String) throws {
        try ensureStructure()
        try data.write(to: try GameScanner.containedURL(name, in: coversURL), options: .atomic)
    }

    public func removeArtwork(named name: String?) {
        guard let name, let url = try? GameScanner.containedURL(name, in: coversURL) else { return }
        try? FileManager.default.removeItem(at: url)
    }

    /// Applies `transform` to one record and returns the result.
    @discardableResult
    public func update(_ id: UUID, _ transform: (inout GameRecord) throws -> Void) throws -> GameRecord {
        try mutate { records in
            guard let index = records.firstIndex(where: { $0.id == id }) else {
                throw LibraryError.recordNotFound
            }
            try transform(&records[index])
            return records[index]
        }
    }

    public func directory(for game: GameRecord) throws -> URL {
        try GameScanner.containedURL(game.folderName, in: gamesRoot)
    }

    public func entryPoint(for game: GameRecord) throws -> URL {
        let directory = try directory(for: game)
        guard let target = game.launchTarget else { throw LibraryError.noGame }
        return target == "." ? directory : try GameScanner.containedURL(target, in: directory)
    }

    public func coverURL(for game: GameRecord) -> URL? {
        customCoverURL(for: game) ?? detectedCoverURL(for: game)
    }

    /// A cover the player picked, if its file is still present.
    public func customCoverURL(for game: GameRecord) -> URL? {
        artworkURL(named: game.customCoverName)
    }

    /// An image found in the game folder itself.
    public func detectedCoverURL(for game: GameRecord) -> URL? {
        guard let detected = game.detectedCoverPath,
              let directory = try? directory(for: game),
              let detectedURL = try? GameScanner.containedURL(detected, in: directory),
              FileManager.default.fileExists(atPath: detectedURL.path) else {
            return nil
        }
        return detectedURL
    }

    /// Writes the cover and returns only the updated record.
    ///
    /// Deliberately does not rescan: a cover change cannot alter launch targets
    /// or folder contents, and a full scan re-stats every game directory. Batch
    /// callers can write many covers and refresh once at the end.
    @discardableResult
    public func setCustomCover(for id: UUID, jpegData: Data) throws -> GameRecord {
        try mutate { records in
            guard let index = records.firstIndex(where: { $0.id == id }) else {
                throw LibraryError.recordNotFound
            }
            try ensureStructure()
            let name = "\(id.uuidString).jpg"
            try jpegData.write(to: coversURL.appendingPathComponent(name), options: .atomic)
            records[index].customCoverName = name
            return records[index]
        }
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

    private func key(folderName: String) -> String {
        GameScanner.normalized(folderName)
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
            launchCount: metadata.launchCount,
            // Measured by this scan, not inherited: it pairs with byteCount.
            sizeMeasuredAt: scanned.sizeMeasuredAt,
            detectedCoverPath: scanned.detectedCoverPath,
            customCoverName: metadata.customCoverName,
            availability: scanned.availability,
            link: metadata.link,
            listEntry: metadata.listEntry
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
    /// Completed launches. Absent in libraries written before this field
    /// existed, so it decodes as zero rather than failing the whole document.
    public var launchCount: Int
    /// Modification date of the game folder when `byteCount` was measured.
    /// A scan reuses the stored size while this still matches, which avoids
    /// walking large game trees on every return to the foreground.
    public var sizeMeasuredAt: Date?
    public var detectedCoverPath: String?
    public var customCoverName: String?
    public var availability: GameAvailability
    /// The Bangumi and VNDB entries this game is linked to, if any.
    public var link: GameLink?
    /// Status, rating and dates; kept with or without a link.
    public var listEntry: ListEntry

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
        launchCount: Int = 0,
        sizeMeasuredAt: Date? = nil,
        detectedCoverPath: String? = nil,
        customCoverName: String? = nil,
        availability: GameAvailability = .unsupported,
        link: GameLink? = nil,
        listEntry: ListEntry = ListEntry()
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
        self.launchCount = launchCount
        self.sizeMeasuredAt = sizeMeasuredAt
        self.detectedCoverPath = detectedCoverPath
        self.customCoverName = customCoverName
        self.availability = availability
        self.link = link
        self.listEntry = listEntry
    }

    /// Hand-written so that fields added after a library was written decode to
    /// their default instead of failing the whole document.
    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        title = try container.decode(String.self, forKey: .title)
        engine = try container.decode(EngineID.self, forKey: .engine)
        folderName = try container.decode(String.self, forKey: .folderName)
        launchTargetKind = try container.decodeIfPresent(LaunchTargetKind.self, forKey: .launchTargetKind)
        launchTarget = try container.decodeIfPresent(String.self, forKey: .launchTarget)
        byteCount = try container.decode(Int64.self, forKey: .byteCount)
        addedAt = try container.decode(Date.self, forKey: .addedAt)
        lastPlayedAt = try container.decodeIfPresent(Date.self, forKey: .lastPlayedAt)
        playTime = try container.decode(TimeInterval.self, forKey: .playTime)
        launchCount = try container.decodeIfPresent(Int.self, forKey: .launchCount) ?? 0
        sizeMeasuredAt = try container.decodeIfPresent(Date.self, forKey: .sizeMeasuredAt)
        detectedCoverPath = try container.decodeIfPresent(String.self, forKey: .detectedCoverPath)
        customCoverName = try container.decodeIfPresent(String.self, forKey: .customCoverName)
        availability = try container.decode(GameAvailability.self, forKey: .availability)
        // A link that no longer decodes is dropped rather than failing the
        // library; the game can simply be matched again.
        link = try? container.decodeIfPresent(GameLink.self, forKey: .link)
        listEntry = (try? container.decodeIfPresent(ListEntry.self, forKey: .listEntry)) ?? ListEntry()
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
            return String(localized: "No supported game structure was found. Choose the engine manually.")
        case .unsafePath:
            return String(localized: "The game file path is outside the allowed folder.")
        case .symbolicLink:
            return String(localized: "The game folder contains symbolic links, which can’t be imported yet.")
        case .unsupportedSchema(let version):
            return String(localized: "Library version \(version) isn’t supported. Please update the app.")
        case .duplicateName(let name):
            return String(localized: "“\(name)” already exists in Documents. Rename the folder first.")
        case .invalidFolderName:
            return String(localized: "The game folder name is invalid.")
        case .engineMismatch(let expected, let detected):
            return String(localized: "The selected folder was recognized as \(detected.displayName) and can’t be linked to a \(expected.displayName) game.")
        case .recordNotFound:
            return String(localized: "The game record no longer exists.")
        }
    }
}

public enum GameScanner {
    private static let xp3Signature = Data([0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20, 0x0A, 0x1A, 0x8B, 0x67, 0x01])
    private static let preferredXP3Names = ["启动游戏.xp3", "startup.xp3", "start.xp3", "boot.xp3", "data.xp3"]
    /// Script files ONScripter Yuri opens, plain or obfuscated.
    private static let onscripterScriptNames: Set<String> = ["0.txt", "00.txt", "nscript.dat", "nscr_sec.dat", "onscript.nt2", "onscript.nt3"]
    private static let preferredCoverNames = ["cover", "folder", "poster", "thumbnail", "icon"]
    private static let coverExtensions: Set<String> = ["png", "jpg", "jpeg", "heic", "webp"]

    /// - Parameter reusableSize: a previously measured size to adopt instead of
    ///   walking the tree again. Callers pass this only when they have
    ///   established the directory has not changed; see `scan`.
    public static func inspectGame(
        at directory: URL,
        engine: EngineID,
        reusableSize: Int64? = nil
    ) throws -> GameRecord {
        let values = try directory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true else { throw LibraryError.noGame }
        guard values.isSymbolicLink != true else { throw LibraryError.symbolicLink }

        let launch = try launchTarget(in: directory, engine: engine)
        let availability: GameAvailability
        if !engine.isLaunchSupported {
            availability = .unsupported
        } else if launch == nil {
            switch engine {
            case .onscripter:
                availability = .invalid(String(localized: "No ONScripter script found (0.txt, 00.txt or nscript.dat)"))
            case .artemis:
                availability = .invalid(String(localized: "No Artemis data found (root.pfs or system.ini)"))
            case .renpy:
                availability = .invalid(String(localized: "No Ren’Py game found (a game folder with .rpy, .rpyc or .rpa files)"))
            case .siglus:
                availability = .invalid(String(localized: "No SiglusEngine data found (Scene.pck or Gameexe.dat)"))
            default:
                availability = .invalid(String(localized: "No valid startup.tjs or XP3 found"))
            }
        } else {
            availability = .ready
        }

        return GameRecord(
            title: directory.lastPathComponent,
            engine: engine,
            folderName: directory.lastPathComponent,
            launchTargetKind: launch?.kind,
            launchTarget: launch?.relativePath,
            byteCount: try reusableSize ?? byteCount(in: directory),
            detectedCoverPath: try detectedCover(in: directory),
            availability: availability
        )
    }

    public static func launchTarget(in directory: URL, engine: EngineID) throws -> (kind: LaunchTargetKind, relativePath: String)? {
        if engine == .onscripter {
            // ONScripter finds its script and archives in the game folder itself.
            let names = Set(try rootFiles(in: directory).map { normalized($0.lastPathComponent) })
            return names.isDisjoint(with: onscripterScriptNames) ? nil : (.directory, ".")
        }
        if engine == .artemis {
            // openArtemis opens root.pfs (or the folder's first pack, or an
            // extracted project with system.ini) from the game folder itself.
            return isArtemisGame(try rootFiles(in: directory)) ? (.directory, ".") : nil
        }
        if engine == .renpy {
            // Ren'Py runs the folder that holds game/ (or the .rpa archives
            // themselves).
            return try isRenPyGame(in: directory) ? (.directory, ".") : nil
        }
        if engine == .siglus {
            // siglus_rs finds Scene.pck and Gameexe.dat in the game folder.
            return try isSiglusGame(in: directory) ? (.directory, ".") : nil
        }
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
        if !names.isDisjoint(with: onscripterScriptNames.union(["arc.nsa"])) {
            matches.append(.onscripter)
        }
        if entries.contains(where: { normalized($0.pathExtension) == "pfs" }) {
            matches.append(.artemis)
        }

        if try isRenPyGame(in: directory) { matches.append(.renpy) }
        if try isSiglusGame(in: directory) { matches.append(.siglus) }

        return matches
    }

    private static func isRenPyGame(in directory: URL) throws -> Bool {
        let entries = try rootFiles(in: directory)
        let gameDirectory = directory.appendingPathComponent("game", isDirectory: true)
        let gameScripts = (try? rootFiles(in: gameDirectory)) ?? []
        let hasRenPyScript = gameScripts.contains {
            ["rpy", "rpyc"].contains(normalized($0.pathExtension))
        }
        let hasRenPyArchive = (entries + gameScripts).contains {
            normalized($0.pathExtension) == "rpa"
        }
        return hasRenPyScript || hasRenPyArchive
    }

    private static func isSiglusGame(in directory: URL) throws -> Bool {
        let names = Set(try rootFiles(in: directory).map { normalized($0.lastPathComponent) })
        return names.contains(normalized("Scene.pck")) || names.contains(normalized("Gameexe.dat"))
    }

    private static func isArtemisGame(_ entries: [URL]) -> Bool {
        entries.contains {
            normalized($0.pathExtension) == "pfs" || normalized($0.lastPathComponent) == "system.ini"
        }
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
        for component in relative.split(separator: "/") where component != "." {
            candidate.appendPathComponent(String(component))
            do {
                let attributes = try FileManager.default.attributesOfItem(atPath: candidate.path)
                if attributes[.type] as? FileAttributeType == .typeSymbolicLink {
                    throw LibraryError.unsafePath
                }
            } catch let error as NSError where error.domain == NSCocoaErrorDomain
                        && [NSFileNoSuchFileError, NSFileReadNoSuchFileError].contains(error.code) {
                // Missing leaves are valid when constructing a future destination.
                // Apple's Foundation reports them as NSFileReadNoSuchFileError.
            }
        }
        // Every component was checked above, so the candidate is inside the
        // base already. Standardizing it again could drop or keep iOS's
        // "/private" prefix depending on whether the file exists yet, which
        // made new files look like they escaped the base.
        guard candidate.path.hasPrefix(base.path + "/") else { throw LibraryError.unsafePath }
        return candidate
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

    /// Total size of the regular files under `directory`; zero when it
    /// does not exist.
    public static func directorySize(_ directory: URL) -> Int64 {
        guard FileManager.default.fileExists(atPath: directory.path) else { return 0 }
        return (try? byteCount(in: directory)) ?? 0
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
