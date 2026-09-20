import SwiftUI
import VNCore

@main
struct MikageApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .preferredColorScheme(model.settings.appearance.colorScheme)
        }
    }
}

struct PendingGameImport: Identifiable {
    let id = UUID()
    let url: URL
    let detectedEngines: [EngineID]
}

@MainActor
final class AppModel: ObservableObject {
    @Published var games: [GameRecord] = []
    @Published var missingGames: [GameRecord] = []
    @Published var settings: PlayerSettings {
        didSet {
            AppDiagnostics.shared.event("settings", "preferences.changed", ["renderer": settings.renderer.rawValue, "appearance": settings.appearance.rawValue, "performance": String(settings.performance)])
            if let data = Self.encoded(settings) {
                defaults.set(data, forKey: Self.settingsKey)
            }
        }
    }
    @Published var tab = 0
    @Published var query = ""
    @Published var importing = false
    @Published var scanning = false
    @Published var alert: String? {
        didSet {
            if let alert { AppDiagnostics.shared.event("app", "user.error", ["message": alert]) }
        }
    }
    @Published var player: GameRecord?
    @Published var pendingImport: PendingGameImport?

    private var libraryReadable = true
    let repository: LibraryRepository
    let krkrSession: any KRKRSession
    private let defaults: UserDefaults

    private static let settingsKey = "settings.v1"

    /// The on-device layout: visible game folders in Documents, hidden identity
    /// and metadata in Application Support.
    static func appRepository() -> LibraryRepository {
        let fileManager = FileManager.default
        let documents = fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let applicationSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Mikage", isDirectory: true)
        return LibraryRepository(documentsRoot: documents, metadataRoot: applicationSupport)
    }

    /// Sorted keys keep the encoded form stable so the normalizing rewrite in
    /// `init` only fires when a value actually changed.
    private static func encoded(_ settings: PlayerSettings) -> Data? {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .sortedKeys
        return try? encoder.encode(settings)
    }

    /// - Parameter configureDiagnostics: disabled by tests, which must not
    ///   install the process-wide log callback or touch the real log directory.
    init(
        repository: LibraryRepository? = nil,
        session: (any KRKRSession)? = nil,
        defaults: UserDefaults = .standard,
        configureDiagnostics: Bool = true
    ) {
        self.defaults = defaults
        self.repository = repository ?? Self.appRepository()
        krkrSession = session ?? NativeKRKRSession()

        let stored = defaults.data(forKey: Self.settingsKey)
        let loadedSettings = stored
            .flatMap { try? JSONDecoder().decode(PlayerSettings.self, from: $0) }
            ?? PlayerSettings()
        // Decoding accepts display text persisted by earlier versions; rewrite
        // the file once so the stored form is always the stable raw values.
        let normalized = Self.encoded(loadedSettings)
        if let normalized, normalized != stored {
            defaults.set(normalized, forKey: Self.settingsKey)
        }

        settings = loadedSettings

        if configureDiagnostics {
            do {
                try AppDiagnostics.shared.configure(enabled: defaults.bool(forKey: AppDiagnostics.preferenceKey))
            } catch {
                defaults.set(false, forKey: AppDiagnostics.preferenceKey)
                alert = "无法启动日志记录：\(error.localizedDescription)"
            }
            AppDiagnostics.shared.event("app", "launch")
        }

        do {
            try self.repository.ensureStructure()
            let records = try self.repository.load()
            games = records.filter { $0.availability != .missing }
            missingGames = records.filter { $0.availability == .missing }
        } catch {
            libraryReadable = false
            AppDiagnostics.shared.event("library", "initialization.failed", ["error": error.localizedDescription])
            alert = """
                无法读取游戏库：\(error.localizedDescription)
                原文件已保留在 Application Support/Mikage/library-v2.json。下拉刷新可重试。
                """
        }
    }

    /// Re-attempts the load that failed at launch. Returns false while the
    /// library is still unreadable, which keeps the mutating operations
    /// disabled rather than letting them write over a file we cannot parse.
    @discardableResult
    private func retryLoadIfNeeded() -> Bool {
        if libraryReadable { return true }
        do {
            let records = try repository.load()
            games = records.filter { $0.availability != .missing }
            missingGames = records.filter { $0.availability == .missing }
            libraryReadable = true
            AppDiagnostics.shared.event("library", "recovery.succeeded")
            return true
        } catch {
            AppDiagnostics.shared.event("library", "recovery.failed", ["error": error.localizedDescription])
            return false
        }
    }

    var displayedGames: [GameRecord] { games }

