import SwiftUI
import PhotosUI
import UniformTypeIdentifiers
import VNCore

struct RootView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        tabs
            .fullScreenCover(item: $model.player) { game in
                PlayerView(game: game).environmentObject(model)
            }
            .task {
                await model.refreshLibrary(showErrors: false)
            }
            .onChange(of: scenePhase) { phase in
                AppDiagnostics.shared.event("app", "scenePhase.changed", ["phase": String(describing: phase)])
                if phase == .active {
                    Task { await model.refreshLibrary(showErrors: false) }
                }
            }
            .alert("提示", isPresented: Binding(
                get: { model.alert != nil },
                set: { if !$0 { model.alert = nil } }
            )) {
                Button("好", role: .cancel) { model.alert = nil }
            } message: {
                Text(model.alert ?? "")
            }
    }

    /// iOS 18 and later get the floating tab bar with a separate search tab,
    /// which becomes a sidebar on iPad. Earlier systems keep a classic tab bar
    /// with the same three destinations.
    @ViewBuilder
    private var tabs: some View {
        if #available(iOS 18.0, *) {
            TabView(selection: $model.tab) {
                Tab("游戏库", systemImage: "books.vertical", value: 0) {
                    LibraryView()
                }
                Tab("设置", systemImage: "gearshape", value: 1) {
                    SettingsView()
                }
                Tab(value: 2, role: .search) {
                    LibrarySearchView()
                }
            }
            .tabViewStyle(.sidebarAdaptable)
        } else {
            TabView(selection: $model.tab) {
                LibraryView()
                    .tabItem { Label("游戏库", systemImage: "books.vertical.fill") }
                    .tag(0)
                SettingsView()
                    .tabItem { Label("设置", systemImage: "gearshape.fill") }
                    .tag(1)
                LibrarySearchView()
                    .tabItem { Label("搜索", systemImage: "magnifyingglass") }
                    .tag(2)
            }
        }
    }
}

