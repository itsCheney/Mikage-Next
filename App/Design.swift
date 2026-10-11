import SwiftUI
import VNCore

struct AppBackground: View {
    @Environment(\.colorScheme) private var scheme
    var body: some View {
        (scheme == .dark
            ? Color.black
            : Color(red: 242 / 255, green: 242 / 255, blue: 246 / 255))
            .ignoresSafeArea()
    }
}

extension TimeInterval {
    /// `H:MM:SS` elapsed time. Negative and non-finite values read as zero.
    var playTimeClock: String {
        guard isFinite, self > 0 else { return "0:00:00" }
        let total = Int(self.rounded())
        return String(format: "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60)
    }

    /// Short form for summaries: "3 小时 12 分" / "12 分" / "不到 1 分钟".
    var playTimeSummary: String {
        guard isFinite, self >= 60 else { return "不到 1 分钟" }
        let minutes = Int(self) / 60
        let hours = minutes / 60
        return hours > 0 ? "\(hours) 小时 \(minutes % 60) 分" : "\(minutes) 分"
    }
}

extension View {
    @ViewBuilder
    func nativeGlassButtonStyle(prominent: Bool = false) -> some View {
        if #available(iOS 26.0, *) {
            if prominent {
                buttonStyle(.glassProminent)
            } else {
                buttonStyle(.glass)
            }
        } else if prominent {
            buttonStyle(.borderedProminent)
        } else {
            buttonStyle(.bordered)
        }
    }

    @ViewBuilder
    func nativeGlassPanel(_ radius: CGFloat = 24) -> some View {
        if #available(iOS 26.0, *) {
            glassEffect(in: .rect(cornerRadius: radius))
        } else {
            background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
        }
    }
}

struct RoundButton: View {
    let symbol: String
    let label: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label(label, systemImage: symbol)
                .labelStyle(.iconOnly)
                .font(.title3.weight(.medium))
                .frame(width: 44, height: 44)
        }
        .nativeGlassButtonStyle()
        .accessibilityLabel(label)
    }
}

struct GameCover: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.displayScale) private var displayScale
    let game: GameRecord
    @State private var image: UIImage?

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                placeholder
                if let image {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFill()
                        .frame(width: proxy.size.width, height: proxy.size.height)
                        .clipped()
                        .transition(.opacity)
                }
            }
            // Re-runs when the cover file or the laid-out size changes. Decoding
            // happens off the main thread and is cached at display size.
            .task(id: coverIdentity(proxy.size)) {
                await load(size: proxy.size)
            }
        }
    }

    private var placeholder: some View {
        Color(uiColor: .secondarySystemGroupedBackground)
            .overlay(
                Image(systemName: "book.closed.fill")
                    .font(.system(size: 34))
                    .foregroundStyle(.secondary)
            )
    }

    private func coverIdentity(_ size: CGSize) -> String {
        let url = model.coverURL(game)?.path ?? "none"
        return "\(url)|\(Int(size.width))x\(Int(size.height))"
    }

    private func load(size: CGSize) async {
        guard size.width > 0, size.height > 0, let url = model.coverURL(game) else {
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
