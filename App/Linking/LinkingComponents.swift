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
                if let image {
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
            VStack(alignment: .leading, spacing: 1) {
                Text(toast.title)
                    .font(.subheadline.weight(.semibold))
                Text(toast.detail)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            if toast.previous != nil {
                Button("Undo") { model.undoToast() }
                    .font(.subheadline.weight(.semibold))
                    .nativeGlassButtonStyle()
            }
        }
        .padding(.leading, 18)
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
