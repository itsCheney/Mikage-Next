import SwiftUI
import VNCore

/// Where each launchable cover sits on screen, so a launch can grow the
/// cover out of the card the player tapped. Updated as cards lay out and
/// scroll; plain storage, never observed, so it costs no redraws.
final class LaunchSourceFrames {
    static let shared = LaunchSourceFrames()
    private var frames: [String: CGRect] = [:]

    private init() {}

    static func cardKey(_ id: UUID) -> String { "card-\(id.uuidString)" }
    static let heroKey = "hero"

    func set(_ frame: CGRect, for key: String) { frames[key] = frame }
    func remove(_ key: String) { frames[key] = nil }

    /// The frame in window coordinates, if it is at least partly on screen.
    func frame(for key: String) -> CGRect? {
        guard let frame = frames[key], frame.width > 1, frame.height > 1 else { return nil }
        let screen = UIScreen.main.bounds
        return frame.intersects(screen) ? frame : nil
    }
}

/// Records the frame of the view it is the background of.
struct LaunchSourceReader: View {
    let key: String

    var body: some View {
        GeometryReader { proxy in
            Color.clear
                .onAppear { LaunchSourceFrames.shared.set(proxy.frame(in: .global), for: key) }
                .onChange(of: proxy.frame(in: .global)) { LaunchSourceFrames.shared.set($0, for: key) }
                .onDisappear { LaunchSourceFrames.shared.remove(key) }
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

/// The cover growing from its card to fill the screen as a game launches,
/// or fading back over the library as it closes.
struct LaunchTransition: Identifiable, Equatable {
    enum Direction: Equatable {
        /// Grows from `source` to full screen, then the player covers it.
        case entering
        /// Starts full screen over the library and fades away.
        case leaving
    }

    let id = UUID()
    let game: GameRecord
    let source: CGRect?
    let direction: Direction
    /// The corner radius of the card the cover starts in.
    let sourceRadius: CGFloat

    static func == (lhs: LaunchTransition, rhs: LaunchTransition) -> Bool { lhs.id == rhs.id }

    /// How long the cover takes to fill the screen.
    static let enterDuration: TimeInterval = 0.42
    static let leaveDuration: TimeInterval = 0.32
}

/// A game's cover drawn the way its library card draws it: center-cropped,
/// or fitted as the Adaptive layout does. Used at any size, from the card to
/// the whole screen, so the launch reads as one continuous image.
struct LaunchArtwork: View {
    @EnvironmentObject private var model: AppModel
    let game: GameRecord

    var body: some View {
        GameCover(game: game, adaptive: model.settings.coverLayout == .adaptive)
    }
}

/// Drawn by `RootView` over the library: the tapped cover expands to the
/// screen (entering), or the game's cover dissolves back into the library
/// (leaving). The player is presented underneath it without an animation.
struct LaunchTransitionOverlay: View {
    @EnvironmentObject private var model: AppModel
    let transition: LaunchTransition
    @State private var expanded = false
    @State private var visible = true

    var body: some View {
        GeometryReader { proxy in
            let screen = CGRect(origin: .zero, size: proxy.size)
            let from = transition.source ?? screen.insetBy(dx: screen.width * 0.04, dy: screen.height * 0.04)
            let frame = expanded ? screen : from
            ZStack {
                LaunchArtwork(game: transition.game)
                // The darkening the player's own stage starts with.
                LinearGradient(
                    colors: [.black.opacity(0.1), .black.opacity(0.55)],
                    startPoint: .top,
                    endPoint: .bottom
                )
                .opacity(expanded ? 1 : 0)
            }
            .frame(width: frame.width, height: frame.height)
            .clipShape(RoundedRectangle(cornerRadius: expanded ? 0 : transition.sourceRadius, style: .continuous))
            .position(x: frame.midX, y: frame.midY)
            .opacity(visible ? 1 : 0)
        }
        .ignoresSafeArea()
        .allowsHitTesting(false)
        .accessibilityHidden(true)
        .onAppear(perform: run)
    }

    private func run() {
        switch transition.direction {
        case .entering:
            // Without a card to grow from, the cover fades in near full size.
            if transition.source == nil { visible = false }
            withAnimation(.timingCurve(0.3, 0.7, 0.2, 1, duration: LaunchTransition.enterDuration)) {
                expanded = true
                visible = true
            }
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: UInt64((LaunchTransition.enterDuration + 0.03) * 1_000_000_000))
                model.coverDidFillScreen(transition)
            }
        case .leaving:
            expanded = true
            withAnimation(.easeOut(duration: LaunchTransition.leaveDuration)) {
                visible = false
            }
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: UInt64(LaunchTransition.leaveDuration * 1_000_000_000))
                model.transitionDidEnd(transition)
            }
        }
    }
}

