import SwiftUI
import VNCore

/// The notice over the library after a rescan found game folders the
/// library didn't have. Import opens the review sheet, where each game is
/// matched with Bangumi and VNDB or kept with a generated cover; the toast
/// then shows the progress of adding them.
struct NewGamesToast: Identifiable, Equatable {
    enum Phase: Equatable {
        /// Found, waiting for Import.
        case found
        /// Linking the reviewed choices; `done` games are finished.
        case importing(done: Int)
        /// All added; `matched` were linked, the rest use generated covers.
        case imported(matched: Int)
    }

    /// What became of one game once its review choice was applied.
    enum Outcome: Equatable {
        case matched
        case generated
        case failed
    }

    let id = UUID()
    let gameIDs: [UUID]
    let names: [String]
    var phase: Phase = .found
    var outcomes: [UUID: Outcome] = [:]
}

/// The games the review sheet walks through, one at a time.
struct ImportReview: Identifiable, Equatable {
    let id = UUID()
    let gameIDs: [UUID]
}

extension AppModel {
    /// Offers the games a scan added, unless they came from Mikage's own
    /// import (which opens the match sheet) or ship with the app.
    func announceNewGames(_ added: [GameRecord]) {
        let games = added.filter { $0.link == nil }
        guard !games.isEmpty else { return }
        AppDiagnostics.shared.event("library", "newGames.found", ["count": String(games.count)])
        newGamesToast = NewGamesToast(gameIDs: games.map(\.id), names: games.map(\.title))
    }

    /// Opens the review sheet for the games the toast found.
    func reviewNewGames() {
        guard let current = newGamesToast, current.phase == .found else { return }
        let present = current.gameIDs.filter { record($0) != nil }
        guard !present.isEmpty else {
            newGamesToast = nil
            return
        }
        importReview = ImportReview(gameIDs: present)
    }

    /// Applies the review: links each game to its chosen candidate, in
    /// order, and leaves the rest with generated covers. The toast follows
    /// along and closes itself a few seconds after the last one.
    func applyImportReview(_ choices: [(UUID, MatchCandidate?)]) async {
        guard var current = newGamesToast else { return }
        let ids = choices.map(\.0)
        current = NewGamesToast(gameIDs: ids, names: ids.map { record($0)?.title ?? "" }, phase: .importing(done: 0))
        newGamesToast = current
        var matched = 0
        for (index, (id, candidate)) in choices.enumerated() {
            var outcome = NewGamesToast.Outcome.generated
            if let candidate, let game = record(id) {
                do {
                    try await link(game, to: candidate)
                    outcome = .matched
                    matched += 1
                } catch {
                    outcome = .failed
                    AppDiagnostics.shared.event("library", "newGames.matchFailed", ["error": error.localizedDescription])
                }
            }
            guard newGamesToast?.id == current.id else { return }
            current.outcomes[id] = outcome
            current.phase = .importing(done: index + 1)
            newGamesToast = current
        }
        AppDiagnostics.shared.event("library", "newGames.imported", ["count": String(ids.count), "matched": String(matched)])
        current.phase = .imported(matched: matched)
        newGamesToast = current
    }
}

struct NewGamesToastView: View {
    @EnvironmentObject private var model: AppModel
    let toast: NewGamesToast
    @State private var expanded = false

    private var games: [GameRecord] { toast.gameIDs.compactMap { model.record($0) } }

    private var title: String {
        let count = toast.gameIDs.count
        switch toast.phase {
        case .found:
            return String(localized: "Found \(count) new games")
        case .importing(let done):
            return String(localized: "Adding \(min(done + 1, count)) of \(count)…")
        case .imported:
            return String(localized: "Added \(count) games")
        }
    }

    private var detail: String {
        switch toast.phase {
        case .found:
            return expanded
                ? String(localized: "From the Files app")
                : toast.names.formatted(.list(type: .and, width: .narrow))
        case .importing:
            return String(localized: "Fetching covers from Bangumi and VNDB")
        case .imported(let matched):
            let generated = toast.gameIDs.count - matched
            return generated == 0
                ? String(localized: "Matched with Bangumi and VNDB")
                : String(localized: "\(matched) matched · \(generated) with generated covers")
        }
    }

    private var found: Bool { toast.phase == .found }

