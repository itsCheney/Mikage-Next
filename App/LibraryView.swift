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
            .sheet(item: $model.finishRequest) { game in
                RatingSheet(game: game, mode: .finish).environmentObject(model)
            }
            .task {
                await model.refreshLibrary(showErrors: false)
            }
            .onChange(of: scenePhase) { phase in
                AppDiagnostics.shared.event("app", "scenePhase.changed", ["phase": String(describing: phase)])
                if phase == .active {
                    Task {
                        await model.refreshLibrary(showErrors: false)
                        await model.syncPending()
                        await model.refreshLinkedGames()
                    }
                }
            }
            .sheet(item: $model.matchRequest) { game in
                MatchView(game: game).environmentObject(model)
            }
            .overlay(alignment: .bottom) {
                VStack(spacing: 8) {
                    if let toast = model.newGamesToast {
                        NewGamesToastView(toast: toast)
                            .transition(.move(edge: .bottom).combined(with: .opacity))
                    }
                    if let toast = model.toast {
                        LibraryToastView(toast: toast)
                            .transition(.move(edge: .bottom).combined(with: .opacity))
                    }
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 72)
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.85), value: model.toast)
            .animation(.spring(response: 0.4, dampingFraction: 0.85), value: model.newGamesToast)
            .confirmationDialog(
                "Which file should this game start from?",
                isPresented: Binding(
                    get: { model.startupRequest != nil },
                    set: { if !$0 { model.startupRequest = nil } }
                ),
                titleVisibility: .visible,
                presenting: model.startupRequest
            ) { request in
                ForEach(request.choices, id: \.relativePath) { file in
                    let name = AppModel.startupName(file.relativePath, engine: request.game.engine)
                    Button(file.relativePath == request.game.launchTarget
                        ? String(localized: "\(name) (Suggested)")
                        : name
                    ) {
                        model.launch(request, from: file)
                    }
                }
                Button("Cancel", role: .cancel) {}
            } message: { _ in
                Text("Mikage remembers your choice. You can change it later under Startup file on the game’s page.")
            }
            .alert("Notice", isPresented: Binding(
                get: { model.alert != nil },
                set: { if !$0 { model.alert = nil } }
            )) {
                Button("OK", role: .cancel) { model.alert = nil }
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
                Tab("Library", systemImage: "books.vertical", value: 0) {
                    LibraryView()
                }
                Tab("Settings", systemImage: "gearshape", value: 1) {
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
                    .tabItem { Label("Library", systemImage: "books.vertical.fill") }
                    .tag(0)
                SettingsView()
                    .tabItem { Label("Settings", systemImage: "gearshape.fill") }
                    .tag(1)
                LibrarySearchView()
                    .tabItem { Label("Search", systemImage: "magnifyingglass") }
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
                            ContinueBanner(
                                game: latest,
                                topInset: topInset,
                                headerHeight: headerHeight,
                                regular: regular
                            )
                        } else {
                            Color.clear.frame(height: headerHeight)
                        }

                        VStack(alignment: .leading, spacing: 12) {
                            if model.importing || model.scanning {
                                HStack {
                                    ProgressView()
                                    Text(model.importing ? LocalizedStringKey("Copying game files…") : LocalizedStringKey("Scanning game folders…"))
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
            "Choose Game Engine",
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
            Button("Cancel", role: .cancel) {
                model.pendingImport = nil
            }
        } message: { pending in
            if pending.detectedEngines.isEmpty {
                Text("Couldn’t recognize “\(pending.url.lastPathComponent)”. Choose the engine it uses.")
            } else {
                Text("Several engines match “\(pending.url.lastPathComponent)”. Confirm which one it uses.")
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
                Text("Library")
                    .font(.largeTitle.bold())
                    .accessibilityAddTraits(.isHeader)
                Text("\(model.displayedGames.count) games")
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
                .accessibilityLabel("Missing games: \(model.missingGames.count)")
                .accessibilityIdentifier("missing-games-button")
            }
            viewMenu
            importMenu
        }
        .font(.title3.weight(.medium))
        .tint(Color.primary)
        .nativeGlassCapsule()
    }

    private var viewMenu: some View {
        Menu {
            Section("Layout") {
                Picker("Layout", selection: $model.settings.listLayout) {
                    Label("Grid", systemImage: "square.grid.2x2").tag(false)
                    Label("List", systemImage: "list.bullet").tag(true)
                }
            }
            Section("Sort By") {
                Picker("Sort By", selection: $model.settings.sort) {
                    ForEach(LibrarySort.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
            }
        } label: {
            Label("View & Sort", systemImage: "ellipsis")
                .labelStyle(.iconOnly)
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .accessibilityLabel("View & Sort")
    }

    private var importMenu: some View {
        Menu {
            Button { importPicker = true } label: {
                Label("Import Folder", systemImage: "folder.badge.plus")
            }
            Button {
                model.alert = String(localized: "ZIP extraction and Wi-Fi upload aren’t available yet. You can also put game folders directly in Documents.")
            } label: {
                Label("ZIP & Wi-Fi Import", systemImage: "wifi")
            }
        } label: {
            Label("Add Game", systemImage: "plus")
                .labelStyle(.iconOnly)
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .disabled(model.importing)
        .accessibilityLabel("Add Game")
    }

    // MARK: Sections

    @ViewBuilder
    private var sections: some View {
        let playable = playableGames
        let comingSoon = comingSoonGames

        if !playable.isEmpty {
            HStack(alignment: .firstTextBaseline) {
                Text("All Games")
                    .font(.title2.bold())
                    .accessibilityAddTraits(.isHeader)
                Spacer()
                (Text("\(playable.count) games")
                    + Text(verbatim: " · \(AppModel.librarySize(playable.reduce(0) { $0 + $1.byteCount }))"))
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            GameGrid(games: playable)
        }

        if !comingSoon.isEmpty {
            VStack(alignment: .leading, spacing: 3) {
                Text("Coming Soon")
                    .font(.title2.bold())
                    .accessibilityAddTraits(.isHeader)
                Text("Engine recognized. Playable once its runtime is added.")
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
                Label("Your Stories Start Here", systemImage: "books.vertical")
            } description: {
                Text("Put game folders directly in Documents. They’re scanned automatically when you return to the app.")
            } actions: {
                Button("Import Game") { importPicker = true }
                    .buttonStyle(.borderedProminent)
                    .disabled(model.importing)
            }
            .padding(.top, 60)
        } else {
            VStack(spacing: 15) {
                Label("Your Stories Start Here", systemImage: "books.vertical")
                    .font(.title3.weight(.semibold))
                Text("Put game folders directly in Documents. They’re scanned automatically when you return to the app.")
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                Button("Import Game") { importPicker = true }
                    .buttonStyle(.borderedProminent)
                    .disabled(model.importing)
            }
            .frame(maxWidth: .infinity)
            .padding(.top, 60)
        }
    }
}

/// The last played game under the pinned header, as one composition built
/// from its cover:
/// - the top band (about the status bar plus 70 pt) is the color of the
///   cover's top edge, fading into
/// - a full-bleed, pre-blurred copy of the cover behind everything;
/// - the cover itself, sharp and fitted rather than cropped, starts below
///   the header text, and its edges are masked into the blur;
/// - the bottom darkens behind the title, play history and Resume.
private struct ContinueBanner: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.displayScale) private var displayScale
    let game: GameRecord
    /// Safe-area top (status bar) the banner extends behind.
    let topInset: CGFloat
    /// Bottom edge of the pinned header, including its fade.
    let headerHeight: CGFloat
    let regular: Bool
    @ObservedObject private var covers = CoverCache.shared
    @State private var artwork: HeroArtwork?

    /// Same total height as before the redesign: about 313 pt on an iPhone 13
    /// Pro (266 + its 47 pt status bar).
    private var heroHeight: CGFloat { (regular ? 380 : 266) + topInset }

    /// Decode size for the cover; also the cache key, so it stays fixed.
    private static let decodeSize = CGSize(width: 600, height: 600)

    var body: some View {
        GeometryReader { proxy in
            let size = proxy.size
            ZStack(alignment: .topLeading) {
                backdrop(size: size)
                if let artwork {
                    foreground(artwork, in: size)
                }
                // Contrast for the text and Resume at the bottom.
                LinearGradient(
                    stops: [
                        .init(color: .clear, location: 0.5),
                        .init(color: .black.opacity(0.65), location: 1)
                    ],
                    startPoint: .top,
                    endPoint: .bottom
                )
                .allowsHitTesting(false)
                info
                    .frame(width: size.width, height: size.height, alignment: .bottomLeading)
            }
        }
        .frame(height: heroHeight)
        .clipped()
        .environment(\.colorScheme, .dark)
        .task(id: "\(model.coverURL(game)?.path ?? game.id.uuidString)|\(covers.generation)") {
            await load()
        }
    }

    // MARK: Layers

    private func backdrop(size: CGSize) -> some View {
        let top = Color(uiColor: artwork?.topColor ?? UIColor(white: 0.22, alpha: 1))
        // The color band covers the status bar and about 70 pt more.
        let band = min(1, (topInset + 70) / max(size.height, 1))
        return ZStack {
            top
            if let artwork {
                Image(uiImage: artwork.blurred)
                    .resizable()
                    .scaledToFill()
                    .frame(width: size.width, height: size.height)
                    .clipped()
                    .opacity(0.9)
            }
            LinearGradient(
                stops: [
                    .init(color: top, location: 0),
                    .init(color: top.opacity(0.6), location: band * 0.6),
                    .init(color: top.opacity(0), location: band)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        .frame(width: size.width, height: size.height)
        .accessibilityHidden(true)
    }

    private func foreground(_ artwork: HeroArtwork, in size: CGSize) -> some View {
        let frame = Self.foregroundFrame(
            aspect: artwork.aspectRatio,
            in: size,
            preferredTop: max(topInset + 50, headerHeight - 22),
            minimumTop: topInset + 40
        )
        // Portrait covers are narrower than the hero: fade their sides too.
        let sideStops: [Gradient.Stop] = frame.width >= size.width - 0.5
            ? [.init(color: .black, location: 0), .init(color: .black, location: 1)]
            : [
                .init(color: .clear, location: 0),
                .init(color: .black, location: 0.12),
                .init(color: .black, location: 0.88),
                .init(color: .clear, location: 1)
            ]
        return Image(uiImage: artwork.cover)
            .resizable()
            .scaledToFit()
            .frame(width: frame.width, height: frame.height)
            // Fade the top into the color band and the bottom into the
            // darkened info area, so no edge of the cover shows.
            .mask(
                LinearGradient(
                    stops: [
                        .init(color: .clear, location: 0),
                        .init(color: .black, location: 0.14),
                        .init(color: .black, location: 0.62),
                        .init(color: .clear, location: 1)
                    ],
                    startPoint: .top,
                    endPoint: .bottom
                )
            )
            .mask(LinearGradient(stops: sideStops, startPoint: .leading, endPoint: .trailing))
            .offset(x: frame.minX, y: frame.minY)
            .accessibilityHidden(true)
    }

    /// Fits the cover in the hero below `preferredTop`. A cover too tall to
    /// fit there moves up, but never above `minimumTop`, before it is scaled
    /// down to the remaining height; wide covers span the full width.
    static func foregroundFrame(
        aspect: CGFloat,
        in size: CGSize,
        preferredTop: CGFloat,
        minimumTop: CGFloat
    ) -> CGRect {
        guard aspect > 0, size.width > 0, size.height > 0 else { return .zero }
        let fullWidthHeight = size.width / aspect
        // Room below the header that keeps the whole cover inside the hero.
        let top = min(preferredTop, max(minimumTop, size.height - fullWidthHeight))
        let available = size.height - top
        if fullWidthHeight <= available {
            return CGRect(x: 0, y: top, width: size.width, height: fullWidthHeight)
        }
        let width = available * aspect
        return CGRect(x: (size.width - width) / 2, y: top, width: width, height: available)
    }

    private var info: some View {
        HStack(alignment: .bottom, spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Continue Playing")
                    .font(.caption.weight(.semibold))
                    .tracking(2)
                    .opacity(0.9)
                Text(model.displayTitle(game))
                    .font(regular ? .largeTitle.bold() : .title2.bold())
                    .lineLimit(2)
                Text("\(model.recency(game)) · Played \(game.playTime.playTimeSummary)")
                    .font(regular ? .subheadline : .footnote)
                    .opacity(0.88)
                    .lineLimit(1)
            }
            .shadow(color: .black.opacity(0.35), radius: 6, y: 1)
            Spacer(minLength: 0)
            Button { model.launch(game) } label: {
                Label("Resume", systemImage: "play.fill")
                    .font(.headline)
                    .padding(.horizontal, 4)
            }
            .nativeGlassButtonStyle(prominent: true)
            .controlSize(.large)
            .accessibilityLabel("Continue Playing: \(model.displayTitle(game))")
        }
        .foregroundStyle(.white)
        .padding(.horizontal, regular ? 28 : 20)
        .padding(.bottom, regular ? 26 : 22)
    }

    private func load() async {
        guard let url = model.coverURL(game) else {
            artwork = nil
            return
        }
        // A cache hit appears without a fade so returning to the tab is calm.
        if let hit = CoverCache.shared.cachedHeroArtwork(url, size: Self.decodeSize, scale: displayScale) {
            artwork = hit
            return
        }
        let processed = await CoverCache.shared.heroArtwork(
            for: url,
            size: Self.decodeSize,
            scale: displayScale
        )
        guard !Task.isCancelled else { return }
        withAnimation(.easeOut(duration: 0.25)) { artwork = processed }
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
            .navigationTitle("Search")
            .searchable(text: $model.query, prompt: "Search Games")
            .autocorrectionDisabled()
            .scrollDismissesKeyboard(.interactively)
        }
    }

    @ViewBuilder
    private var emptyResults: some View {
        let title: LocalizedStringKey = model.displayedGames.isEmpty ? "Your Library Is Empty" : "No Games Found"
        let detail: LocalizedStringKey = model.displayedGames.isEmpty ? "Import games in the Library first" : "Try a different name"
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

/// Game cards in the chosen layout. Tapping launches; long-press shows a
/// preview with the game's details, status and match actions.
struct GameGrid: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.horizontalSizeClass) private var sizeClass
    @State private var selectedGame: GameRecord?
    @State private var matchGame: GameRecord?
    @State private var finishGame: GameRecord?
    @State private var coverGame: GameRecord?
    @State private var artGame: GameRecord?
    @State private var showCoverPicker = false
    @State private var coverPhoto: PhotosPickerItem?
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
                    menu(for: game)
                } preview: {
                    GamePreview(game: game).environmentObject(model)
                }
                .accessibilityLabel(
                    game.availability.canLaunch
                        ? Text("Play \(model.displayTitle(game))")
                        : Text("\(model.displayTitle(game)), \(model.availabilityText(game))")
                )
            }
        }
        .sheet(item: $selectedGame) {
            GameDetailView(game: $0).environmentObject(model)
        }
        .sheet(item: $matchGame) {
            MatchView(game: $0).environmentObject(model)
        }
        .sheet(item: $finishGame) {
            RatingSheet(game: $0, mode: .finish).environmentObject(model)
        }
        .sheet(item: $artGame) {
            ArtworkChoiceView(game: $0).environmentObject(model)
        }
        .photosPicker(isPresented: $showCoverPicker, selection: $coverPhoto, matching: .images)
        .onChange(of: coverPhoto) { item in
            guard let item, let game = coverGame else { return }
            coverPhoto = nil
            Task {
                do {
                    guard let data = try await item.loadTransferable(type: Data.self),
                          let image = UIImage(data: data) else { return }
                    await model.updateCover(game, image: image)
                } catch {
                    model.alert = error.localizedDescription
                }
            }
        }
    }

    @ViewBuilder
    private func menu(for game: GameRecord) -> some View {
        Button { selectedGame = game } label: {
            Label("Game Details", systemImage: "info.circle")
        }
        Menu {
            StatusMenuItems(current: game.listEntry.status) { status in
                if status == .finished {
                    finishGame = game
                } else {
                    model.setStatus(status, for: game)
                }
            }
        } label: {
            if let status = game.listEntry.status {
                Label {
                    Text("Status")
                    Text(status.displayName)
                } icon: {
                    Image(systemName: status.symbol)
                }
            } else {
                Label("Status", systemImage: "circle.dashed")
            }
        }
        if game.listEntry.status != .finished {
            Button { finishGame = game } label: {
                Label("Mark as Finished", systemImage: "checkmark.seal")
            }
        }
        Divider()
        Button {
            coverGame = game
            showCoverPicker = true
        } label: {
            Label("Change Cover", systemImage: "photo")
        }
        if game.link != nil {
            Button { artGame = game } label: {
                Label("Choose Cover from Bangumi & VNDB", systemImage: "photo.on.rectangle")
            }
        }
        Button { matchGame = game } label: {
            Label(
                game.link == nil ? LocalizedStringKey("Find on Bangumi & VNDB") : LocalizedStringKey("Match Again"),
                systemImage: "link"
            )
        }
    }
}

/// The lifted card shown while a game is long-pressed.
private struct GamePreview: View {
    @EnvironmentObject private var model: AppModel
    let game: GameRecord

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            GameCover(game: game)
                .frame(width: 320, height: 180)
            VStack(alignment: .leading, spacing: 3) {
                Text(model.displayTitle(game))
                    .font(.headline)
                    .lineLimit(2)
                HStack(spacing: 6) {
                    if let status = game.listEntry.status {
                        Circle().fill(status.color).frame(width: 8, height: 8)
                        Text(status.displayName)
                    } else {
                        Text(game.engine.displayName)
                    }
                    if game.playTime >= 60 {
                        Text(verbatim: "·")
                        Text("Played \(game.playTime.playTimeSummary)")
                    }
                }
                .font(.footnote)
                .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .frame(width: 320, alignment: .leading)
        }
        .background(Color(uiColor: .secondarySystemGroupedBackground))
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
        var parts = [game.engine.displayName, AppModel.librarySize(game.byteCount)]
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

    /// The status leads the line when the player set one, as in the
    /// redesign: "Finished · Sep 2". Otherwise engine, size and recency.
    @ViewBuilder
    private var metaLine: some View {
        if let status = game.listEntry.status, !comingSoon {
            HStack(spacing: 4) {
                Image(systemName: status.symbol)
                    .imageScale(.small)
                    .foregroundStyle(status.color)
                Text(status.displayName)
                    .fontWeight(.semibold)
                    .foregroundStyle(status == .wish ? Color.secondary : status.color)
                if status != .wish {
                    Text(verbatim: "· \(model.recency(game))")
                        .foregroundStyle(.secondary)
                }
            }
            .lineLimit(1)
        } else {
            Text(meta)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
    }

    private var tile: some View {
        VStack(alignment: .leading, spacing: 0) {
            Color.clear
                .aspectRatio(16 / 9, contentMode: .fit)
                .overlay { cover }
                .clipped()
            VStack(alignment: .leading, spacing: 2) {
                Text(model.displayTitle(game))
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(comingSoon ? Color.secondary : Color.primary)
                    .lineLimit(1)
                metaLine
                    .font(.caption)
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
                Text(model.displayTitle(game))
                    .font(.headline)
                    .foregroundStyle(comingSoon ? Color.secondary : Color.primary)
                    .lineLimit(1)
                metaLine
                    .font(.subheadline)
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
                            "No Missing Games",
                            systemImage: "checkmark.circle",
                            description: Text("Game folders that were moved away appear here.")
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
                                    Text(model.displayTitle(game)).font(.headline)
                                    Text(verbatim: "Documents/\(game.folderName)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(2)
                                    Text("Total play time: \(game.playTime.playTimeClock)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Text("Last played: \(model.recency(game))")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    if model.repository.hasKeptSaves(game) {
                                        Label("Removed with saves kept. Import the folder again to restore them.", systemImage: "externaldrive.badge.checkmark")
                                            .font(.caption)
                                            .foregroundStyle(.tint)
                                    }
                                }
                            }
                            HStack {
                                Button {
                                    relinkGame = game
                                    relinkPickerShown = true
                                } label: {
                                    Label("Relink", systemImage: "folder.badge.plus")
                                }
                                Spacer()
                                Button(role: .destructive) {
                                    deleteGame = game
                                } label: {
                                    Label("Delete Record", systemImage: "trash")
                                }
                            }
                            .buttonStyle(.borderless)
                        }
                        .padding(.vertical, 4)
                    }
                }
            }
            .navigationTitle("Missing Games")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
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
            "Permanently delete this game record?",
            isPresented: Binding(
                get: { deleteGame != nil },
                set: { if !$0 { deleteGame = nil } }
            ),
            titleVisibility: .visible,
            presenting: deleteGame
        ) { game in
            Button("Delete Record and History", role: .destructive) {
                deleteGame = nil
                Task { await model.forget(game) }
            }
            Button("Cancel", role: .cancel) { deleteGame = nil }
        } message: { game in
            if model.repository.hasKeptSaves(game) {
                Text("This removes its play history, custom cover and the saves kept when the game was removed.")
            } else {
                Text("This doesn’t delete the game folder you keep elsewhere, but removes its play history and custom cover.")
            }
        }
    }

}
