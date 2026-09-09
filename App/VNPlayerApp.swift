import SwiftUI
import VNCore

@main
struct VNPlayerApp: App {
    @StateObject private var model = AppModel()
    var body: some Scene {
        WindowGroup {
            RootView().environmentObject(model)
                .preferredColorScheme(model.settings.appearance == "跟随系统" ? nil : model.settings.appearance == "浅色" ? .light : .dark)
                .tint(model.settings.tint)
        }
    }
}

struct PlayerSettings: Codable {
    var appearance = "深色"
    var accent = 0
    var renderer = "Metal 原生"
    var background = "游戏封面"
    var floatingButton = true
    var idleOpacity = 0.38
    var threeFingerMenu = true
    var performance = false
    var listLayout = false
    var sort = "最近游玩"
    var tint: Color { [Color(red: 0.60, green: 0.64, blue: 0.72), Color(red: 0.84, green: 0.73, blue: 0.55), Color(red: 0.51, green: 0.64, blue: 0.83)][min(max(accent, 0), 2)] }
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
    @Published var demo = false
    @Published var player: GameRecord?
    private var libraryReadable = true
    let repository: LibraryRepository
    static let sample = GameRecord(id: UUID(uuidString: "A84D1897-2F77-4DAD-9F38-69ACFFB1022B")!,
        title: "青空下的加缪", directory: "demo", entryPoint: ".", byteCount: 512124518)

    init() {
        settings = UserDefaults.standard.data(forKey: "settings.v1").flatMap { try? JSONDecoder().decode(PlayerSettings.self, from: $0) } ?? PlayerSettings()
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("VNPlayer")
        repository = LibraryRepository(root: root)
        do { games = try repository.load() }
        catch { libraryReadable = false; alert = "无法读取游戏库：\(error.localizedDescription)。原文件已保留。" }
        if ProcessInfo.processInfo.arguments.contains("--demo") { demo = true }
    }

    var displayedGames: [GameRecord] { demo ? [Self.sample] : games }
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
        demo ? Self.sample : games.filter { $0.lastPlayedAt != nil }.max { ($0.lastPlayedAt ?? .distantPast) < ($1.lastPlayedAt ?? .distantPast) }
    }
    var totalSize: String { Self.size(displayedGames.reduce(0) { $0 + $1.byteCount }) }
    static func size(_ count: Int64) -> String { ByteCountFormatter.string(fromByteCount: count, countStyle: .file) }
    func recency(_ game: GameRecord) -> String {
        if demo { return "7分钟前" }
        guard let date = game.lastPlayedAt else { return "尚未游玩" }
        let formatter = RelativeDateTimeFormatter(); formatter.locale = Locale(identifier: "zh_CN")
        return formatter.localizedString(for: date, relativeTo: Date())
    }
    func launch(_ game: GameRecord) {
        if demo { player = game; return }
        alert = "此构建尚未连接 KRKR 运行时，暂时不能运行游戏。已导入的文件会保留。可在“关于”中打开界面演示。"
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
            games.append(record); demo = false
        } catch { alert = error.localizedDescription }
    }
    func coverURL(_ game: GameRecord) -> URL? {
        guard let name = game.coverName else { return nil }
        return try? GameScanner.containedURL(name, in: repository.root.appendingPathComponent("Covers"))
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
