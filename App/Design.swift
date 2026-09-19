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
    let game: GameRecord
    var body: some View {
        GeometryReader { proxy in
            if let url = model.coverURL(game), let image = UIImage(contentsOfFile: url.path) {
                Image(uiImage: image).resizable().scaledToFill().frame(width: proxy.size.width, height: proxy.size.height).clipped()
            } else {
                Color(uiColor: .secondarySystemGroupedBackground)
                    .overlay(
                        Image(systemName: "book.closed.fill")
                            .font(.system(size: 34))
                            .foregroundStyle(.secondary)
                    )
            }
        }
    }
}