struct LibraryView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.horizontalSizeClass) private var sizeClass
    @State private var importPicker = false
    @State private var showMissingGames = false

    private var regular: Bool { sizeClass == .regular }

    /// Everything that is not waiting on an engine runtime. Games with a broken
    /// launch target stay here, disabled, with the reason under the title.
    private var playableGames: [GameRecord] {
        model.sortedGames.filter { $0.availability != .unsupported }
    }

    /// Recognised engines whose runtime has not landed yet.
    private var comingSoonGames: [GameRecord] {
        model.sortedGames.filter { $0.availability == .unsupported }
    }

    /// Height of the pinned header, including the status bar area.
    @State private var headerHeight: CGFloat = 0

    var body: some View {
        NavigationStack {
            GeometryReader { proxy in
                let topInset = proxy.safeAreaInsets.top

                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        if let latest = model.latest {
                            continueCard(latest, topInset: topInset)
                        } else {
                            Color.clear.frame(height: headerHeight)
                        }

                        VStack(alignment: .leading, spacing: 12) {
                            if model.importing || model.scanning {
                                HStack {
                                    ProgressView()
                                    Text(model.importing ? "正在复制游戏文件…" : "正在扫描游戏目录…")
                                }
                                .font(.subheadline)
                            }

                            if model.displayedGames.isEmpty {
                                emptyLibrary
                            } else {
                                sections
                            }
                        }
                        .padding(.horizontal, 16)
                        .padding(.top, 18)
                        .padding(.bottom, 24)
                    }
                }
                .refreshable {
                    await model.refreshLibrary()
                }
                // Content runs under the status bar and the pinned header.
                .ignoresSafeArea(edges: .top)
                .overlay(alignment: .top) {
                    header(topInset: topInset)
                        .background(
                            GeometryReader { header in
                                Color.clear.preference(key: HeaderHeightKey.self, value: header.size.height)
                            }
                        )
                        .ignoresSafeArea(edges: .top)
                }
                .onPreferenceChange(HeaderHeightKey.self) { headerHeight = $0 }
            }
            .background(AppBackground())
            .toolbar(.hidden, for: .navigationBar)
        }
        .fileImporter(isPresented: $importPicker, allowedContentTypes: [.folder]) { result in
            switch result {
            case .success(let url):
                Task { await model.beginImport(url) }
            case .failure(let error):
                model.alert = error.localizedDescription
            }
        }
        .sheet(isPresented: $showMissingGames) {
            MissingGamesView().environmentObject(model)
        }
        .confirmationDialog(
            "选择游戏引擎",
            isPresented: Binding(
                get: { model.pendingImport != nil },
                set: { if !$0 { model.pendingImport = nil } }
            ),
            titleVisibility: .visible,
            presenting: model.pendingImport
        ) { pending in
            ForEach(EngineID.allCases) { engine in
                Button(engine.displayName) {
                    Task { await model.importPending(pending, as: engine) }
                }
            }
            Button("取消", role: .cancel) {
                model.pendingImport = nil
            }
        } message: { pending in
            if pending.detectedEngines.isEmpty {
                Text("未能自动识别“\(pending.url.lastPathComponent)”，请选择它所属的引擎。")
            } else {
                Text("检测到多个可能的引擎，请确认“\(pending.url.lastPathComponent)”的类型。")
            }
        }
    }

    // MARK: Header

    /// Pinned like the Photos library header: the large title and its count on
    /// the left, glass buttons on the same row, over a blur that fades out
    /// downward so covers and cards scroll softly underneath.
    private func header(topInset: CGFloat) -> some View {
        HStack(alignment: .center, spacing: 12) {
            VStack(alignment: .leading, spacing: 0) {
                Text("游戏库")
                    .font(.largeTitle.bold())
                    .accessibilityAddTraits(.isHeader)
                Text("\(model.displayedGames.count) 部作品")
                    .font(.subheadline.weight(.semibold))
            }
            Spacer(minLength: 0)
            headerButtons
        }
        .foregroundStyle(.white)
        .padding(.leading, 20)
        .padding(.trailing, 16)
        .padding(.top, topInset + 6)
        .padding(.bottom, 22)
        .frame(maxWidth: .infinity)
        .background(alignment: .top) {
            ProgressiveHeaderBlur()
        }
        .environment(\.colorScheme, .dark)
    }

    private var headerButtons: some View {
        HStack(spacing: 0) {
            if !model.missingGames.isEmpty {
                Button {
                    showMissingGames = true
                } label: {
                    Image(systemName: "exclamationmark.folder")
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("缺失游戏 \(model.missingGames.count)")
                .accessibilityIdentifier("missing-games-button")
            }
            viewMenu
            importMenu
        }
        .font(.title3.weight(.medium))
        .tint(Color.primary)
        .nativeGlassCapsule()
    }

    /// The last played game fills the top of the library under the pinned
    /// header, with Resume as the one prominent action.
    private func continueCard(_ game: GameRecord, topInset: CGFloat) -> some View {
        ZStack(alignment: .bottom) {
            GameCover(game: game)
                .accessibilityHidden(true)
            LinearGradient(
                stops: [
                    .init(color: .clear, location: 0.45),
                    .init(color: .black.opacity(0.72), location: 1)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
            .allowsHitTesting(false)

            HStack(alignment: .bottom, spacing: 12) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("继续上次")
                        .font(.caption.weight(.semibold))
                        .tracking(2)
                        .opacity(0.9)
                    Text(game.title)
                        .font(regular ? .largeTitle.bold() : .title2.bold())
                        .lineLimit(2)
                    Text("\(model.recency(game)) · 已玩 \(game.playTime.playTimeSummary)")
                        .font(regular ? .subheadline : .footnote)
                        .opacity(0.88)
                        .lineLimit(1)
                }
                Spacer(minLength: 0)
                Button { model.launch(game) } label: {
                    Label("继续", systemImage: "play.fill")
                        .font(.headline)
                        .padding(.horizontal, 4)
                }
                .nativeGlassButtonStyle(prominent: true)
                .controlSize(.large)
                .accessibilityLabel("继续上次：\(game.title)")
            }
            .foregroundStyle(.white)
            .padding(.horizontal, regular ? 28 : 20)
            .padding(.bottom, regular ? 26 : 22)
        }
        .frame(height: (regular ? 380 : 266) + topInset)
        .environment(\.colorScheme, .dark)
    }

    private var viewMenu: some View {
        Menu {
            Section("显示方式") {
                Picker("显示方式", selection: $model.settings.listLayout) {
                    Label("网格", systemImage: "square.grid.2x2").tag(false)
                    Label("列表", systemImage: "list.bullet").tag(true)
                }
            }
            Section("排序方式") {
                Picker("排序方式", selection: $model.settings.sort) {
                    ForEach(LibrarySort.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
            }
        } label: {
            Label("显示与排序", systemImage: "ellipsis")
                .labelStyle(.iconOnly)
        }
        .accessibilityLabel("显示与排序")
    }

    private var importMenu: some View {
        Menu {
            Button { importPicker = true } label: {
                Label("导入文件夹", systemImage: "folder.badge.plus")
            }
            Button {
                model.alert = "ZIP 解压与局域网上传尚未接入。也可以直接把游戏文件夹放入 Documents 下的 krkr、ons、renpy 或 artemis 目录。"
            } label: {
                Label("ZIP 与 Wi-Fi 导入", systemImage: "wifi")
            }
        } label: {
            Label("添加游戏", systemImage: "plus")
                .labelStyle(.iconOnly)
        }
        .disabled(model.importing)
        .accessibilityLabel("添加游戏")
    }

    // MARK: Sections

    @ViewBuilder
    private var sections: some View {
        let playable = playableGames
        let comingSoon = comingSoonGames

        if !playable.isEmpty {
            HStack(alignment: .firstTextBaseline) {
                Text("全部作品")
                    .font(.title2.bold())
                    .accessibilityAddTraits(.isHeader)
                Spacer()
                Text("\(playable.count) 部 · \(AppModel.size(playable.reduce(0) { $0 + $1.byteCount }))")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            GameGrid(games: playable)
        }

        if !comingSoon.isEmpty {
            VStack(alignment: .leading, spacing: 3) {
                Text("即将支持")
                    .font(.title2.bold())
                    .accessibilityAddTraits(.isHeader)
                Text("已识别引擎，运行时接入后即可游玩")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .padding(.top, playable.isEmpty ? 0 : 12)
            GameGrid(games: comingSoon)
        }
    }

    @ViewBuilder
    private var emptyLibrary: some View {
        if #available(iOS 17.0, *) {
            ContentUnavailableView {
                Label("故事，从这里开始", systemImage: "books.vertical")
            } description: {
                Text("把游戏文件夹放入 Documents/krkr、ons、renpy 或 artemis，返回 App 后会自动扫描。")
            } actions: {
                Button("导入游戏") { importPicker = true }
                    .buttonStyle(.borderedProminent)
                    .disabled(model.importing)
            }
            .padding(.top, 60)
        } else {
            VStack(spacing: 15) {
                Label("故事，从这里开始", systemImage: "books.vertical")
                    .font(.title3.weight(.semibold))
                Text("把游戏文件夹放入 Documents/krkr、ons、renpy 或 artemis，返回 App 后会自动扫描。")
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                Button("导入游戏") { importPicker = true }
                    .buttonStyle(.borderedProminent)
                    .disabled(model.importing)
            }
            .frame(maxWidth: .infinity)
            .padding(.top, 60)
        }
    }
}

private struct HeaderHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

/// A blur that is full strength under the status bar and fades to nothing at
/// the bottom of the header, with a dark scrim so white text stays legible
/// over any cover or card.
private struct ProgressiveHeaderBlur: View {
    var body: some View {
        ZStack {
            Rectangle()
                .fill(.ultraThinMaterial)
                .mask(
                    LinearGradient(
                        stops: [
                            .init(color: .black, location: 0),
                            .init(color: .black, location: 0.45),
                            .init(color: .clear, location: 1)
                        ],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
            LinearGradient(
                colors: [.black.opacity(0.5), .black.opacity(0.2), .clear],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        .allowsHitTesting(false)
    }
}

/// The search tab: every game, narrowed by the query typed in the tab bar.
struct LibrarySearchView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    if model.filteredGames.isEmpty {
                        emptyResults
                    } else {
                        GameGrid(games: model.filteredGames)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 24)
            }
            .background(AppBackground())
            .navigationTitle("搜索")
            .searchable(text: $model.query, prompt: "搜索游戏")
            .autocorrectionDisabled()
            .scrollDismissesKeyboard(.interactively)
        }
    }

    @ViewBuilder
    private var emptyResults: some View {
        let title = model.displayedGames.isEmpty ? "游戏库还是空的" : "没有找到游戏"
        let detail = model.displayedGames.isEmpty ? "先在游戏库中导入游戏" : "试试其他名称"
        if #available(iOS 17.0, *) {
            ContentUnavailableView(
                title,
                systemImage: "magnifyingglass",
                description: Text(detail)
            )
            .padding(.top, 50)
        } else {
            VStack(spacing: 12) {
                Label(title, systemImage: "magnifyingglass")
                    .font(.title3.weight(.semibold))
                Text(detail).foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity)
            .padding(.top, 70)
        }
    }
}

/// Game cards in the chosen layout. Tapping launches; long-press opens the
/// game's details.
struct GameGrid: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.horizontalSizeClass) private var sizeClass
    @State private var selectedGame: GameRecord?
    let games: [GameRecord]

    private var columns: [GridItem] {
        if model.settings.listLayout {
            return [GridItem(.flexible())]
        }
        return [GridItem(.adaptive(minimum: sizeClass == .regular ? 180 : 150), spacing: 12)]
    }

    var body: some View {
        LazyVGrid(columns: columns, spacing: 12) {
            ForEach(games) { game in
                Button { model.launch(game) } label: {
                    GameCard(game: game, list: model.settings.listLayout)
                }
                .buttonStyle(.plain)
                .disabled(!game.availability.canLaunch)
                .contextMenu {
                    Button { selectedGame = game } label: {
                        Label("作品详情", systemImage: "info.circle")
                    }
                }
                .accessibilityLabel(
                    game.availability.canLaunch
                        ? "运行 \(game.title)"
                        : "\(game.title)，\(model.availabilityText(game))"
                )
            }
        }
        .sheet(item: $selectedGame) {
            GameDetailView(game: $0).environmentObject(model)
        }
    }
}

