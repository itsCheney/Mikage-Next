import SwiftUI

/// A landscape phone with a letterboxed game and the floating buttons where
/// they rest: in the bars beside the game, at the spots the player dragged
/// them to, or at their default corners. Settings › Floating Button and the
/// welcome guide both draw it; the guide numbers the parts.
struct FloatingControlsMap: View {
    var showsMenuButton = true
    var opacity: Double = 1
    var actions: [QuickActionKind]
    var showsStats = false
    var numbered = false
    /// Changes when the positions are reset, so the map reloads them.
    var revision = 0

    private static let accent = Color(FloatingButtonStyle.accent)
    /// The phone's screen, 19.5:9, and a 4:3 game inside it.
    private static let screenAspect: CGFloat = 19.5 / 9
    private static let gameAspect: CGFloat = 4 / 3

    var body: some View {
        GeometryReader { proxy in
            let screen = proxy.size
            let game = CGSize(width: screen.height * Self.gameAspect, height: screen.height)
            let bar = (screen.width - game.width) / 2
            // Buttons are drawn larger than true scale so they stay legible.
            let scale = max(0.5, screen.width / 600)
            let markers = layout(screen: screen, bar: bar, scale: scale)

            ZStack(alignment: .topLeading) {
                Color.black
                gameArea
                    .frame(width: game.width, height: game.height)
                    .offset(x: bar)
                // The Dynamic Island, on the left in landscape.
                Capsule()
                    .fill(Color(white: 0.12))
                    .frame(width: screen.height * 0.09, height: screen.height * 0.3)
                    .offset(x: screen.width * 0.012, y: screen.height * 0.35)

                if showsStats {
                    statsPill(scale: scale)
                        .offset(x: bar + 6 * scale, y: 6 * scale)
                }
                ForEach(markers) { marker in
                    marker.view
                        .opacity(opacity)
                        .overlay(alignment: .topLeading) {
                            if numbered, let number = marker.number {
                                badge(number, scale: scale)
                                    .offset(x: -6 * scale, y: -6 * scale)
                            }
                        }
                        .position(marker.center)
                }
                if numbered, showsStats {
                    badge(3, scale: scale)
                        .offset(x: bar + 2 * scale, y: 2 * scale)
                }
            }
            .frame(width: screen.width, height: screen.height)
        }
        .aspectRatio(Self.screenAspect, contentMode: .fit)
        .clipShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 22, style: .continuous)
                .strokeBorder(Color(white: 0.3), lineWidth: 3)
        )
        .id(revision)
        .accessibilityHidden(true)
    }

    private var gameArea: some View {
        LinearGradient(
            colors: [Color(red: 0.36, green: 0.42, blue: 0.62), Color(red: 0.18, green: 0.2, blue: 0.32)],
            startPoint: .top,
            endPoint: .bottom
        )
        .overlay {
            // A faint grid stands in for the game's own picture.
            Canvas { context, size in
                var path = Path()
                for i in 1..<6 {
                    let x = size.width * CGFloat(i) / 6
                    path.move(to: CGPoint(x: x, y: 0))
                    path.addLine(to: CGPoint(x: x, y: size.height))
                }
                for i in 1..<4 {
                    let y = size.height * CGFloat(i) / 4
                    path.move(to: CGPoint(x: 0, y: y))
                    path.addLine(to: CGPoint(x: size.width, y: y))
                }
                context.stroke(path, with: .color(.white.opacity(0.08)), lineWidth: 1)
            }
        }
        .overlay(alignment: .bottom) {
            // The text window.
            RoundedRectangle(cornerRadius: 6)
                .fill(Color.black.opacity(0.35))
                .frame(height: 50)
                .padding(10)
                .opacity(0.8)
        }
    }

    private struct Marker: Identifiable {
        let id: String
        let center: CGPoint
        let number: Int?
        let view: AnyView
    }

    /// Each button centered in the bar on its side, at its saved height,
    /// and moved along the bar until it no longer overlaps one placed first.
    private func layout(screen: CGSize, bar: CGFloat, scale: CGFloat) -> [Marker] {
        let saved = FloatingControlPositions.load()
        var entries: [(id: String, size: CGFloat, index: Int?, view: AnyView, number: Int?)] = []
        if showsMenuButton {
            entries.append(("ball", FloatingControlsView.ballSize * scale, nil,
                            AnyView(button(symbol: "moon.stars.fill", size: FloatingControlsView.ballSize * scale, fill: Self.accent)),
                            2))
        }
        for (index, action) in actions.prefix(QuickActionKind.limit).enumerated() {
            entries.append((action.rawValue, FloatingControlsView.quickActionSize * scale, index,
                            AnyView(button(symbol: action.symbol, size: FloatingControlsView.quickActionSize * scale, fill: Color(white: 0.2))),
                            index == 0 ? 1 : nil))
        }

        let inset = 8 * scale
        var placed: [CGRect] = []
        var markers: [Marker] = []
        for entry in entries {
            let fraction = saved[entry.id]
                ?? FloatingControlPositions.defaultPosition(for: entry.id, quickActionIndex: entry.index)
            let x = fraction.x < 0.5 ? bar / 2 : screen.width - bar / 2
            let half = entry.size / 2
            var y = min(max(fraction.y * screen.height, inset + half), screen.height - inset - half)
            let step = (entry.size + 8 * scale) * (y > screen.height / 2 ? -1 : 1)
            var frame = CGRect(x: x - half, y: y - half, width: entry.size, height: entry.size)
            var tries = 0
            while placed.contains(where: { $0.insetBy(dx: -2, dy: -2).intersects(frame) }), tries < 6 {
                y += step
                frame.origin.y = y - half
                tries += 1
            }
            placed.append(frame)
            markers.append(Marker(id: entry.id, center: CGPoint(x: x, y: y), number: entry.number, view: entry.view))
        }
        return markers
    }

    private func button(symbol: String, size: CGFloat, fill: Color) -> some View {
        Image(systemName: symbol)
            .font(.system(size: size * 0.42, weight: .semibold))
            .foregroundStyle(.white)
            .frame(width: size, height: size)
            .background(Circle().fill(fill))
            .overlay(Circle().strokeBorder(Color.white.opacity(0.22)))
    }

    private func statsPill(scale: CGFloat) -> some View {
        HStack(spacing: 4 * scale) {
            Circle().fill(Color.green).frame(width: 5 * scale, height: 5 * scale)
            Text(verbatim: "60 FPS")
                .font(.system(size: 9 * scale, weight: .semibold, design: .rounded))
                .monospacedDigit()
        }
        .foregroundStyle(.white)
        .padding(.horizontal, 7 * scale)
        .padding(.vertical, 4 * scale)
        .background(Capsule().fill(Color.black.opacity(0.55)))
    }

    private func badge(_ number: Int, scale: CGFloat) -> some View {
        Text(verbatim: "\(number)")
            .font(.system(size: 10 * scale, weight: .bold, design: .rounded))
            .foregroundStyle(.black)
            .frame(width: 16 * scale, height: 16 * scale)
            .background(Circle().fill(Color.yellow))
            .overlay(Circle().strokeBorder(Color.black.opacity(0.4), lineWidth: 1))
    }
}

/// The key under the map: what each kind of button is.
struct FloatingControlsLegend: View {
    var showsStats: Bool

    var body: some View {
        HStack(spacing: 14) {
            item(Color(FloatingButtonStyle.accent), Text("Menu Button"))
            item(Color(white: 0.35), Text("Quick Actions"))
            if showsStats {
                item(Color.green, Text("Stats"))
            }
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .frame(maxWidth: .infinity)
    }

    private func item(_ color: Color, _ label: Text) -> some View {
        HStack(spacing: 5) {
            Circle().fill(color).frame(width: 8, height: 8)
            label
        }
    }
}
