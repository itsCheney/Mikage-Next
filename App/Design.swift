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

    /// Short form for summaries: "3 hr 12 min" / "12 min" / "Less than 1 min".
    var playTimeSummary: String {
        guard isFinite, self >= 60 else { return String(localized: "Less than 1 min") }
        let minutes = Int(self) / 60
        let hours = minutes / 60
        return hours > 0
            ? String(localized: "\(hours) hr \(minutes % 60) min")
            : String(localized: "\(minutes) min")
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

    /// Interactive glass capsule for a toolbar-like group of icon buttons
    /// drawn inside content, such as the pinned library header.
    @ViewBuilder
    func nativeGlassCapsule() -> some View {
        if #available(iOS 26.0, *) {
            glassEffect(.regular.interactive(), in: .capsule)
        } else {
            background(.ultraThinMaterial, in: Capsule())
        }
    }
}

/// Settings row label whose icon takes the app accent, as in the redesign.
struct SettingLabel: View {
    let title: LocalizedStringKey
    let symbol: String

    init(_ title: LocalizedStringKey, systemImage symbol: String) {
        self.title = title
        self.symbol = symbol
    }

    var body: some View {
        Label {
            Text(title)
                .foregroundStyle(.primary)
        } icon: {
            Image(systemName: symbol)
                .foregroundStyle(.tint)
        }
    }
}

struct RoundButton: View {
    let symbol: String
    let label: LocalizedStringKey
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
        ArtworkImage(url: model.coverURL(game))
    }
}
