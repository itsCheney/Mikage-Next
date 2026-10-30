import SwiftUI
import PhotosUI
import ImageIO
import VNCore

/// A game's page: banner and cover, each service's community rating, play,
/// the player's status and rating, local progress with each service's sync state, and the details fetched
/// from Bangumi and VNDB.
struct GameDetailView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openURL) private var openURL
    let game: GameRecord

    @State private var photo: PhotosPickerItem?
    @State private var showPhotoPicker = false
    @State private var matching = false
    @State private var ratingMode: RatingSheet.Mode?
    @State private var synopsisExpanded = false
    @State private var confirmUnlink = false
    @State private var refreshing = false
    @State private var choosingArt = false
    @State private var showingSaves = false
    @State private var saveCount: Int?
    /// Width over height of the cover on disk; VNDB has both portrait and
    /// landscape package art.
    @State private var posterAspect: CGFloat?
    /// The files this game can start from; the picker shows only when there
    /// is more than one.
    @State private var startupChoices: [StartupFile] = []

    private var current: GameRecord { model.record(game.id) ?? game }
    private var metadata: LinkedMetadata? { current.link?.metadata }

    var body: some View {
        // A stack only for the saves page; the game page draws its own header.
        NavigationStack {
            page
                .toolbar(.hidden, for: .navigationBar)
                .navigationDestination(isPresented: $showingSaves) {
                    GameSavesView(gameID: current.id) { slot in
                        play(fromSaveSlot: slot)
                    }
                    .environmentObject(model)
                }
        }
    }

    private var page: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                banner
                titles
                communityRatings
                primaryActions
                VStack(alignment: .leading, spacing: 24) {
                    progress
                    if let metadata {
                        about(metadata)
                    } else {
                        linkPrompt
                    }
                    information
                    if current.link != nil {
                        links
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, 24)
                .padding(.bottom, 32)
            }
        }
        .background(AppBackground())
        .ignoresSafeArea(edges: .top)
        .sheet(isPresented: $matching) {
            MatchView(game: current).environmentObject(model)
        }
        .sheet(item: $ratingMode) { mode in
            RatingSheet(game: current, mode: mode).environmentObject(model)
        }
        .task { await model.refreshLinked(game.id) }
        .sheet(isPresented: $choosingArt) {
            ArtworkChoiceView(game: current).environmentObject(model)
        }
        .photosPicker(isPresented: $showPhotoPicker, selection: $photo, matching: .images)
        .onChange(of: photo) { item in
            guard let item else { return }
            Task {
                do {
                    guard let data = try await item.loadTransferable(type: Data.self),
                          let image = UIImage(data: data) else { return }
                    await model.updateCover(current, image: image)
                } catch {
                    model.alert = error.localizedDescription
                }
            }
        }
        .confirmationDialog(
            "Unlink from Bangumi and VNDB?",
            isPresented: $confirmUnlink,
            titleVisibility: .visible
        ) {
            Button("Unlink", role: .destructive) { model.unlink(current) }
        } message: {
            Text("The fetched cover and details are removed. Your status and rating stay on this device and in your lists.")
        }
    }

    // MARK: Banner and titles

    private var banner: some View {
        ArtworkImage(url: model.coverURL(current))
            .frame(height: 260)
            .frame(maxWidth: .infinity)
            .clipped()
            .overlay(alignment: .top) {
                HStack {
                    RoundButton(symbol: "xmark", label: "Close") { dismiss() }
                    Spacer()
                    moreMenu
                }
                .padding(.horizontal, 16)
                .padding(.top, 58)
                .environment(\.colorScheme, .dark)
            }
    }

    private var moreMenu: some View {
        Menu {
            Button { showPhotoPicker = true } label: {
                Label("Change Cover", systemImage: "photo")
            }
            if current.link != nil {
                Button { choosingArt = true } label: {
                    Label("Choose Cover from Bangumi & VNDB", systemImage: "photo.on.rectangle")
                }
            }
            Button { matching = true } label: {
                Label(
                    current.link == nil ? LocalizedStringKey("Find on Bangumi & VNDB") : LocalizedStringKey("Match Again"),
                    systemImage: "link"
                )
            }
            if current.link != nil {
                Button {
                    refreshing = true
                    Task {
                        await model.refreshMetadata(current)
                        refreshing = false
                    }
                } label: {
                    Label("Refresh Details", systemImage: "arrow.clockwise")
                }
                .disabled(refreshing)
                Button(role: .destructive) { confirmUnlink = true } label: {
                    Label("Unlink", systemImage: "link.badge.plus")
                }
            }
        } label: {
            Group {
                if refreshing {
                    ProgressView()
                } else {
                    Image(systemName: "ellipsis")
                        .font(.title3.weight(.medium))
                }
            }
            .frame(width: 44, height: 44)
            .contentShape(Circle())
            .nativeGlassCapsule()
        }
        .accessibilityLabel("More Actions")
    }

    /// Portrait covers stand 100 points wide; landscape ones keep their
    /// shape at a similar area, up to 190 points wide.
    private var posterSize: CGSize {
        guard let aspect = posterAspect, aspect > 1.05 else {
            return CGSize(width: 100, height: 142)
        }
        let width = min(190, 118 * aspect.squareRoot())
        return CGSize(width: width, height: (width / aspect).rounded())
    }

    /// Reads only the image header, not the pixels.
    private static func aspectRatio(of url: URL?) -> CGFloat? {
        guard let url, let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? CGFloat,
              let height = properties[kCGImagePropertyPixelHeight] as? CGFloat, height > 0 else { return nil }
        let orientation = properties[kCGImagePropertyOrientation] as? Int ?? 1
        // Orientations 5 to 8 are rotated a quarter turn.
        return orientation >= 5 ? height / width : width / height
    }

    private var titles: some View {
        HStack(alignment: .top, spacing: 16) {
            ArtworkImage(url: model.posterURL(current))
                .frame(width: posterSize.width, height: posterSize.height)
                .animation(.easeOut(duration: 0.2), value: posterSize)
                .task(id: model.posterURL(current)?.path) {
                    posterAspect = Self.aspectRatio(of: model.posterURL(current))
                }
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .strokeBorder(Color(uiColor: .systemBackground), lineWidth: 2)
                )
                .shadow(color: .black.opacity(0.28), radius: 13, y: 10)
                // Overlaps the banner, like a poster pinned to it.
                .padding(.top, -62)
            VStack(alignment: .leading, spacing: 3) {
                Text(model.displayTitle(current))
                    .font(.title2.bold())
                    .lineLimit(3)
                    .accessibilityAddTraits(.isHeader)
                if let original = metadata?.originalTitle, original != model.displayTitle(current) {
                    Text(original)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                let byline = [metadata?.developer, metadata?.releaseYear].compactMap { $0 }.joined(separator: " · ")
                if !byline.isEmpty {
                    Text(byline)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.top, 14)
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 20)
    }

    // MARK: Community ratings

    /// One card per service that has a score, each opening the game's page
    /// there. Hidden until either service has ratings.
    @ViewBuilder
    private var communityRatings: some View {
        let cards = MetadataService.allCases.compactMap { service in
            communityRating(service).map { (service: service, rating: $0) }
        }
        if !cards.isEmpty {
            HStack(spacing: 10) {
                ForEach(cards, id: \.service) { card in
                    ratingCard(card.service, card.rating)
                }
                if cards.count == 1 {
                    // A lone card keeps its width rather than stretching.
                    Color.clear.frame(maxWidth: .infinity, maxHeight: 0)
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 18)
        }
    }

    private func communityRating(_ service: MetadataService) -> CommunityRating? {
        switch service {
        case .bangumi: return current.link?.bangumiID == nil ? nil : metadata?.bangumiRating
        case .vndb: return current.link?.vndbID == nil ? nil : metadata?.vndbRating
        }
    }

    private func serviceURL(_ service: MetadataService) -> URL? {
        switch service {
        case .bangumi: return current.link?.bangumiURL
        case .vndb: return current.link?.vndbURL
        }
    }

    private func ratingCard(_ service: MetadataService, _ rating: CommunityRating) -> some View {
        Button {
            if let url = serviceURL(service) { openURL(url) }
        } label: {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 4) {
                    Text(verbatim: service.displayName)
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(.secondary)
                    Spacer(minLength: 4)
                    if let rank = rating.rank {
                        Text("Rank #\(rank)")
                            .font(.footnote.weight(.medium))
                            .foregroundStyle(.tertiary)
                            .monospacedDigit()
                    }
                }
                .lineLimit(1)
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(rating.score, format: .number.precision(.fractionLength(1)))
                        .font(.system(size: 30, weight: .bold))
                        .monospacedDigit()
                    Spacer(minLength: 0)
                    Text("\(rating.votes) ratings")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                }
            }
            .foregroundStyle(.primary)
            .padding(.vertical, 12)
            .padding(.leading, 16)
            .padding(.trailing, 14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                Color(uiColor: .secondarySystemGroupedBackground),
                in: RoundedRectangle(cornerRadius: 20, style: .continuous)
            )
            .shadow(color: .black.opacity(0.06), radius: 1.5, y: 1)
        }
        .buttonStyle(.plain)
        .disabled(serviceURL(service) == nil)
        .accessibilityElement(children: .combine)
        .accessibilityHint(Text("Opens the game in \(service.displayName)"))
    }

    // MARK: Play, status and rating

    private var primaryActions: some View {
        VStack(spacing: 10) {
            if current.availability.canLaunch {
                Button {
                    play()
                } label: {
                    Label(
                        current.lastPlayedAt == nil ? LocalizedStringKey("Play") : LocalizedStringKey("Resume"),
                        systemImage: "play.fill"
                    )
                        .font(.headline)
                        .frame(maxWidth: .infinity, minHeight: 30)
                }
                .nativeGlassButtonStyle(prominent: true)
                .controlSize(.large)
            }
            HStack(spacing: 10) {
                Menu {
                    StatusMenuItems(current: current.listEntry.status) { choose($0) }
                } label: {
                    pill(
                        title: "Status",
                        value: current.listEntry.status?.displayName ?? String(localized: "Not Set")
                    ) {
                        Circle()
                            .fill(current.listEntry.status?.color ?? Color(uiColor: .tertiaryLabel))
                            .frame(width: 10, height: 10)
                    } trailing: {
                        Image(systemName: "chevron.up.chevron.down")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                    }
                }
                Button { ratingMode = .rate } label: {
                    pill(title: "My Rating", value: ratingText) {
                        Image(systemName: "star.fill")
                            .foregroundStyle(.orange)
                    } trailing: {
                        EmptyView()
                    }
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 16)
        .padding(.top, 18)
    }

    private func play(fromSaveSlot slot: Int? = nil) {
        let game = current
        let model = self.model
        dismiss()
        // The player covers the whole app; present it once this sheet is
        // gone.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) {
            model.launch(game, fromSaveSlot: slot)
        }
    }

    private var ratingText: String {
        guard let rating = current.listEntry.rating else { return String(localized: "Not Rated") }
        return "\(rating) · \(RatingLabel.text(for: rating))"
    }

    private func pill<Leading: View, Trailing: View>(
        title: LocalizedStringKey,
        value: String,
        @ViewBuilder leading: () -> Leading,
        @ViewBuilder trailing: () -> Trailing
    ) -> some View {
        HStack(spacing: 8) {
            leading()
            VStack(alignment: .leading, spacing: 0) {
                Text(title)
                    .font(.caption2.weight(.medium))
                    .foregroundStyle(.secondary)
                Text(value)
                    .font(.body.weight(.semibold))
                    .foregroundStyle(.primary)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
            Spacer(minLength: 0)
            trailing()
        }
        .padding(.horizontal, 16)
        .frame(maxWidth: .infinity, minHeight: 50)
        .contentShape(Capsule())
        .nativeGlassCapsule()
    }

    private func choose(_ status: PlayStatus?) {
        if status == .finished {
            ratingMode = .finish
        } else {
            model.setStatus(status, for: current)
        }
    }

    // MARK: Progress

    private var progress: some View {
        DetailGroup("Progress") {
            DetailRow("Played on This Device", value: current.playTime.playTimeSummary, divider: false)
            if let started = current.listEntry.startedAt {
                DetailRow("Started", value: started.formatted(date: .abbreviated, time: .omitted))
            }
            if let finished = current.listEntry.finishedAt {
                DetailRow("Finished On", value: finished.formatted(date: .abbreviated, time: .omitted))
            }
            ForEach(current.link?.services ?? []) { service in
                DetailRow(label: LocalizedStringKey(service.displayName)) {
                    syncState(service)
                }
            }
            if current.engine == .onscripter && current.availability.canLaunch {
                savesRow
            }
        }
    }

    /// ONScripter saves are the game's own files, listed on their own page.
    private var savesRow: some View {
        Button {
            showingSaves = true
        } label: {
            DetailRow(label: "Saves") {
                HStack(spacing: 6) {
                    if let saveCount {
                        Text("\(saveCount) saves")
                    }
                    Image(systemName: "chevron.right")
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(.tertiary)
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("game-detail-saves")
        .task(id: showingSaves) {
            guard !showingSaves, let directory = try? model.repository.savesURL(for: current) else { return }
            saveCount = await Task.detached(priority: .utility) {
                ONSSaveFiles.saves(in: directory).count
            }.value
        }
    }

    @ViewBuilder
    private func syncState(_ service: MetadataService) -> some View {
        if model.accounts[service] == nil {
            Text("Not Connected")
        } else if model.pendingServices(current).contains(service) {
            HStack(spacing: 6) {
                if service == .bangumi, model.bangumiSignInExpired {
                    Text("Sign In Again to Sync")
                } else if service == .bangumi, current.listEntry.status == nil {
                    Text("Removal Pending")
                } else {
                    Text("Waiting to Sync")
                }
                Image(systemName: "clock")
                    .foregroundStyle(.orange)
            }
        } else {
            HStack(spacing: 6) {
                if let status = current.listEntry.status {
                    Text("\(status.displayName) · Synced")
                } else {
                    Text("Synced")
                }
                Image(systemName: "checkmark")
                    .font(.body.weight(.semibold))
                    .foregroundStyle(.green)
            }
        }
    }

    // MARK: About

    private func about(_ metadata: LinkedMetadata) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            if !metadata.tags.isEmpty {
                FlowLayout(spacing: 8) {
                    ForEach(metadata.tags, id: \.self) { tag in
                        Text(tag)
                            .font(.subheadline)
                            .padding(.horizontal, 12)
                            .frame(minHeight: 30)
                            .background(Color(uiColor: .tertiarySystemFill), in: Capsule())
                    }
                }
            }
            if let synopsis = metadata.synopsis {
                VStack(alignment: .leading, spacing: 4) {
                    Text(synopsis)
                        .font(.subheadline)
                        .lineSpacing(3)
                        .lineLimit(synopsisExpanded ? nil : 4)
                        .textSelection(.enabled)
                    if synopsis.count > 120 {
                        Button {
                            withAnimation(.easeInOut(duration: 0.2)) { synopsisExpanded.toggle() }
                        } label: {
                            Text(synopsisExpanded ? LocalizedStringKey("Show Less") : LocalizedStringKey("Show More"))
                        }
                        .font(.subheadline.weight(.semibold))
                    }
                }
            }
        }
    }

    private var linkPrompt: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Link this game to Bangumi or VNDB to show its cover, synopsis and tags, and to sync your status and rating to your lists.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Button { matching = true } label: {
                Label("Find on Bangumi & VNDB", systemImage: "link")
                    .frame(maxWidth: .infinity, minHeight: 30)
            }
            .nativeGlassButtonStyle()
        }
        .padding(16)
        .background(
            Color(uiColor: .secondarySystemGroupedBackground),
            in: RoundedRectangle(cornerRadius: 24, style: .continuous)
        )
    }

    // MARK: Information

    private var information: some View {
        DetailGroup("Information") {
            if let developer = metadata?.developer {
                DetailRow("Developer", value: developer, divider: false)
            }
            if let released = metadata?.released {
                DetailRow("Released", value: released, divider: metadata?.developer != nil)
            }
            DetailRow("Engine", value: current.engine.displayName, divider: metadata?.developer != nil || metadata?.released != nil)
            DetailRow("Folder", value: "Documents/\(current.folderName)")
            if startupChoices.count > 1 {
                DetailRow(label: "Startup file") { startupMenu }
            }
            if current.engine == .fvp || current.engine == .bgi {
                DetailRow(label: "Text encoding") { encodingMenu }
            }
            DetailRow("Size", value: AppModel.librarySize(current.byteCount))
            DetailRow("Launches", value: String(localized: "\(current.launchCount) times"))
            if !current.availability.canLaunch {
                DetailRow("Availability", value: model.availabilityText(current))
            }
        }
        .task(id: current.folderName) {
            startupChoices = model.startupChoices(current)
        }
    }

    /// Automatic follows the player's language (xxx_CHS.exe, 启动游戏.xp3 for
    /// Chinese, xxx_EN.exe for English); any listed file can be fixed instead.
    private var startupMenu: some View {
        Menu {
            Picker("Startup file", selection: Binding(
                get: { current.chosenStartup ?? "" },
                set: { model.chooseStartup($0.isEmpty ? nil : $0, for: current) }
            )) {
                Text("Automatic").tag("")
                ForEach(startupChoices, id: \.relativePath) { file in
                    Text(verbatim: startupName(file.relativePath)).tag(file.relativePath)
                }
            }
        } label: {
            HStack(spacing: 4) {
                if current.chosenStartup == nil {
                    Text("Automatic (\(startupName(current.launchTarget)))")
                } else {
                    Text(verbatim: startupName(current.launchTarget))
                }
                Image(systemName: "chevron.up.chevron.down")
                    .font(.footnote)
            }
            .lineLimit(1)
            .truncationMode(.middle)
        }
    }

    /// FVP scripts don't say how their text is encoded: Automatic reads
    /// translation patches (.bch) as GBK and original scripts as Shift-JIS.
    /// BGI games read GBK when the folder is named as a Chinese release.
    private var encodingMenu: some View {
        Menu {
            Picker("Text encoding", selection: Binding(
                get: { current.textEncoding?.rawValue ?? "" },
                set: { model.chooseTextEncoding(GameTextEncoding(rawValue: $0), for: current) }
            )) {
                Text("Automatic").tag("")
                ForEach(GameTextEncoding.choices(for: current.engine), id: \.rawValue) { encoding in
                    Text(verbatim: encoding.displayName).tag(encoding.rawValue)
                }
            }
        } label: {
            HStack(spacing: 4) {
                if current.textEncoding == nil {
                    Text("Automatic (\(model.textEncoding(for: current).displayName))")
                } else {
                    Text(verbatim: model.textEncoding(for: current).displayName)
                }
                Image(systemName: "chevron.up.chevron.down")
                    .font(.footnote)
            }
            .lineLimit(1)
        }
    }

    /// "." is the folder itself: startup.tjs for KiriKiri, the original
    /// files (Scene.pck and Gameexe.dat) for Siglus.
    private func startupName(_ relativePath: String?) -> String {
        AppModel.startupName(relativePath, engine: current.engine)
    }

    private var links: some View {
        VStack(alignment: .leading, spacing: 8) {
            DetailGroup {
                if let url = current.link?.bangumiURL {
                    linkRow("Open in Bangumi", url: url, divider: false)
                }
                if let url = current.link?.vndbURL {
                    linkRow("Open in VNDB", url: url, divider: current.link?.bangumiURL != nil)
                }
            }
            Text(credit)
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.horizontal, 16)
        }
    }

    private func linkRow(_ title: LocalizedStringKey, url: URL, divider: Bool) -> some View {
        VStack(spacing: 0) {
            if divider { Divider().padding(.leading, 16) }
            Link(destination: url) {
                HStack {
                    Text(title)
                    Spacer()
                    Image(systemName: "arrow.up.right")
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(.tertiary)
                }
                .padding(.horizontal, 16)
                .frame(minHeight: 50)
            }
        }
    }

    private var credit: String {
        let link = current.link
        switch (link?.bangumiID != nil, link?.vndbID != nil) {
        case (true, true):
            return metadata?.synopsisSource == .vndb
                ? String(localized: "Details from Bangumi and VNDB. Screenshots from VNDB.")
                : String(localized: "Synopsis and tags from Bangumi. Developer details and screenshots from VNDB.")
        case (false, true):
            return String(localized: "Details and screenshots from VNDB.")
        default:
            return String(localized: "Details from Bangumi.")
        }
    }
}
