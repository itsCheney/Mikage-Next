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
                LinearGradient(colors: [.indigo.opacity(0.7), .blue.opacity(0.3)], startPoint: .topLeading, endPoint: .bottomTrailing)
                    .overlay(Image(systemName: "book.closed.fill").font(.system(size: 34)).foregroundStyle(.white.opacity(0.5)))
            }
        }
    }
}
