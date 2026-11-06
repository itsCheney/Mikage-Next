import SwiftUI
import VNCore

/// "Review New Games": the games a scan found, one at a time. Each is
/// matched with a Bangumi/VNDB candidate or kept with a cover generated
/// from its folder name; a summary lists the choices before they're added.
struct ImportReviewView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let review: ImportReview

    private enum Choice: Equatable {
        case candidate(String)
        case generated
    }

    private enum Search: Equatable {
        case loading
        case done([MatchCandidate])
        case failed(String)
    }

    @State private var index = 0
    @State private var showingSummary = false
    @State private var choices: [UUID: Choice] = [:]
    @State private var searches: [UUID: Search] = [:]
    @State private var queries: [UUID: String] = [:]

    private var ids: [UUID] { review.gameIDs }
    private var game: GameRecord? { model.record(ids[index]) }

    var body: some View {
        NavigationStack {
            Group {
                if showingSummary {
                    summary
                } else if let game {
                    page(game)
                        .id(game.id)
                }
            }
            .background(AppBackground())
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    if showingSummary {
                        Button("Back") { showingSummary = false }
                    } else {
                        Button("Cancel") { dismiss() }
                    }
                }
                ToolbarItem(placement: .principal) {
                    Text(showingSummary ? LocalizedStringKey("Ready to Add") : LocalizedStringKey("Review New Games"))
                        .font(.headline)
                }
                if !showingSummary {
                    ToolbarItem(placement: .primaryAction) {
                        Text("\(index + 1) of \(ids.count)")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                }
            }
        }
        .task { await searchAll() }
    }

    // MARK: One game

    private func page(_ game: GameRecord) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                progress
                source(game)
                VStack(alignment: .leading, spacing: 8) {
                    sectionHeader("Bangumi & VNDB")
                    results(game)
                    searchField(game)
                }
                VStack(alignment: .leading, spacing: 8) {
                    sectionHeader("Or Keep It Unmatched")
                    generatedRow(game)
                }
            }
            .padding(.horizontal, 16)
            .padding(.bottom, 24)
        }
        .scrollDismissesKeyboard(.interactively)
        .safeAreaInset(edge: .bottom) { pageActions }
    }

    private var progress: some View {
        HStack(spacing: 4) {
            ForEach(ids.indices, id: \.self) { i in
                Capsule()
                    .fill(i <= index ? Color.accentColor : Color(uiColor: .tertiarySystemFill))
                    .frame(height: 4)
            }
        }
        .padding(.top, 6)
        .accessibilityElement()
        .accessibilityLabel(Text("Game \(index + 1) of \(ids.count)"))
    }

    private func source(_ game: GameRecord) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Recognized from the folder name")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Text(verbatim: game.folderName)
                .font(.system(.footnote, design: .monospaced))
                .padding(.horizontal, 9)
                .padding(.vertical, 5)
                .background(Color(uiColor: .tertiarySystemFill), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                .lineLimit(2)
            Text(verbatim: "\(game.engine.displayName) · \(AppModel.librarySize(game.byteCount))")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .padding(.horizontal, 4)
    }

    private func sectionHeader(_ title: LocalizedStringKey) -> some View {
        Text(title)
            .font(.footnote.weight(.semibold))
            .foregroundStyle(.secondary)
            .textCase(.uppercase)
            .padding(.horizontal, 4)
    }

    @ViewBuilder
    private func results(_ game: GameRecord) -> some View {
        switch searches[game.id] ?? .loading {
        case .loading:
            ProgressView("Searching Bangumi and VNDB…")
                .frame(maxWidth: .infinity)
                .padding(.vertical, 34)
                .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
        case .failed(let message):
            Label(message, systemImage: "wifi.exclamationmark")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 26)
                .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
        case .done(let candidates) where candidates.isEmpty:
            Text("No matches. Try another title below, or keep a generated cover.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 26)
                .padding(.horizontal, 18)
                .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
        case .done(let candidates):
            VStack(spacing: 0) {
                ForEach(Array(candidates.enumerated()), id: \.element.id) { i, candidate in
                    if i > 0 { Divider().padding(.leading, 84) }
                    MatchCandidateRow(
                        candidate: candidate,
                        best: i == 0 && MatchFinder.isConfident(candidate),
                        selected: choices[game.id] == .candidate(candidate.id)
                    ) {
                        choices[game.id] = .candidate(candidate.id)
                    }
                }
            }
            .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Candidates")
        }
    }

    private func searchField(_ game: GameRecord) -> some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .foregroundStyle(.secondary)
            TextField("Search by title", text: Binding(
                get: { queries[game.id] ?? MatchFinder.searchText(fromFolderName: game.folderName) },
                set: { queries[game.id] = $0 }
            ))
            .textInputAutocapitalization(.never)
            .autocorrectionDisabled()
            .submitLabel(.search)
            .onSubmit { Task { await search(game.id, choosing: false) } }
        }
        .padding(.horizontal, 14)
        .frame(minHeight: 44)
        .background(Color(uiColor: .tertiarySystemFill), in: Capsule())
    }

    private func generatedRow(_ game: GameRecord) -> some View {
        let selected = choices[game.id] == .generated
        return Button {
            choices[game.id] = .generated
        } label: {
            HStack(spacing: 14) {
                GeneratedCover(game: game)
                    .frame(width: 112, height: 63)
                    .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text("Use a Generated Cover")
                        .font(.body.weight(.semibold))
                    Text("Made from the folder name. Match it later from the card’s menu.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
                Image(systemName: selected ? "checkmark.circle.fill" : "circle")
                    .font(.title2)
                    .foregroundStyle(selected ? Color.accentColor : Color(uiColor: .tertiaryLabel))
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
            .contentShape(RoundedRectangle(cornerRadius: 24, style: .continuous))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? [.isSelected] : [])
    }

    private var pageActions: some View {
        VStack(spacing: 8) {
            if index < ids.count - 1 {
                Button("Use Best Match for the Rest") {
                    for id in ids[(index + 1)...] where choices[id] == nil {
                        choices[id] = .generated
                    }
                    showingSummary = true
                }
                .font(.subheadline.weight(.semibold))
                .frame(minHeight: 36)
            }
            HStack(spacing: 10) {
                if index > 0 {
                    Button("Back") { index -= 1 }
                        .font(.headline)
                        .frame(minHeight: 30)
                        .padding(.horizontal, 8)
                        .nativeGlassButtonStyle()
                        .controlSize(.large)
                }
                Button {
                    if index < ids.count - 1 {
                        index += 1
                    } else {
                        showingSummary = true
                    }
                } label: {
                    Text(index < ids.count - 1 ? LocalizedStringKey("Next Game") : LocalizedStringKey("Review Summary"))
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .frame(minHeight: 30)
                }
                .nativeGlassButtonStyle(prominent: true)
                .controlSize(.large)
            }
        }
        .padding(.horizontal, 16)
        .padding(.top, 8)
        .background(.bar)
    }

    // MARK: Summary

    private var summary: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                sectionHeader("Tap a Game to Change It")
                    .padding(.top, 12)
                VStack(spacing: 0) {
                    ForEach(Array(ids.enumerated()), id: \.element) { i, id in
                        if let game = model.record(id) {
                            if i > 0 { Divider().padding(.leading, 72) }
                            summaryRow(game, at: i)
                        }
                    }
                }
                .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
                Text("Matched games get their cover, synopsis and tags. Game files are never changed.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 4)
            }
            .padding(.horizontal, 16)
            .padding(.bottom, 24)
        }
        .safeAreaInset(edge: .bottom) {
            Button {
                let plan = ids.map { ($0, candidate(for: $0)) }
                dismiss()
                Task { await model.applyImportReview(plan) }
            } label: {
                Text("Add \(ids.count) Games to Library")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .frame(minHeight: 30)
            }
            .nativeGlassButtonStyle(prominent: true)
            .controlSize(.large)
            .padding(.horizontal, 16)
            .padding(.top, 8)
            .background(.bar)
        }
    }

    private func summaryRow(_ game: GameRecord, at i: Int) -> some View {
        let candidate = candidate(for: game.id)
        return Button {
            index = i
            showingSummary = false
        } label: {
            HStack(spacing: 12) {
                Group {
                    if let candidate {
                        RemoteThumbnail(
                            url: candidate.thumbnailURL,
                            blurred: candidate.thumbnailIsSensitive && model.settings.hideSensitiveImages
                        )
                    } else {
                        GeneratedCover(game: game)
                    }
                }
                .frame(width: 44, height: 44)
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                VStack(alignment: .leading, spacing: 1) {
                    Text(candidate?.title ?? GeneratedCover.title(fromFolderName: game.folderName))
                        .font(.body.weight(.semibold))
                        .foregroundStyle(.primary)
                        .lineLimit(1)
                    if let candidate {
                        Text("Matched · \(sources(candidate))")
                            .font(.footnote)
                            .foregroundStyle(Color(red: 0x24 / 255, green: 0x8A / 255, blue: 0x3D / 255))
                    } else {
                        Text("Generated cover")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
                Spacer(minLength: 0)
                Image(systemName: "chevron.right")
                    .font(.footnote.weight(.semibold))
                    .foregroundStyle(.tertiary)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    private func sources(_ candidate: MatchCandidate) -> String {
        [candidate.bangumi != nil ? "Bangumi" : nil, candidate.vndb != nil ? "VNDB" : nil]
            .compactMap { $0 }
            .joined(separator: " + ")
    }

    // MARK: Searching

    private func candidate(for id: UUID) -> MatchCandidate? {
        guard case .candidate(let candidateID) = choices[id],
              case .done(let candidates) = searches[id] else { return nil }
        return candidates.first { $0.id == candidateID }
    }

    /// Searches every game in turn from its folder name, so later pages and
    /// "Use Best Match for the Rest" have results waiting.
    private func searchAll() async {
        for id in ids where searches[id] == nil {
            await search(id, choosing: true)
        }
    }

    /// `choosing` picks the confident best match, or the generated cover,
    /// for a game the player hasn't decided on yet.
    private func search(_ id: UUID, choosing: Bool) async {
        guard let game = model.record(id) else { return }
        let text = (queries[id] ?? MatchFinder.searchText(fromFolderName: game.folderName))
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        searches[id] = .loading
        do {
            let found = try await model.searchCandidates(text)
            searches[id] = .done(found)
            if choosing, choices[id] == nil {
                if let best = found.first, MatchFinder.isConfident(best) {
                    choices[id] = .candidate(best.id)
                } else {
                    choices[id] = .generated
                }
            }
        } catch {
            searches[id] = .failed(error.localizedDescription)
            if choices[id] == nil { choices[id] = .generated }
        }
    }
}