struct GameCard: View {
    @EnvironmentObject private var model: AppModel
    let game: GameRecord
    let list: Bool

    private var comingSoon: Bool { game.availability == .unsupported }

    /// A KiriKiri game whose launch target could not be found.
    private var showsReason: Bool {
        if case .invalid = game.availability { return true }
        return false
    }

    private var meta: String {
        var parts = [game.engine.displayName, AppModel.size(game.byteCount)]
        if !comingSoon { parts.append(model.recency(game)) }
        return parts.joined(separator: " · ")
    }

    var body: some View {
        if list {
            row
        } else {
            tile
        }
    }

    private var cover: some View {
        GameCover(game: game)
            .grayscale(comingSoon ? 0.9 : 0)
            .opacity(comingSoon ? 0.55 : 1)
    }

    private var tile: some View {
        VStack(alignment: .leading, spacing: 0) {
            Color.clear
                .aspectRatio(16 / 9, contentMode: .fit)
                .overlay { cover }
                .clipped()
            VStack(alignment: .leading, spacing: 2) {
                Text(game.title)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(comingSoon ? Color.secondary : Color.primary)
                    .lineLimit(1)
                Text(meta)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                if showsReason {
                    Text(model.availabilityText(game))
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            .padding(.horizontal, 12)
            .padding(.top, 10)
            .padding(.bottom, 12)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .background(Color(uiColor: .secondarySystemGroupedBackground))
        .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
        .contentShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
    }

    private var row: some View {
        HStack(spacing: 14) {
            cover
                .frame(width: 96, height: 54)
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text(game.title)
                    .font(.headline)
                    .foregroundStyle(comingSoon ? Color.secondary : Color.primary)
                    .lineLimit(1)
                Text(meta)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                if showsReason {
                    Text(model.availabilityText(game))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            Spacer(minLength: 0)
            if game.availability.canLaunch {
                Image(systemName: "play.circle.fill")
                    .font(.title2)
                    .symbolRenderingMode(.hierarchical)
                    .foregroundStyle(.tint)
            }
        }
        .padding(10)
        .background(
            Color(uiColor: .secondarySystemGroupedBackground),
            in: RoundedRectangle(cornerRadius: 20, style: .continuous)
        )
        .contentShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
    }
}

struct MissingGamesView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var relinkGame: GameRecord?
    @State private var relinkPickerShown = false
    @State private var deleteGame: GameRecord?

    var body: some View {
        NavigationStack {
            Group {
                if model.missingGames.isEmpty {
                    if #available(iOS 17.0, *) {
                        ContentUnavailableView(
                            "没有缺失游戏",
                            systemImage: "checkmark.circle",
                            description: Text("被移走的游戏文件夹会显示在这里。")
                        )
                    }
                } else {
                    List(model.missingGames) { game in
                        VStack(alignment: .leading, spacing: 10) {
                            HStack(spacing: 12) {
                                GameCover(game: game)
                                    .frame(width: 72, height: 52)
                                    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(game.title).font(.headline)
                                    Text("Documents/\(game.engine.documentsDirectoryName)/\(game.folderName)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(2)
                                    Text("累计游玩 \(game.playTime.playTimeClock)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Text("最近游玩 \(model.recency(game))")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                            HStack {
                                Button {
                                    relinkGame = game
                                    relinkPickerShown = true
                                } label: {
                                    Label("重新关联", systemImage: "folder.badge.plus")
                                }
                                Spacer()
                                Button(role: .destructive) {
                                    deleteGame = game
                                } label: {
                                    Label("删除记录", systemImage: "trash")
                                }
                            }
                            .buttonStyle(.borderless)
                        }
                        .padding(.vertical, 4)
                    }
                }
            }
            .navigationTitle("缺失游戏")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("完成") { dismiss() }
                }
            }
        }
        .fileImporter(
            isPresented: $relinkPickerShown,
            allowedContentTypes: [.folder]
        ) { result in
            let game = relinkGame
            relinkGame = nil
            switch result {
            case .success(let url):
                if let game {
                    Task { await model.relink(game, to: url) }
                }
            case .failure(let error):
                model.alert = error.localizedDescription
            }
        }
        .confirmationDialog(
            "永久删除这条游戏记录？",
            isPresented: Binding(
                get: { deleteGame != nil },
                set: { if !$0 { deleteGame = nil } }
            ),
            titleVisibility: .visible,
            presenting: deleteGame
        ) { game in
            Button("删除记录与历史", role: .destructive) {
                deleteGame = nil
                Task { await model.forget(game) }
            }
            Button("取消", role: .cancel) { deleteGame = nil }
        } message: { _ in
            Text("这不会删除你另外存放的游戏文件夹，但会移除游玩历史和手动封面。")
        }
    }

}

struct GameDetailView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var photo: PhotosPickerItem?
    let game: GameRecord

    private var current: GameRecord {
        model.games.first { $0.id == game.id } ?? game
    }

    var body: some View {
        NavigationStack {
            Form {
                GameCover(game: current)
                    .frame(height: 170)
                    .listRowInsets(EdgeInsets())
                LabeledContent("名称", value: current.title)
                LabeledContent("引擎", value: current.engine.displayName)
                LabeledContent("目录", value: "Documents/\(current.engine.documentsDirectoryName)/\(current.folderName)")
                LabeledContent("状态", value: model.availabilityText(current))
                LabeledContent("文件大小", value: AppModel.size(current.byteCount))
                LabeledContent("最近游玩", value: model.recency(current))
                LabeledContent("累计游玩") {
                    VStack(alignment: .trailing, spacing: 2) {
                        Text(current.playTime.playTimeClock)
                            .monospacedDigit()
                        if current.playTime >= 60 {
                            Text(current.playTime.playTimeSummary)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                .accessibilityLabel("累计游玩 \(current.playTime.playTimeSummary)")
                LabeledContent("启动次数", value: "\(current.launchCount) 次")
                PhotosPicker(selection: $photo, matching: .images) {
                    Label("更换封面", systemImage: "photo")
                }
                Text("长按游戏卡片可打开此页面。只有 KiriKiri 引擎已接入运行时；引擎识别不代表已经验证兼容性。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .navigationTitle("作品详情")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("完成") { dismiss() }
                }
            }
            .onChange(of: photo) { item in
                Task {
                    do {
                        guard let data = try await item?.loadTransferable(type: Data.self),
                              let image = UIImage(data: data) else { return }
                        await model.updateCover(game, image: image)
                    } catch {
                        model.alert = error.localizedDescription
                    }
                }
            }
        }
    }
}