/// What the player shows while a game starts and after it ends: the cover
/// filling the portrait screen at first, then, once the scene turns to
/// landscape, the cover beside the title and a loading bar. The engine's
/// window fades in over it when the game's first frames are ready.
struct LaunchStage: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.displayScale) private var displayScale
    let game: GameRecord
    let ending: Bool
    @State private var artwork: HeroArtwork?
    @State private var infoShown = false

    var body: some View {
        GeometryReader { proxy in
            let size = proxy.size
            ZStack {
                Color.black
                if size.width > size.height {
                    landscape(size)
                        .transition(.opacity)
                } else {
                    portrait
                        .transition(.opacity)
                }
            }
            .animation(.easeInOut(duration: 0.3), value: size.width > size.height)
        }
        .ignoresSafeArea()
        .task(id: model.coverURL(game)?.path) { await load() }
    }

    private var portrait: some View {
        ZStack {
            LaunchArtwork(game: game)
            LinearGradient(
                colors: [.black.opacity(0.1), .black.opacity(0.55)],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        .accessibilityHidden(true)
    }

    private func landscape(_ size: CGSize) -> some View {
        let coverHeight = size.height * 0.74
        return ZStack(alignment: .leading) {
            if model.settings.background == .cover, let artwork {
                Image(uiImage: artwork.blurred)
                    .resizable()
                    .scaledToFill()
                    .frame(width: size.width, height: size.height)
                    .clipped()
                    .overlay(Color.black.opacity(0.35))
                    .accessibilityHidden(true)
            }
            HStack(spacing: size.width * 0.05) {
                cover
                    .frame(maxWidth: size.width * 0.42, maxHeight: coverHeight)
                info
                    .frame(maxWidth: size.width * 0.36, alignment: .leading)
                    .opacity(infoShown ? 1 : 0)
                    .offset(y: infoShown ? 0 : 10)
            }
            .padding(.leading, size.width * 0.1)
            .frame(width: size.width, height: size.height, alignment: .leading)
        }
        .onAppear {
            withAnimation(.easeOut(duration: 0.4).delay(0.15)) { infoShown = true }
        }
    }

    @ViewBuilder
    private var cover: some View {
        if let artwork {
            Image(uiImage: artwork.cover)
                .resizable()
                .scaledToFit()
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                .shadow(color: .black.opacity(0.5), radius: 20, y: 8)
                .accessibilityHidden(true)
        } else {
            LaunchArtwork(game: game)
                .aspectRatio(16 / 9, contentMode: .fit)
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        }
    }

    private var info: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(game.engine.displayName)
                .font(.caption2.weight(.bold))
                .tracking(1)
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .background(.white.opacity(0.18), in: RoundedRectangle(cornerRadius: 6, style: .continuous))
            Text(model.displayTitle(game))
                .font(.title.bold())
                .lineLimit(2)
                .minimumScaleFactor(0.7)
            if game.playTime >= 60 {
                Text("\(model.recency(game)) · Played \(game.playTime.playTimeSummary)")
                    .font(.footnote)
                    .foregroundStyle(.white.opacity(0.8))
                    .lineLimit(1)
            }
            LoadingBar()
                .frame(height: 3)
                .padding(.top, 10)
            Text(ending ? LocalizedStringKey("Ending game…") : LocalizedStringKey("Loading…"))
                .font(.caption)
                .foregroundStyle(.white.opacity(0.75))
        }
        .foregroundStyle(.white)
        .accessibilityElement(children: .combine)
    }

    private func load() async {
        guard let url = model.coverURL(game) else { return }
        // The same size as the Continue Playing hero, so the last played
        // game usually comes straight from the cache.
        let size = CGSize(width: 600, height: 600)
        if let hit = CoverCache.shared.cachedHeroArtwork(url, size: size, scale: displayScale) {
            artwork = hit
            return
        }
        let processed = await CoverCache.shared.heroArtwork(for: url, size: size, scale: displayScale)
        guard !Task.isCancelled else { return }
        withAnimation(.easeOut(duration: 0.25)) { artwork = processed }
    }
}

/// An indeterminate progress bar: engines don't report how far they've
/// loaded, so a short segment sweeps across instead.
private struct LoadingBar: View {
    @State private var phase: CGFloat = 0

    var body: some View {
        GeometryReader { proxy in
            let width = proxy.size.width
            Capsule()
                .fill(.white.opacity(0.22))
                .overlay(alignment: .leading) {
                    Capsule()
                        .fill(.white)
                        .frame(width: width * 0.3)
                        .offset(x: -width * 0.3 + phase * width * 1.3)
                }
                .clipShape(Capsule())
        }
        .onAppear {
            withAnimation(.easeInOut(duration: 1.1).repeatForever(autoreverses: false)) { phase = 1 }
        }
        .accessibilityHidden(true)
    }
}

// MARK: - Model

extension AppModel {
    /// Presents the player. With a cover to grow from (and Reduce Motion
    /// off), the cover first expands over the library and the player then
    /// appears under it without a slide; otherwise it slides up as before.
    func presentPlayer(_ game: GameRecord, from source: CGRect?, sourceRadius: CGFloat) {
        guard let source, !UIAccessibility.isReduceMotionEnabled else {
            player = game
            return
        }
        launchTransition = LaunchTransition(game: game, source: source, direction: .entering, sourceRadius: sourceRadius)
    }

    /// The entering cover now fills the screen: put the player, which starts
    /// with the same picture, over it.
    func coverDidFillScreen(_ transition: LaunchTransition) {
        guard launchTransition == transition else { return }
        var instant = Transaction()
        instant.disablesAnimations = true
        withTransaction(instant) { player = transition.game }
        // Leave the overlay under the player a moment so no frame of the
        // library shows while the player appears.
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 400_000_000)
            if launchTransition == transition { launchTransition = nil }
        }
    }

    func transitionDidEnd(_ transition: LaunchTransition) {
        if launchTransition == transition { launchTransition = nil }
    }

    /// Takes the player down. A game that was entered with the cover
    /// animation leaves the same way: its cover, which the player shows by
    /// now, dissolves over the library instead of sliding down.
    func closePlayer(_ game: GameRecord) {
        guard player != nil else { return }
        guard !UIAccessibility.isReduceMotionEnabled else {
            player = nil
            return
        }
        launchTransition = LaunchTransition(game: game, source: nil, direction: .leaving, sourceRadius: 0)
        var instant = Transaction()
        instant.disablesAnimations = true
        withTransaction(instant) { player = nil }
    }
}
