import SwiftUI
import VNCore

extension PlayStatus {
    var displayName: String {
        switch self {
        case .wish: return String(localized: "Want to Play")
        case .playing: return String(localized: "Playing")
        case .finished: return String(localized: "Finished")
        case .onHold: return String(localized: "On Hold")
        case .dropped: return String(localized: "Dropped")
        }
    }

    var symbol: String {
        switch self {
        case .wish: return "bookmark"
        case .playing: return "play.circle.fill"
        case .finished: return "checkmark.circle.fill"
        case .onHold: return "pause.circle.fill"
        case .dropped: return "xmark.circle.fill"
        }
    }

    var color: Color {
        switch self {
        case .wish: return .secondary
        case .playing: return .indigo
        case .finished: return .green
        case .onHold: return .orange
        case .dropped: return Color(uiColor: .systemGray)
        }
    }
}

/// Bangumi's words for each point on the 1–10 scale.
enum RatingLabel {
    static func text(for rating: Int) -> String {
        switch rating {
        case 1: return String(localized: "Unbearable")
        case 2: return String(localized: "Very Bad")
        case 3: return String(localized: "Bad")
        case 4: return String(localized: "Weak")
        case 5: return String(localized: "So-so")
        case 6: return String(localized: "Fine")
        case 7: return String(localized: "Good")
        case 8: return String(localized: "Great")
        case 9: return String(localized: "Masterpiece")
        default: return String(localized: "All-Time Favorite")
        }
    }
}

/// The status choices shared by the details page and the long-press menu.
/// Picking Finished is handed back to the caller, which opens the rating
/// sheet instead of applying it directly.
struct StatusMenuItems: View {
    let current: PlayStatus?
    let select: (PlayStatus?) -> Void

    var body: some View {
        ForEach(PlayStatus.allCases) { status in
            Button { select(status) } label: {
                if current == status {
                    Label(status.displayName, systemImage: "checkmark")
                } else {
                    Text(status.displayName)
                }
            }
        }
        if current != nil {
            Divider()
            Button(role: .destructive) { select(nil) } label: {
                Label("Remove Status", systemImage: "minus.circle")
            }
        }
    }
}

/// A local image file decoded off the main thread at display size, with a
/// placeholder while it loads or when there is none.
struct ArtworkImage: View {
    @Environment(\.displayScale) private var displayScale
    let url: URL?
    @ObservedObject private var covers = CoverCache.shared
    @State private var image: UIImage?

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                Color(uiColor: .secondarySystemGroupedBackground)
                    .overlay(
                        Image(systemName: "book.closed.fill")
                            .font(.system(size: min(34, max(14, proxy.size.width / 4))))
                            .foregroundStyle(.secondary)
                    )
                // A cache hit draws on the first frame, before the load task
                // runs, so a cover shown again (as when a launch moves it to
                // a new view) never flashes the placeholder.
                if let image = image ?? cachedImage(size: proxy.size) {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFill()
                        .frame(width: proxy.size.width, height: proxy.size.height)
                        .clipped()
                        .transition(.opacity)
                }
            }
            // Re-runs when the file or the laid-out size changes.
            .task(id: "\(url?.path ?? "none")|\(covers.generation)|\(Int(proxy.size.width))x\(Int(proxy.size.height))") {
                await load(size: proxy.size)
            }
        }
        .accessibilityHidden(true)
    }

    private func cachedImage(size: CGSize) -> UIImage? {
        guard let url, size.width > 0, size.height > 0 else { return nil }
        return CoverCache.shared.cached(url, size: size, scale: displayScale)
    }

    private func load(size: CGSize) async {
        guard size.width > 0, size.height > 0, let url else {
            image = nil
            return
        }
        // Show a cache hit without an animation so scrolling does not flicker.
        if let hit = CoverCache.shared.cached(url, size: size, scale: displayScale) {
            image = hit
            return
        }
        let decoded = await CoverCache.shared.image(for: url, size: size, scale: displayScale)
        guard !Task.isCancelled else { return }
        withAnimation(.easeOut(duration: 0.18)) { image = decoded }
    }
}

