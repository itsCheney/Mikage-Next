import SwiftUI
import PhotosUI
import UniformTypeIdentifiers
import VNCore

struct RootView: View {
    @EnvironmentObject private var model: AppModel
    var body: some View {
        ZStack {
            AppBackground()
            if model.tab == 0 { LibraryView() } else { SettingsView() }
        }
        .safeAreaInset(edge: .bottom, spacing: 0) { tabBar.padding(.bottom, 8).padding(.top, 12) }
        .fullScreenCover(item: $model.player) { game in PlayerView(game: game).environmentObject(model) }
        .alert("提示", isPresented: Binding(get: { model.alert != nil }, set: { if !$0 { model.alert = nil } })) {
            Button("好", role: .cancel) { model.alert = nil }
        } message: { Text(model.alert ?? "") }
    }
    private var tabBar: some View {
        HStack(spacing: 0) {
            tab("游戏库", symbol: "books.vertical.fill", index: 0)
            tab("设置", symbol: "gearshape.fill", index: 1)
        }.padding(4).frame(width: 194, height: 62).glass(40)
    }
    private func tab(_ title: String, symbol: String, index: Int) -> some View {
        Button { withAnimation(.easeInOut(duration: 0.18)) { model.tab = index } } label: {
            VStack(spacing: 3) {
                Image(systemName: symbol).font(.system(size: 25, weight: .semibold))
                Text(title).font(.system(size: 11, weight: .medium))
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(model.tab == index ? Color.primary.opacity(0.10) : .clear, in: Capsule())
        }.buttonStyle(.plain).accessibilityAddTraits(model.tab == index ? .isSelected : [])
    }
}

struct LibraryView: View {
    @EnvironmentObject private var model: AppModel
    @State private var importPicker = false
    @State private var selectedGame: GameRecord?
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                header.padding(.top, 16)
                HStack(spacing: 10) {
                    Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                    TextField("搜索游戏", text: $model.query).autocorrectionDisabled()
                    if !model.query.isEmpty { Button { model.query = "" } label: { Image(systemName: "xmark.circle.fill") }.accessibilityLabel("清除搜索") }
                }.padding(14).glass(17)
                if model.importing { HStack { ProgressView(); Text("正在复制游戏文件…").font(.subheadline) }.padding() }
                if model.demo {
                    HStack {
                        Text("界面演示 · 不包含游戏").font(.caption).foregroundStyle(.secondary)
                        Spacer()
                        Button("结束演示") { model.demo = false }.font(.caption)
                    }
                }
                if model.query.isEmpty, let latest = model.latest { continueCard(latest) }
                if model.displayedGames.isEmpty { emptyLibrary }
                else if model.filteredGames.isEmpty {
                    VStack(spacing: 12) { Image(systemName: "magnifyingglass").font(.largeTitle); Text("没有找到游戏"); Text("试试其他名称").foregroundStyle(.secondary) }
                        .frame(maxWidth: .infinity).padding(.top, 70)
                } else {
                    LazyVGrid(columns: model.settings.listLayout ? [GridItem(.flexible())] : [GridItem(.adaptive(minimum: 150), spacing: 14)], spacing: 14) {
                        ForEach(model.filteredGames) { game in
                            Button { model.launch(game) } label: { gameCard(game) }
                                .buttonStyle(.plain)
                                .contextMenu { Button { selectedGame = game } label: { Label("作品详情", systemImage: "info.circle") } }
                                .accessibilityLabel("运行 \(game.title)")
                        }
                    }
                }
            }.padding(.horizontal, 16).padding(.bottom, 20)
                .frame(maxWidth: 850).frame(maxWidth: .infinity)
        }
        .fileImporter(isPresented: $importPicker, allowedContentTypes: [.folder]) { result in
            switch result { case .success(let url): Task { await model.importFolder(url) }; case .failure(let error): model.alert = error.localizedDescription }
        }
        .sheet(item: $selectedGame) { GameDetailView(game: $0).environmentObject(model) }
    }
    private var header: some View {
        HStack(alignment: .center, spacing: 8) {
            VStack(alignment: .leading, spacing: 3) {
                Text("游戏库").font(.system(size: 34, weight: .bold))
                Text("\(model.displayedGames.count)部作品 · 共\(model.totalSize)").font(.system(size: 13)).foregroundStyle(.secondary)
            }.frame(maxWidth: .infinity, alignment: .leading)
            HStack(spacing: 0) {
                layoutButton("square.grid.2x2.fill", list: false)
                layoutButton("list.bullet", list: true)
            }.padding(3).glass(26)
            Menu {
                Picker("排序方式", selection: $model.settings.sort) {
                    ForEach(["最近游玩", "添加时间", "名称", "大小"], id: \.self) { Text($0) }
                }
            } label: { Image(systemName: "arrow.up.arrow.down").font(.system(size: 20)).frame(width: 40, height: 40).glass(25) }
                .accessibilityLabel("排序")
            Menu {
                Button { importPicker = true } label: { Label("导入文件夹", systemImage: "folder.badge.plus") }
                Button { model.alert = "ZIP 解压与局域网上传将在导入模块接通后开放。当前可以从“文件”选择游戏文件夹。" } label: { Label("ZIP 与 Wi-Fi 导入", systemImage: "wifi") }
                if model.games.isEmpty { Button { model.demo = true } label: { Label("查看界面演示", systemImage: "sparkles") } }
            } label: { Image(systemName: "plus").font(.system(size: 23)).frame(width: 40, height: 40).glass(25) }
                .disabled(model.importing).accessibilityLabel("添加游戏")
        }.foregroundStyle(.primary)
    }
    private func layoutButton(_ icon: String, list: Bool) -> some View {
        Button { model.settings.listLayout = list } label: {
            Image(systemName: icon).font(.system(size: 16, weight: .semibold)).frame(width: 34, height: 34)
                .foregroundStyle(model.settings.listLayout == list ? Color(.systemBackground) : Color.secondary)
                .background(model.settings.listLayout == list ? Color.primary : .clear, in: Circle())
        }.buttonStyle(.plain).accessibilityLabel(list ? "列表视图" : "网格视图")
    }
    private func continueCard(_ game: GameRecord) -> some View {
        Button { model.launch(game) } label: {
            ZStack(alignment: .bottomLeading) {
                GameCover(game: game)
                LinearGradient(colors: [.clear, .black.opacity(0.75)], startPoint: .top, endPoint: .bottom)
                VStack(alignment: .leading) {
                    Text("继续上次").font(.system(size: 12, weight: .semibold)).tracking(2)
                    Spacer()
                    HStack {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(game.title).font(.system(size: 20, weight: .semibold)).lineLimit(1)
                            Text(model.recency(game)).font(.system(size: 13)).opacity(0.8)
                        }
                        Spacer()
                        Image(systemName: "play.fill").font(.system(size: 21)).foregroundStyle(.black)
                            .frame(width: 44, height: 44).background(.white.opacity(0.92), in: Circle())
                    }
                }.padding(15).foregroundStyle(.white)
            }.frame(height: 140).clipShape(RoundedRectangle(cornerRadius: 22)).glass(22)
        }.buttonStyle(.plain).accessibilityLabel("继续上次：\(game.title)")
    }
    @ViewBuilder private func gameCard(_ game: GameRecord) -> some View {
        if model.settings.listLayout {
            HStack(spacing: 14) {
                GameCover(game: game).frame(width: 90, height: 66).clipShape(RoundedRectangle(cornerRadius: 12))
                VStack(alignment: .leading, spacing: 5) { Text(game.title).font(.headline); Text(AppModel.size(game.byteCount)).foregroundStyle(.secondary).font(.subheadline) }
                Spacer(); Image(systemName: "play.fill").padding(.trailing, 10)
            }.padding(10).glass(20)
        } else {
            VStack(alignment: .leading, spacing: 0) {
                GameCover(game: game).frame(height: 62).overlay(alignment: .topLeading) {
                    Text(model.recency(game)).font(.system(size: 11, weight: .medium)).padding(.horizontal, 8).padding(.vertical, 3)
                        .background(.black.opacity(0.45), in: Capsule()).foregroundStyle(.white).padding(7)
                }
                VStack(alignment: .leading, spacing: 3) {
                    Text(game.title).font(.system(size: 15, weight: .medium)).lineLimit(1)
                    Text(AppModel.size(game.byteCount)).font(.system(size: 13)).foregroundStyle(.secondary)
                }.padding(12).frame(maxWidth: .infinity, alignment: .leading)
            }.clipShape(RoundedRectangle(cornerRadius: 22)).glass(22)
        }
    }
    private var emptyLibrary: some View {
        VStack(spacing: 15) {
            Image(systemName: "books.vertical").font(.system(size: 45, weight: .light)).foregroundStyle(.secondary)
            Text("故事，从这里开始").font(.title3.weight(.semibold))
            Text("导入你的 KrKr 游戏文件夹\n把喜欢的故事带在身边").font(.subheadline).foregroundStyle(.secondary).multilineTextAlignment(.center)
            Button("导入游戏") { importPicker = true }.buttonStyle(.borderedProminent).disabled(model.importing)
            Button("查看界面演示") { model.demo = true }.font(.caption)
        }.frame(maxWidth: .infinity).padding(.top, 90)
    }
}

struct GameDetailView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var photo: PhotosPickerItem?
    let game: GameRecord
    private var current: GameRecord { model.games.first { $0.id == game.id } ?? game }
    var body: some View {
        NavigationStack {
            Form {
                GameCover(game: current).frame(height: 170).listRowInsets(EdgeInsets())
                LabeledContent("名称", value: game.title)
                LabeledContent("引擎", value: "KiriKiri（候选）")
                LabeledContent("文件大小", value: AppModel.size(game.byteCount))
                LabeledContent("最近游玩", value: model.recency(game))
                if !model.demo {
                    PhotosPicker(selection: $photo, matching: .images) { Label("更换封面", systemImage: "photo") }
                }
                Text("长按游戏卡片可打开此页面。引擎识别不代表已经验证兼容性。").font(.footnote).foregroundStyle(.secondary)
            }.navigationTitle("作品详情").navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("完成") { dismiss() } } }
                .onChange(of: photo) { item in
                    Task {
                        do {
                            guard let data = try await item?.loadTransferable(type: Data.self), let image = UIImage(data: data) else { return }
                            model.updateCover(game, image: image)
                        } catch { model.alert = error.localizedDescription }
                    }
                }
        }
    }
}
