import SwiftUI
import PhotosUI
import UniformTypeIdentifiers
import VNCore

struct RootView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        TabView(selection: $model.tab) {
            LibraryView()
                .tabItem {
                    Label("游戏库", systemImage: "books.vertical.fill")
                }
                .tag(0)

            SettingsView()
                .tabItem {
                    Label("设置", systemImage: "gearshape.fill")
                }
                .tag(1)
        }
        .fullScreenCover(item: $model.player) { game in
            PlayerView(game: game).environmentObject(model)
        }
        .task {
            await model.refreshLibrary(showErrors: false)
        }
        .onChange(of: scenePhase) { phase in
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
}

struct LibraryView: View {
    @EnvironmentObject private var model: AppModel
    @State private var importPicker = false
    @State private var selectedGame: GameRecord?
    @State private var showMissingGames = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text("\(model.displayedGames.count)部作品 · 共\(model.totalSize)")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)

                    if model.importing || model.scanning {
                        HStack {
                            ProgressView()
                            Text(model.importing ? "正在复制游戏文件…" : "正在扫描游戏目录…")
                        }
                        .font(.subheadline)
                    }

                    if model.query.isEmpty, let latest = model.latest {
                        continueCard(latest)
                    }

                    if model.displayedGames.isEmpty {
                        emptyLibrary
                    } else if model.filteredGames.isEmpty {
                        if #available(iOS 17.0, *) {
                            ContentUnavailableView(
                                "没有找到游戏",
                                systemImage: "magnifyingglass",
                                description: Text("试试其他名称")
                            )
                            .padding(.top, 50)
                        } else {
                            emptySearchResults
                        }
                    } else {
                        LazyVGrid(
                            columns: model.settings.listLayout
                                ? [GridItem(.flexible())]
                                : [GridItem(.adaptive(minimum: 150), spacing: 14)],
                            spacing: 14
                        ) {
                            ForEach(model.filteredGames) { game in
                                Button { model.launch(game) } label: {
                                    gameCard(game)
                                }
                                .buttonStyle(.borderless)
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
                    }
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 20)
                .frame(maxWidth: 850)
                .frame(maxWidth: .infinity)
            }
            .background(AppBackground())
            .refreshable {
                await model.refreshLibrary()
            }
            .navigationTitle("游戏库")
            .searchable(
                text: $model.query,
                placement: .navigationBarDrawer(displayMode: .always),
                prompt: "搜索游戏"
            )
            .autocorrectionDisabled()
            .scrollDismissesKeyboard(.interactively)
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    if !model.missingGames.isEmpty {
                        Button {
                            showMissingGames = true
                        } label: {
                            Label(
                                "缺失游戏 \(model.missingGames.count)",
                                systemImage: "exclamationmark.folder"
                            )
                        }
                        .accessibilityLabel("缺失游戏 \(model.missingGames.count)")
                    }
                    displayMenu
                    sortMenu
                    importMenu
                }
            }
        }
        .fileImporter(isPresented: $importPicker, allowedContentTypes: [.folder]) { result in
            switch result {
            case .success(let url):
                Task { await model.beginImport(url) }
            case .failure(let error):
                model.alert = error.localizedDescription
            }
        }
        .sheet(item: $selectedGame) {
            GameDetailView(game: $0).environmentObject(model)
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

    private var displayMenu: some View {
        Menu {
            Button {
                model.settings.listLayout = false
            } label: {
                Label("网格视图", systemImage: "square.grid.2x2")
            }

            Button {
                model.settings.listLayout = true
            } label: {
                Label("列表视图", systemImage: "list.bullet")
            }
        } label: {
            Label(
                "显示方式",
                systemImage: model.settings.listLayout ? "list.bullet" : "square.grid.2x2"
            )
            .labelStyle(.iconOnly)
        }
        .accessibilityLabel("显示方式")
    }

    private var sortMenu: some View {
        Menu {
            Picker("排序方式", selection: $model.settings.sort) {
                ForEach(["最近游玩", "添加时间", "名称", "大小"], id: \.self) {
                    Text($0)
                }
            }
        } label: {
            Label("排序", systemImage: "arrow.up.arrow.down")
                .labelStyle(.iconOnly)
        }
        .accessibilityLabel("排序")
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

    private func continueCard(_ game: GameRecord) -> some View {
        Button { model.launch(game) } label: {
            ZStack(alignment: .bottomLeading) {
                GameCover(game: game)
                LinearGradient(
                    colors: [.clear, .black.opacity(0.75)],
                    startPoint: .top,
                    endPoint: .bottom
                )
                VStack(alignment: .leading) {
                    Text("继续上次")
                        .font(.caption.weight(.semibold))
                        .tracking(2)
                    Spacer()
                    HStack {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(game.title).font(.headline).lineLimit(1)
                            Text(model.recency(game)).font(.subheadline).opacity(0.8)
                        }
                        Spacer()
                        Image(systemName: "play.circle.fill")
                            .font(.largeTitle)
                            .symbolRenderingMode(.hierarchical)
                    }
                }
                .padding(15)
                .foregroundStyle(.white)
            }
            .frame(height: 140)
            .clipShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
        }
        .buttonStyle(.borderless)
        .accessibilityLabel("继续上次：\(game.title)")
    }

    @ViewBuilder
    private func gameCard(_ game: GameRecord) -> some View {
        if model.settings.listLayout {
            HStack(spacing: 14) {
                GameCover(game: game)
                    .frame(width: 90, height: 66)
                    .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                VStack(alignment: .leading, spacing: 5) {
                    Text(game.title).font(.headline)
                    Text("\(game.engine.displayName) · \(AppModel.size(game.byteCount))")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    if !game.availability.canLaunch {
                        Text(model.availabilityText(game))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                }
                Spacer()
                Image(systemName: game.availability.canLaunch ? "play.circle.fill" : "lock.circle")
                    .font(.title2)
                    .symbolRenderingMode(.hierarchical)
            }
            .padding(10)
            .background(
                Color(uiColor: .secondarySystemGroupedBackground),
                in: RoundedRectangle(cornerRadius: 20, style: .continuous)
            )
        } else {
            VStack(alignment: .leading, spacing: 0) {
                GameCover(game: game)
                    .frame(height: 62)
                    .overlay(alignment: .topLeading) {
                        Text(model.recency(game))
                            .font(.caption2.weight(.medium))
                            .padding(.horizontal, 8)
                            .padding(.vertical, 3)
                            .background(.black.opacity(0.45), in: Capsule())
                            .foregroundStyle(.white)
                            .padding(7)
                    }
                VStack(alignment: .leading, spacing: 3) {
                    Text(game.title).font(.subheadline.weight(.medium)).lineLimit(1)
                    Text("\(game.engine.displayName) · \(AppModel.size(game.byteCount))")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    if !game.availability.canLaunch {
                        Text(model.availabilityText(game))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(Color(uiColor: .secondarySystemGroupedBackground))
            .clipShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
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

    private var emptySearchResults: some View {
        VStack(spacing: 12) {
            Label("没有找到游戏", systemImage: "magnifyingglass")
                .font(.title3.weight(.semibold))
            Text("试试其他名称").foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.top, 70)
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
                                    Text("累计游玩 \(formattedPlayTime(game.playTime))")
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

    private func formattedPlayTime(_ interval: TimeInterval) -> String {
        let seconds = max(0, Int(interval))
        return String(format: "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60)
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
                        model.updateCover(game, image: image)
                    } catch {
                        model.alert = error.localizedDescription
                    }
                }
            }
        }
    }
}