/// Cover art for the Adaptive card layout. A cover narrower than
/// `CardArtwork.fitBelow` shows whole, centered over a blurred copy of
/// itself, as the Continue Playing hero does; a wider one fills the frame,
/// cropped around what Vision finds most salient rather than the center.
struct AdaptiveArtworkImage: View {
    @Environment(\.displayScale) private var displayScale
    let url: URL?
    @ObservedObject private var covers = CoverCache.shared
    @State private var artwork: CardArtwork?

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                Color(uiColor: .secondarySystemGroupedBackground)
                    .overlay(
                        Image(systemName: "book.closed.fill")
                            .font(.system(size: min(34, max(14, proxy.size.width / 4))))
                            .foregroundStyle(.secondary)
                    )
                if let artwork = artwork ?? cachedArtwork(size: proxy.size) {
                    content(artwork, size: proxy.size)
                        .transition(.opacity)
                }
            }
            .task(id: "\(url?.path ?? "none")|\(covers.generation)|\(Int(proxy.size.width))x\(Int(proxy.size.height))") {
                await load(size: proxy.size)
            }
        }
        .accessibilityHidden(true)
    }

    @ViewBuilder
    private func content(_ artwork: CardArtwork, size: CGSize) -> some View {
        if let backdrop = artwork.backdrop {
            ZStack {
                Image(uiImage: backdrop)
                    .resizable()
                    .scaledToFill()
                    .frame(width: size.width, height: size.height)
                    .clipped()
                    .overlay(Color.black.opacity(0.22))
                Image(uiImage: artwork.cover)
                    .resizable()
                    .scaledToFit()
                    .frame(width: size.width, height: size.height)
                    .shadow(color: .black.opacity(0.35), radius: 8)
            }
        } else {
            let rect = CardArtwork.fillRect(
                aspect: artwork.aspect,
                in: size,
                focus: artwork.focus ?? CGPoint(x: 0.5, y: 0.5)
            )
            Image(uiImage: artwork.cover)
                .resizable()
                .frame(width: rect.width, height: rect.height)
                .offset(x: rect.minX, y: rect.minY)
                .frame(width: size.width, height: size.height, alignment: .topLeading)
                .clipped()
        }
    }

    private func cachedArtwork(size: CGSize) -> CardArtwork? {
        guard let url, size.width > 0, size.height > 0 else { return nil }
        return CoverCache.shared.cachedCardArtwork(url, size: size, scale: displayScale)
    }

    private func load(size: CGSize) async {
        guard size.width > 0, size.height > 0, let url else {
            artwork = nil
            return
        }
        if let hit = CoverCache.shared.cachedCardArtwork(url, size: size, scale: displayScale) {
            artwork = hit
            return
        }
        let prepared = await CoverCache.shared.cardArtwork(for: url, size: size, scale: displayScale)
        guard !Task.isCancelled else { return }
        withAnimation(.easeOut(duration: 0.18)) { artwork = prepared }
    }
}

/// Columns of equal width; each child goes under the shortest column, at
/// the height it asks for at that width.
struct MasonryLayout: Layout {
    var minimumColumnWidth: CGFloat
    var spacing: CGFloat

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? minimumColumnWidth * 2 + spacing
        let frames = arrange(subviews, width: width)
        return CGSize(width: width, height: frames.map(\.maxY).max() ?? 0)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        for (index, frame) in arrange(subviews, width: bounds.width).enumerated() {
            subviews[index].place(
                at: CGPoint(x: bounds.minX + frame.minX, y: bounds.minY + frame.minY),
                proposal: ProposedViewSize(width: frame.width, height: frame.height)
            )
        }
    }

    /// Matches `GridItem(.adaptive(minimum:))`: as many columns as fit.
    static func columnCount(width: CGFloat, minimum: CGFloat, spacing: CGFloat) -> Int {
        guard width.isFinite, width > 0 else { return 1 }
        return max(1, Int((width + spacing) / (minimum + spacing)))
    }

    private func arrange(_ subviews: Subviews, width: CGFloat) -> [CGRect] {
        let heights: [(CGFloat) -> CGFloat] = subviews.map { subview in
            { columnWidth in
                subview.sizeThatFits(ProposedViewSize(width: columnWidth, height: nil)).height
            }
        }
        return Self.frames(
            width: width,
            columns: Self.columnCount(width: width, minimum: minimumColumnWidth, spacing: spacing),
            spacing: spacing,
            heights: heights
        )
    }

    /// The frame of each child, given its height at a column width.
    static func frames(
        width: CGFloat,
        columns: Int,
        spacing: CGFloat,
        heights: [(CGFloat) -> CGFloat]
    ) -> [CGRect] {
        let columns = max(1, columns)
        let columnWidth = max(0, (width - spacing * CGFloat(columns - 1)) / CGFloat(columns))
        var bottoms = [CGFloat](repeating: 0, count: columns)
        return heights.map { height in
            // The leftmost of the shortest columns, so equal heights read
            // left to right.
            let column = bottoms.indices.min { bottoms[$0] < bottoms[$1] } ?? 0
            let h = height(columnWidth)
            let frame = CGRect(
                x: CGFloat(column) * (columnWidth + spacing),
                y: bottoms[column],
                width: columnWidth,
                height: h
            )
            bottoms[column] += h + spacing
            return frame
        }
    }
}

