import SwiftUI
import VNCore

@main
struct MikageApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .preferredColorScheme(
                    model.settings.appearance == "跟随系统"
                        ? nil
                        : model.settings.appearance == "浅色" ? .light : .dark
                )
        }
    }
}

struct PlayerSettings: Codable {
    var appearance = "深色"
    var renderer = "Metal"
    var background = "游戏封面"
    var floatingButton = true
    var idleOpacity = 0.38
    var threeFingerMenu = true
    var performance = false
    var listLayout = false
    var sort = "最近游玩"
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
            AppDiagnostics.shared.event("settings", "preferences.changed", ["renderer": settings.renderer, "appearance": settings.appearance, "performance": String(settings.performance)])
            if let data = try? JSONEncoder().encode(settings) {
                UserDefaults.standard.set(data, forKey: "settings.v1")
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
    let krkrSession = NativeKRKRSession()

    init() {
        var loadedSettings = UserDefaults.standard.data(forKey: "settings.v1")
            .flatMap { try? JSONDecoder().decode(PlayerSettings.self, from: $0) }
            ?? PlayerSettings()
        if loadedSettings.renderer == "Metal 原生" {
            loadedSettings.renderer = "Metal"
            if let data = try? JSONEncoder().encode(loadedSettings) {
                UserDefaults.standard.set(data, forKey: "settings.v1")
            }
        }

        settings = loadedSettings

        let fileManager = FileManager.default
        let documents = fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let applicationSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Mikage", isDirectory: true)
        repository = LibraryRepository(documentsRoot: documents, metadataRoot: applicationSupport)
        do {
            try AppDiagnostics.shared.configure(enabled: UserDefaults.standard.bool(forKey: AppDiagnostics.preferenceKey))
        } catch {
            UserDefaults.standard.set(false, forKey: AppDiagnostics.preferenceKey)
            alert = "无法启动日志记录：\(error.localizedDescription)"
        }
        AppDiagnostics.shared.event("app", "launch")

        do {
            try repository.ensureStructure()
            let records = try repository.load()
            games = records.filter { $0.availability != .missing }
            missingGames = records.filter { $0.availability == .missing }
        } catch {
            libraryReadable = false
            AppDiagnostics.shared.event("library", "initialization.failed", ["error": error.localizedDescription])
            alert = "无法读取游戏库：\(error.localizedDescription)。原文件已保留。"
        }
    }

    var displayedGames: [GameRecord] { games }

    var filteredGames: [GameRecord] {
        displayedGames
            .filter { query.isEmpty || $0.title.localizedCaseInsensitiveContains(query) }
            .sorted {
                switch settings.sort {
                case "名称":
                    return $0.title.localizedStandardCompare($1.title) == .orderedAscending
                case "大小":
                    return $0.byteCount > $1.byteCount
                case "添加时间":
                    return $0.addedAt > $1.addedAt
                default:
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
        let renderer: String
        switch settings.renderer {
        case "OpenGL ES": renderer = "opengl"
        case "软件合成 · Metal": renderer = "software-metal"
        default: renderer = "metal"
        }
        return KRKRLaunchConfiguration(
            gameDirectory: directory,
            entryPoint: entryPoint,
            targetKind: game.launchTargetKind ?? .directory,
            renderer: renderer,
            floatingButton: settings.floatingButton,
            idleOpacity: settings.idleOpacity,
            threeFingerMenu: settings.threeFingerMenu,
            performance: settings.performance,
            gameTitle: game.title
        )
    }

    func refreshLibrary(showErrors: Bool = true) async {
        guard libraryReadable, !scanning else { return }
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

    func recordPlayback(of game: GameRecord, duration: TimeInterval) {
        AppDiagnostics.shared.event("library", "history.save", ["folder": game.folderName, "duration": String(duration)])
        var records = games + missingGames
        guard let index = records.firstIndex(where: { $0.id == game.id }) else { return }
        records[index].lastPlayedAt = Date()
        records[index].playTime += max(duration, 0)
        do {
            try repository.save(records)
            if let activeIndex = games.firstIndex(where: { $0.id == game.id }) {
                games[activeIndex] = records[index]
            }
        } catch {
            alert = "游玩记录保存失败：\(error.localizedDescription)"
        }
    }

    func coverURL(_ game: GameRecord) -> URL? {
        repository.coverURL(for: game)
    }

    func updateCover(_ game: GameRecord, image: UIImage) {
        AppDiagnostics.shared.event("library", "cover.update", ["folder": game.folderName])
        guard libraryReadable, let data = image.jpegData(compressionQuality: 0.85) else { return }
        do {
            apply(try repository.setCustomCover(for: game.id, jpegData: data))
        } catch {
            alert = error.localizedDescription
        }
    }

    private func apply(_ snapshot: LibrarySnapshot) {
        games = snapshot.active
        missingGames = snapshot.missing
    }
}