    private var progress: Double? {
        guard case .importing(let done) = toast.phase, !toast.gameIDs.isEmpty else { return nil }
        return Double(done) / Double(toast.gameIDs.count)
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Button {
                    withAnimation(.spring(response: 0.35, dampingFraction: 0.85)) { expanded.toggle() }
                } label: {
                    HStack(spacing: 12) {
                        thumbnails
                        VStack(alignment: .leading, spacing: 2) {
                            Text(title)
                                .font(.subheadline.weight(.semibold))
                                .foregroundStyle(.primary)
                            Text(detail)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                        }
                        Spacer(minLength: 0)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityHint(expanded ? Text("Hides the games") : Text("Shows the games"))

                if found {
                    Button {
                        model.reviewNewGames()
                    } label: {
                        Text("Import")
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(Color.white)
                            .padding(.horizontal, 16)
                            .frame(height: 40)
                            .background(Color.accentColor, in: Capsule())
                            .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                }
                Button {
                    withAnimation(.easeInOut(duration: 0.25)) { model.newGamesToast = nil }
                } label: {
                    Image(systemName: "xmark")
                        .font(.footnote.weight(.bold))
                        .foregroundStyle(.secondary)
                        .frame(width: 44, height: 44)
                        // A plain button only takes touches on what it draws;
                        // without this, taps around the glyph fell through
                        // to the library underneath.
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(Text("Dismiss"))
            }
            .padding(.leading, 12)
            .padding(.trailing, 4)
            .padding(.vertical, 10)

            if let progress {
                ProgressView(value: progress)
                    .tint(Color.accentColor)
                    .padding(.horizontal, 16)
                    .padding(.bottom, 12)
                    .animation(.easeInOut(duration: 0.4), value: progress)
            }

            if expanded {
                Divider()
                VStack(spacing: 0) {
                    ForEach(games) { game in
                        gameRow(game)
                    }
                }
                .padding(.vertical, 6)
                .transition(.opacity.combined(with: .move(edge: .bottom)))
            }
        }
        .frame(minHeight: 64)
        .nativeGlassPanel(26)
        .accessibilityElement(children: .contain)
        .task(id: "\(toast.phase)|\(expanded)") {
            // Found waits for the player; the result goes away by itself
            // unless the list is open.
            guard case .imported = toast.phase, !expanded else { return }
            try? await Task.sleep(nanoseconds: 6_000_000_000)
            if model.newGamesToast?.id == toast.id {
                withAnimation(.easeInOut(duration: 0.25)) { model.newGamesToast = nil }
            }
        }
    }

    private func gameRow(_ game: GameRecord) -> some View {
        HStack(spacing: 12) {
            GameCover(game: game)
                .frame(width: 36, height: 36)
                .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
            VStack(alignment: .leading, spacing: 1) {
                Text(model.displayTitle(game))
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
                Text(verbatim: "\(game.engine.displayName) · \(AppModel.librarySize(game.byteCount))")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer(minLength: 0)
            state(for: game)
                .font(.footnote.weight(.semibold))
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 6)
        .accessibilityElement(children: .combine)
    }

    @ViewBuilder
    private func state(for game: GameRecord) -> some View {
        let index = toast.gameIDs.firstIndex(of: game.id) ?? 0
        switch (toast.phase, toast.outcomes[game.id]) {
        case (.found, _):
            Text("New").foregroundStyle(.tint)
        case (_, .matched?):
            Text("Matched").foregroundStyle(Self.green)
        case (_, .generated?):
            Text("Cover Made").foregroundStyle(Self.green)
        case (_, .failed?):
            Text("Not Matched").foregroundStyle(.orange)
        case (.importing(let done), nil) where index == done:
            Text("Matching…").foregroundStyle(.secondary)
        default:
            Text("Waiting").foregroundStyle(.secondary)
        }
    }

    private static let green = Color(red: 0x24 / 255, green: 0x8A / 255, blue: 0x3D / 255)

    /// Three of the new games' covers, fanned, with a check once imported.
    private var thumbnails: some View {
        let shown = Array(games.prefix(3))
        let angles: [Double] = [-8, -1, 7]
        let offsets: [CGSize] = [CGSize(width: 0, height: 8), CGSize(width: 15, height: 3), CGSize(width: 30, height: 0)]
        return ZStack(alignment: .topLeading) {
            ForEach(Array(shown.enumerated()), id: \.element.id) { index, game in
                GameCover(game: game)
                    .frame(width: 34, height: 34)
                    .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(.white, lineWidth: 1.5))
                    .shadow(color: .black.opacity(0.18), radius: 3, y: 1)
                    .rotationEffect(.degrees(angles[index]))
                    .offset(offsets[index])
            }
            if case .imported = toast.phase {
                Image(systemName: "checkmark")
                    .font(.system(size: 10, weight: .heavy))
                    .foregroundStyle(.white)
                    .frame(width: 22, height: 22)
                    .background(Circle().fill(Self.green))
                    .overlay(Circle().strokeBorder(.white, lineWidth: 2))
                    .offset(x: 44, y: 24)
            }
        }
        .frame(width: 66, height: 46, alignment: .topLeading)
        .accessibilityHidden(true)
    }
}