    var filteredGames: [GameRecord] {
        displayedGames
            .filter { query.isEmpty || $0.title.localizedCaseInsensitiveContains(query) }
            .sorted {
                switch settings.sort {
                case .title:
                    return $0.title.localizedStandardCompare($1.title) == .orderedAscending
                case .size:
                    return $0.byteCount > $1.byteCount
                case .dateAdded:
                    return $0.addedAt > $1.addedAt
                case .recentlyPlayed:
                    return ($0.lastPlayedAt ?? .distantPast) > ($1.lastPlayedAt ?? .distantPast)
                }
            }
    }

    var latest: GameRecord? {
        games
            .filter { $0.lastPlayedAt != nil && $0.availability.canLaunch }
            .max {
                ($0.lastPlayedAt ?? .distantPast) < ($1.lastPlayedAt ?? .distantPast)
            }
    }

    var totalSize: String {
        Self.size(displayedGames.reduce(0) { $0 + $1.byteCount })
    }

    static func size(_ count: Int64) -> String {
        ByteCountFormatter.string(fromByteCount: count, countStyle: .file)
    }

    func recency(_ game: GameRecord) -> String {
        guard let date = game.lastPlayedAt else { return "尚未游玩" }
        let formatter = RelativeDateTimeFormatter()
        formatter.locale = Locale(identifier: "zh_CN")
        return formatter.localizedString(for: date, relativeTo: Date())
    }

    func availabilityText(_ game: GameRecord) -> String {
        switch game.availability {
        case .ready:
            return game.launchTargetKind == .archive
                ? "启动：\(game.launchTarget ?? "XP3")"
                : "可以启动"
        case .invalid(let reason):
            return reason
        case .unsupported:
            return "\(game.engine.displayName) 引擎尚未接入"
        case .missing:
            return "文件夹已移除"
        }
    }

    func launch(_ game: GameRecord) {
        AppDiagnostics.shared.event("library", "launch.requested", ["engine": game.engine.rawValue, "folder": game.folderName])
        guard game.engine == .kirikiri, game.availability.canLaunch else {
            alert = availabilityText(game)
            return
        }
        player = game
    }

    func launchConfiguration(for game: GameRecord) throws -> KRKRLaunchConfiguration {
        guard game.engine == .kirikiri, game.availability.canLaunch else {
            throw LibraryError.noGame
        }
        let directory = try repository.directory(for: game)
        let entryPoint = try repository.entryPoint(for: game)
        return KRKRLaunchConfiguration(
            gameDirectory: directory,
            entryPoint: entryPoint,
            targetKind: game.launchTargetKind ?? .directory,
            renderer: settings.renderer,
            floatingButton: settings.floatingButton,
            idleOpacity: settings.idleOpacity,
            threeFingerMenu: settings.threeFingerMenu,
            performance: settings.performance,
            gameTitle: game.title,
            skippedMovies: SkippedMovies.runtimeList(enabled: settings.skipPatchVideos)
        )
    }

    func refreshLibrary(showErrors: Bool = true) async {
        guard !scanning else { return }
        // A pull-to-refresh is the retry path out of a failed launch load.
        guard retryLoadIfNeeded() else {
            if showErrors {
                alert = "游戏库文件仍无法读取。请检查 Application Support/Mikage/library-v2.json。"
            }
            return
        }
        scanning = true
        AppDiagnostics.shared.event("library", "scan.begin")
        defer { scanning = false }
        do {
            let repository = self.repository
            let snapshot = try await Task.detached(priority: .userInitiated) {
                try repository.scan()
            }.value
            apply(snapshot)
            AppDiagnostics.shared.event("library", "scan.end", ["active": String(snapshot.active.count), "missing": String(snapshot.missing.count)])
        } catch {
            AppDiagnostics.shared.event("library", "scan.failed", ["error": error.localizedDescription])
            if showErrors {
                alert = "扫描游戏目录失败：\(error.localizedDescription)"
            }
        }
    }

    func beginImport(_ url: URL) async {
        AppDiagnostics.shared.event("library", "import.begin", ["folder": url.lastPathComponent])
        guard !importing, libraryReadable else { return }
        importing = true
        let scoped = url.startAccessingSecurityScopedResource()
        defer {
            if scoped { url.stopAccessingSecurityScopedResource() }
            importing = false
        }

        do {
            let detected = try await Task.detached(priority: .userInitiated) {
                try GameScanner.detectEngines(in: url)
            }.value
            if detected.count == 1, let engine = detected.first {
                AppDiagnostics.shared.event("library", "import.engineDetected", ["engine": engine.rawValue])
                let repository = self.repository
                try await Task.detached(priority: .userInitiated) {
                    try repository.importFolder(url, engine: engine)
                }.value
                await refreshLibrary()
            } else {
                AppDiagnostics.shared.event("library", "import.engineSelectionRequired", ["matches": String(detected.count)])
                pendingImport = PendingGameImport(url: url, detectedEngines: detected)
            }
        } catch {
            alert = error.localizedDescription
        }
    }

