import SwiftUI
import VNCore

/// The notice over the library after a rescan found game folders the
/// library didn't have: Import matches them with Bangumi and VNDB.
struct NewGamesToast: Identifiable, Equatable {
    enum Phase: Equatable {
        /// Found, waiting for Import.
        case found
        /// Matching with Bangumi and VNDB.
        case importing
        /// Done; `matched` of the games were matched.
        case imported(matched: Int)
    }

    let id = UUID()
    let gameIDs: [UUID]
    let names: [String]
    var phase: Phase = .found
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

    /// Matches each new game whose best search result is a near-certain
    /// hit, which fetches its covers and details. The rest stay unmatched
    /// for the match sheet.
    func importNewGames() async {
        guard var current = newGamesToast, current.phase == .found else { return }
        current.phase = .importing
        newGamesToast = current
        var matched = 0
        for id in current.gameIDs {
            if await matchAutomatically(id) { matched += 1 }
        }
        AppDiagnostics.shared.event("library", "newGames.imported", ["count": String(current.gameIDs.count), "matched": String(matched)])
        guard newGamesToast?.id == current.id else { return }
        current.phase = .imported(matched: matched)
        newGamesToast = current
    }

    /// Opens the match sheet for the first new game still unmatched.
    func viewNewGames() {
        guard let current = newGamesToast else { return }
        newGamesToast = nil
        if let game = current.gameIDs.lazy.compactMap({ self.record($0) }).first(where: { $0.link == nil }) {
            matchRequest = game
        }
    }

    private func matchAutomatically(_ id: UUID) async -> Bool {
        guard let game = record(id), game.link == nil else { return false }
        let query = MatchFinder.searchText(fromFolderName: game.folderName)
        guard !query.isEmpty,
              let best = try? await searchCandidates(query).first,
              MatchFinder.isConfident(best), best.score >= 0.85 else { return false }
        do {
            try await link(game, to: best)
            return true
        } catch {
            AppDiagnostics.shared.event("library", "newGames.matchFailed", ["error": error.localizedDescription])
            return false
        }
    }
}

struct NewGamesToastView: View {
    @EnvironmentObject private var model: AppModel
    let toast: NewGamesToast

    private var found: Bool { toast.phase == .found }

    private var title: String {
        found
            ? String(localized: "Found \(toast.gameIDs.count) new games")
            : String(localized: "Imported \(toast.gameIDs.count) games")
    }

    private var detail: String {
        switch toast.phase {
        case .found:
            let names = toast.names.formatted(.list(type: .and, width: .narrow))
            return String(localized: "\(names) · from the Files app")
        case .importing:
            return String(localized: "Fetching covers and details from Bangumi and VNDB…")
        case .imported(let matched):
            return matched == toast.gameIDs.count
                ? String(localized: "Matched with Bangumi and VNDB")
                : String(localized: "Matched \(matched) of \(toast.gameIDs.count) · View to match the rest")
        }
    }

    var body: some View {
        HStack(spacing: 12) {
            thumbnails
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.subheadline.weight(.semibold))
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            Button {
                if found {
                    Task { await model.importNewGames() }
                } else {
                    model.viewNewGames()
                }
            } label: {
                Text(found ? "Import" : "View")
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(found ? Color.white : Color.accentColor)
                    .padding(.horizontal, 16)
                    .frame(height: 40)
                    .background(found ? Color.accentColor : Color.accentColor.opacity(0.16), in: Capsule())
            }
            .buttonStyle(.plain)
            Button {
                withAnimation(.easeInOut(duration: 0.25)) { model.newGamesToast = nil }
            } label: {
                Image(systemName: "xmark")
                    .font(.footnote.weight(.bold))
                    .foregroundStyle(.secondary)
                    .frame(width: 40, height: 44)
            }
            .buttonStyle(.plain)
            .accessibilityLabel(Text("Dismiss"))
        }
        .padding(.leading, 12)
        .padding(.trailing, 6)
        .padding(.vertical, 10)
        .frame(minHeight: 64)
        .nativeGlassPanel(24)
        .accessibilityElement(children: .contain)
        .task(id: toast.phase) {
            // Found waits for the player; the result goes away by itself.
            guard case .imported = toast.phase else { return }
            try? await Task.sleep(nanoseconds: 6_000_000_000)
            if model.newGamesToast?.id == toast.id {
                withAnimation(.easeInOut(duration: 0.25)) { model.newGamesToast = nil }
            }
        }
    }

    /// Two of the new games' covers, fanned, with a check once imported.
    private var thumbnails: some View {
        let games = toast.gameIDs.prefix(2).compactMap { model.record($0) }
        return ZStack(alignment: .topLeading) {
            ForEach(Array(games.enumerated()), id: \.element.id) { index, game in
                GameCover(game: game)
                    .frame(width: 36, height: 36)
                    .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(.white, lineWidth: 1.5))
                    .rotationEffect(.degrees(index == 0 ? -7 : 5))
                    .offset(x: CGFloat(index) * 18, y: index == 0 ? 6 : 0)
            }
            if !found {
                Image(systemName: "checkmark")
                    .font(.system(size: 10, weight: .heavy))
                    .foregroundStyle(.white)
                    .frame(width: 20, height: 20)
                    .background(Circle().fill(Color(red: 0x24 / 255, green: 0x8A / 255, blue: 0x3D / 255)))
                    .overlay(Circle().strokeBorder(.white, lineWidth: 2))
                    .offset(x: 36, y: 24)
                    .opacity(toast.phase == .importing ? 0 : 1)
            }
        }
        .frame(width: 54, height: 42, alignment: .topLeading)
        .accessibilityHidden(true)
    }
}
