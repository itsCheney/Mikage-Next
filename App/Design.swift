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

struct SkyArtwork: View {
    /// Original vector placeholder; no game artwork is bundled.
    var body: some View {
        GeometryReader { proxy in
            ZStack {
                LinearGradient(colors: [Color(red: 0.02, green: 0.25, blue: 0.63), Color(red: 0.06, green: 0.57, blue: 0.84), Color(red: 0.74, green: 0.86, blue: 0.85)], startPoint: .top, endPoint: .bottomTrailing)
                Canvas { context, size in
                    for i in 0..<12 {
                        let x = CGFloat(i) * size.width / 10
                        let y = size.height * (0.64 + CGFloat(i % 3) * 0.05)
                        let cloud = Path(ellipseIn: CGRect(x: x - 35, y: y - 22, width: 88, height: 48))
                        context.fill(cloud, with: .color(.white.opacity(0.33)))
                    }
                    for (x, y, scale) in [(0.18,0.36,0.60),(0.57,0.50,0.38),(0.90,0.30,0.83)] {
                        let center = CGPoint(x: size.width * x, y: size.height * y)
                        let length = min(size.width * 0.17, size.height * 0.65) * scale
                        var mast = Path(); mast.move(to: center); mast.addLine(to: CGPoint(x: center.x - 5, y: size.height))
                        context.stroke(mast, with: .color(.white.opacity(0.70)), lineWidth: 3 * scale)
                        for blade in 0..<3 {
                            let angle = Double(blade) * .pi * 2 / 3 - 1.4
                            var path = Path(); path.move(to: center)
                            path.addLine(to: CGPoint(x: center.x + cos(angle) * length, y: center.y + sin(angle) * length))
                            context.stroke(path, with: .color(.white.opacity(0.83)), style: StrokeStyle(lineWidth: 5 * scale, lineCap: .round))
                        }
                    }
                }
                VStack(spacing: 3) {
                    Text("青 空 下 的 加 缪").font(.system(size: min(proxy.size.width / 12, 29), weight: .light, design: .serif)).tracking(4)
                    Text("C A M U S   I N   T H E   B L U E   S K Y").font(.system(size: 7, weight: .semibold)).tracking(1)
                }.foregroundStyle(Color(red: 0.93, green: 0.87, blue: 0.67).opacity(0.80))
                    .frame(maxHeight: .infinity, alignment: .top).padding(.top, 7)
            }
        }.clipped().accessibilityHidden(true)
    }
}

struct GameCover: View {
    @EnvironmentObject private var model: AppModel
    let game: GameRecord
    var body: some View {
        GeometryReader { proxy in
            if let url = model.coverURL(game), let image = UIImage(contentsOfFile: url.path) {
                Image(uiImage: image).resizable().scaledToFill().frame(width: proxy.size.width, height: proxy.size.height).clipped()
            } else if game.id == AppModel.sample.id { SkyArtwork() }
            else {
                LinearGradient(colors: [.indigo.opacity(0.7), .blue.opacity(0.3)], startPoint: .topLeading, endPoint: .bottomTrailing)
                    .overlay(Image(systemName: "book.closed.fill").font(.system(size: 34)).foregroundStyle(.white.opacity(0.5)))
            }
        }
    }
}