    func importPending(_ pendingImport: PendingGameImport, as engine: EngineID) async {
        AppDiagnostics.shared.event("library", "import.engineSelected", ["folder": pendingImport.url.lastPathComponent, "engine": engine.rawValue])
        guard !importing, libraryReadable else { return }
        self.pendingImport = nil
        importing = true
        let url = pendingImport.url
        let scoped = url.startAccessingSecurityScopedResource()
        defer {
            if scoped { url.stopAccessingSecurityScopedResource() }
            importing = false
        }
        do {
            let repository = self.repository
            try await Task.detached(priority: .userInitiated) {
                try repository.importFolder(url, engine: engine)
            }.value
            await refreshLibrary()
        } catch {
            alert = error.localizedDescription
        }
    }

    func relink(_ game: GameRecord, to url: URL) async {
        AppDiagnostics.shared.event("library", "relink.requested", ["oldFolder": game.folderName, "newFolder": url.lastPathComponent])
        guard !importing, libraryReadable else { return }
        importing = true
        let scoped = url.startAccessingSecurityScopedResource()
        defer {
            if scoped { url.stopAccessingSecurityScopedResource() }
            importing = false
        }
        do {
            let repository = self.repository
            try await Task.detached(priority: .userInitiated) {
                try repository.relink(game, to: url)
            }.value
            await refreshLibrary()
        } catch {
            alert = error.localizedDescription
        }
    }

    func forget(_ game: GameRecord) async {
        AppDiagnostics.shared.event("library", "recordDeletion.requested", ["folder": game.folderName])
        guard libraryReadable else { return }
        do {
            let repository = self.repository
            try await Task.detached(priority: .userInitiated) {
                try repository.forget(game)
            }.value
            await refreshLibrary()
        } catch {
            alert = error.localizedDescription
        }
    }

    /// Adds one session to the game's history.
    ///
    /// Re-reads the file under the repository lock instead of overwriting it
    /// with this actor's in-memory list: a background scan may have saved newer
    /// records since the game launched, and rebuilding from `games` would drop
    /// them. Only the two history fields of the one record are touched.
    func recordPlayback(of game: GameRecord, duration: TimeInterval) {
        AppDiagnostics.shared.event("library", "history.save", ["folder": game.folderName, "duration": String(duration)])
        guard libraryReadable else { return }
        do {
            let updated = try repository.mutate { records -> GameRecord? in
                guard let index = records.firstIndex(where: { $0.id == game.id }) else {
                    return nil
                }
                records[index].lastPlayedAt = Date()
                records[index].playTime += max(duration, 0)
                records[index].launchCount += 1
                return records[index]
            }
            guard let updated else {
                // The folder was forgotten or relinked while the game ran.
                AppDiagnostics.shared.event("library", "history.recordMissing", ["folder": game.folderName])
                alert = "游玩记录未能保存：“\(game.title)”的记录已不存在。"
                return
            }
            if let activeIndex = games.firstIndex(where: { $0.id == game.id }) {
                games[activeIndex] = updated
            }
        } catch {
            alert = "游玩记录保存失败：\(error.localizedDescription)"
        }
    }

    func coverURL(_ game: GameRecord) -> URL? {
        repository.coverURL(for: game)
    }

    /// Encodes and writes off the main thread: a picked photo can be several
    /// thousand pixels on a side, and JPEG encoding it on the MainActor freezes
    /// the library while the user waits.
    func updateCover(_ game: GameRecord, image: UIImage) async {
        AppDiagnostics.shared.event("library", "cover.update", ["folder": game.folderName])
        guard libraryReadable else { return }
        do {
            let repository = self.repository
            let id = game.id
            let updated = try await Task.detached(priority: .userInitiated) {
                guard let data = image.jpegData(compressionQuality: 0.85) else {
                    throw LibraryError.noGame
                }
                return try repository.setCustomCover(for: id, jpegData: data)
            }.value
            CoverCache.shared.invalidateAll()
            if let index = games.firstIndex(where: { $0.id == id }) {
                games[index] = updated
            }
        } catch {
            alert = error.localizedDescription
        }
    }

    private func apply(_ snapshot: LibrarySnapshot) {
        games = snapshot.active
        missingGames = snapshot.missing
    }
}
