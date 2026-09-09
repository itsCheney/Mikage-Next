import SwiftUI
import VNCore

@main
struct MikageApp: App {
    @StateObject private var model = AppModel()
    var body: some Scene {
        WindowGroup {
            RootView().environmentObject(model)
                .preferredColorScheme(model.settings.appearance == "跟随系统" ? nil : model.settings.appearance == "浅色" ? .light : .dark)
        }
    }
}

struct PlayerSettings: Codable {
    var appearance = "深色"
    var renderer = "Metal 原生"
    var background = "游戏封面"
    var floatingButton = true
    var idleOpacity = 0.38
    var threeFingerMenu = true
    var performance = false
    var listLayout = false
    var sort = "最近游玩"
}

@MainActor
final class AppModel: ObservableObject {
    @Published var games: [GameRecord] = []
    @Published var settings: PlayerSettings {
        didSet { if let data = try? JSONEncoder().encode(settings) { UserDefaults.standard.set(data, forKey: "settings.v1") } }
    }
    @Published var tab = 0
    @Published var query = ""
    @Published var importing = false
    @Published var alert: String?
    @Published var player: GameRecord?
    private var libraryReadable = true
    let repository: LibraryRepository
    let krkrSession = NativeKRKRSession()

    init() {
        settings = UserDefaults.standard.data(forKey: "settings.v1").flatMap { try? JSONDecoder().decode(PlayerSettings.self, from: $0) } ?? PlayerSettings()
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Mikage")
        repository = LibraryRepository(root: root)
        do { games = try repository.load() }
        catch { libraryReadable = false; alert = "无法读取游戏库：\(error.localizedDescription)。原文件已保留。" }
        if ProcessInfo.processInfo.arguments.contains("--krkr-smoke") {
            prepareKRKRSmokeLibrary()
        }
    }

    var displayedGames: [GameRecord] { games }
    var filteredGames: [GameRecord] {
        displayedGames.filter { query.isEmpty || $0.title.localizedCaseInsensitiveContains(query) }.sorted {
            switch settings.sort {
            case "名称": return $0.title.localizedStandardCompare($1.title) == .orderedAscending
            case "大小": return $0.byteCount > $1.byteCount
            case "添加时间": return $0.addedAt > $1.addedAt
            default: return ($0.lastPlayedAt ?? .distantPast) > ($1.lastPlayedAt ?? .distantPast)
            }
        }
    }
    var latest: GameRecord? {
        games.filter { $0.lastPlayedAt != nil }.max { ($0.lastPlayedAt ?? .distantPast) < ($1.lastPlayedAt ?? .distantPast) }
    }
    var totalSize: String { Self.size(displayedGames.reduce(0) { $0 + $1.byteCount }) }
    static func size(_ count: Int64) -> String { ByteCountFormatter.string(fromByteCount: count, countStyle: .file) }
    func recency(_ game: GameRecord) -> String {
        guard let date = game.lastPlayedAt else { return "尚未游玩" }
        let formatter = RelativeDateTimeFormatter(); formatter.locale = Locale(identifier: "zh_CN")
        return formatter.localizedString(for: date, relativeTo: Date())
    }
    func launch(_ game: GameRecord) {
        guard game.engine == "kirikiri" else {
            alert = "该作品不是 KiriKiri 游戏，当前版本暂不启动其他引擎。"
            return
        }
        player = game
    }

    func launchConfiguration(for game: GameRecord) throws -> KRKRLaunchConfiguration {
        let directory = try repository.directory(for: game)
        let entryPoint = game.entryPoint == "."
            ? directory
            : try GameScanner.containedURL(game.entryPoint, in: directory)
        let renderer = settings.renderer == "OpenGL ES" ? "opengl" : "metal"
        return KRKRLaunchConfiguration(
            gameDirectory: directory,
            entryPoint: entryPoint,
            renderer: renderer,
            floatingButton: settings.floatingButton,
            idleOpacity: settings.idleOpacity,
            threeFingerMenu: settings.threeFingerMenu
        )
    }

    func recordPlayback(of game: GameRecord, duration: TimeInterval) {
        guard let index = games.firstIndex(where: { $0.id == game.id }) else { return }
        var updated = games
        updated[index].lastPlayedAt = Date()
        updated[index].playTime += max(duration, 0)
        do {
            try repository.save(updated)
            games = updated
        } catch {
            alert = "游玩记录保存失败：\(error.localizedDescription)"
        }
    }
    func importFolder(_ url: URL) async {
        guard !importing, libraryReadable else { return }
        importing = true
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() }; importing = false }
        do {
            let repository = self.repository
            let record = try await Task.detached(priority: .userInitiated) {
                try repository.importFolder(url)
            }.value
            do { try repository.save(games + [record]) }
            catch { try? repository.removeImportedDirectory(for: record); throw error }
            games.append(record)
        } catch { alert = error.localizedDescription }
    }
    func coverURL(_ game: GameRecord) -> URL? {
        guard let name = game.coverName else { return nil }
        return try? GameScanner.containedURL(name, in: repository.root.appendingPathComponent("Covers"))
    }
    private func prepareKRKRSmokeLibrary() {
        let fixtures = [
            (UUID(uuidString: "11111111-1111-1111-1111-111111111111")!, "KRKR Smoke A"),
            (UUID(uuidString: "22222222-2222-2222-2222-222222222222")!, "KRKR Smoke B")
        ]
        do {
            try FileManager.default.createDirectory(at: repository.gamesURL, withIntermediateDirectories: true)
            games = try fixtures.map { fixture in
                let (id, title) = fixture
                let directory = repository.gamesURL.appendingPathComponent(id.uuidString, isDirectory: true)
                try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                let startup = directory.appendingPathComponent("startup.tjs")
                if !FileManager.default.fileExists(atPath: startup.path) {
                    try Data("// Mikage KRKR lifecycle smoke test\n".utf8).write(to: startup, options: .atomic)
                }
                return GameRecord(id: id, title: title, directory: id.uuidString, entryPoint: ".")
            }
        } catch {
            alert = "KRKR smoke test fixture 创建失败：\(error.localizedDescription)"
        }
    }
    func updateCover(_ game: GameRecord, image: UIImage) {
        guard libraryReadable, let index = games.firstIndex(where: { $0.id == game.id }),
              let data = image.jpegData(compressionQuality: 0.85) else { return }
        do {
            let folder = repository.root.appendingPathComponent("Covers")
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let name = "\(UUID().uuidString).jpg"
            let target = folder.appendingPathComponent(name)
            try data.write(to: target, options: .atomic)
            var updated = games; updated[index].coverName = name
            do { try repository.save(updated) } catch { try? FileManager.default.removeItem(at: target); throw error }
            let previous = coverURL(game)
            games = updated
            if let previous { try? FileManager.default.removeItem(at: previous) }
        } catch { alert = error.localizedDescription }
    }
}