/// Lays children out in rows, wrapping to the next row when one is full.
struct FlowLayout: Layout {
    var spacing: CGFloat = 8

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = arrange(subviews, width: proposal.width ?? .infinity)
        let width = rows.map(\.width).max() ?? 0
        let height = rows.map(\.height).reduce(0, +) + spacing * CGFloat(max(rows.count - 1, 0))
        return CGSize(width: proposal.width ?? width, height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var y = bounds.minY
        for row in arrange(subviews, width: bounds.width) {
            var x = bounds.minX
            for index in row.indices {
                let size = subviews[index].sizeThatFits(.unspecified)
                subviews[index].place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
            y += row.height + spacing
        }
    }

    private struct Row {
        var indices: [Int] = []
        var width: CGFloat = 0
        var height: CGFloat = 0
    }

    private func arrange(_ subviews: Subviews, width: CGFloat) -> [Row] {
        var rows: [Row] = []
        var current = Row()
        for index in subviews.indices {
            let size = subviews[index].sizeThatFits(.unspecified)
            let needed = current.indices.isEmpty ? size.width : current.width + spacing + size.width
            if needed > width, !current.indices.isEmpty {
                rows.append(current)
                current = Row()
            }
            current.width = current.indices.isEmpty ? size.width : current.width + spacing + size.width
            current.height = max(current.height, size.height)
            current.indices.append(index)
        }
        if !current.indices.isEmpty { rows.append(current) }
        return rows
    }
}

/// A white, rounded group of rows as in Settings, for use inside a scroll
/// view where `Form` cannot go.
struct DetailGroup<Content: View>: View {
    let title: LocalizedStringKey?
    @ViewBuilder let content: Content

    init(_ title: LocalizedStringKey? = nil, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 7) {
            if let title {
                Text(title)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 16)
                    .accessibilityAddTraits(.isHeader)
            }
            VStack(spacing: 0) {
                content
            }
            .background(
                Color(uiColor: .secondarySystemGroupedBackground),
                in: RoundedRectangle(cornerRadius: 24, style: .continuous)
            )
        }
    }
}

/// One row of a `DetailGroup`. `divider` draws the hairline above it.
struct DetailRow<Value: View>: View {
    let label: LocalizedStringKey
    var divider = true
    @ViewBuilder let value: Value

    var body: some View {
        VStack(spacing: 0) {
            if divider { Divider().padding(.leading, 16) }
            HStack(spacing: 12) {
                Text(label)
                Spacer(minLength: 8)
                value
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.trailing)
            }
            .font(.body)
            .padding(.horizontal, 16)
            .frame(minHeight: 50)
            .accessibilityElement(children: .combine)
        }
    }
}

extension DetailRow where Value == Text {
    init(_ label: LocalizedStringKey, value: String, divider: Bool = true) {
        self.label = label
        self.divider = divider
        self.value = Text(value)
    }
}

/// The notice that appears over the library for about four seconds after a
/// game's status changed by itself.
struct LibraryToastView: View {
    @EnvironmentObject private var model: AppModel
    let toast: LibraryToast

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "checkmark")
                .font(.system(size: 12, weight: .heavy))
                .foregroundStyle(.white)
                .frame(width: 26, height: 26)
                .background(Circle().fill(Color(red: 0x24 / 255, green: 0x8A / 255, blue: 0x3D / 255)))
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 1) {
                Text(toast.title)
                    .font(.subheadline.weight(.semibold))
                Text(toast.detail)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            if toast.previous != nil {
                // A plain capsule: the glass button style pads itself and
                // sat off the toast's trailing edge.
                Button { model.undoToast() } label: {
                    Text("Undo")
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(Color.accentColor)
                        .padding(.horizontal, 16)
                        .frame(height: 44)
                        .background(Color(uiColor: .systemBackground).opacity(0.7), in: Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.leading, 16)
        .padding(.trailing, 8)
        .padding(.vertical, 8)
        .frame(minHeight: 64)
        .nativeGlassPanel(32)
        .accessibilityElement(children: .contain)
        .task(id: toast.id) {
            try? await Task.sleep(nanoseconds: 4_000_000_000)
            if model.toast?.id == toast.id {
                withAnimation(.easeInOut(duration: 0.25)) { model.toast = nil }
            }
        }
    }
}

/// A remote thumbnail for search results, blurred when it is marked
/// sensitive and the player chose to hide such images.
struct RemoteThumbnail: View {
    let url: String?
    let blurred: Bool

    var body: some View {
        AsyncImage(url: url.flatMap { URL(string: $0) }) { phase in
            if let image = phase.image {
                image
                    .resizable()
                    .scaledToFill()
                    .blur(radius: blurred ? 12 : 0)
            } else {
                Color(uiColor: .tertiarySystemFill)
                    .overlay(
                        Image(systemName: "book.closed.fill")
                            .foregroundStyle(.secondary)
                    )
            }
        }
        .clipped()
        .accessibilityHidden(true)
    }
}
