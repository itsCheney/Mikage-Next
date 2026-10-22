import SwiftUI
import VNCore

/// "Which game is this?": candidates from Bangumi and VNDB for a game
/// folder, searched from its name, with a field to search by title.
struct MatchView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let game: GameRecord

    @State private var query = ""
    @State private var candidates: [MatchCandidate] = []
    @State private var selection: String?
    @State private var searching = false
    @State private var linking = false
    @State private var error: String?
    @State private var searchedOnce = false
    @State private var choosingCover = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    source
                    results
                    searchField
                    Text("Linking fetches the cover, synopsis and tags. Game files are never changed, and you can change the link later in the game’s details.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 4)
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 24)
            }
            .background(AppBackground())
            .scrollDismissesKeyboard(.interactively)
            .navigationTitle("Which Game Is This?")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
                }
            }
            .safeAreaInset(edge: .bottom) { actions }
            // Picking the cover is the next step once linking is done.
            .navigationDestination(isPresented: $choosingCover) {
                ArtworkChoiceView(game: game, afterLinking: true) { dismiss() }
            }
        }
        .interactiveDismissDisabled(choosingCover)
        .task {
            guard !searchedOnce else { return }
            searchedOnce = true
            query = MatchFinder.searchText(fromFolderName: game.folderName)
            await search()
        }
        .interactiveDismissDisabled(linking)
    }

    private var source: some View {
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
        }
        .padding(.horizontal, 4)
        .padding(.top, 8)
    }

    @ViewBuilder
    private var results: some View {
        if searching && candidates.isEmpty {
            HStack {
                Spacer()
                ProgressView("Searching Bangumi and VNDB…")
                Spacer()
            }
            .padding(.vertical, 40)
        } else if let error, candidates.isEmpty {
            Label(error, systemImage: "wifi.exclamationmark")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 30)
        } else if candidates.isEmpty {
            Text("No matches. Try another title below.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 30)
        } else {
            VStack(spacing: 0) {
                ForEach(Array(candidates.enumerated()), id: \.element.id) { index, candidate in
                    if index > 0 { Divider().padding(.leading, 84) }
                    row(candidate, best: index == 0 && MatchFinder.isConfident(candidate))
                }
            }
            .background(
                Color(uiColor: .secondarySystemGroupedBackground),
                in: RoundedRectangle(cornerRadius: 24, style: .continuous)
            )
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Candidates")
        }
    }

    private func row(_ candidate: MatchCandidate, best: Bool) -> some View {
        let selected = selection == candidate.id
        return Button {
            selection = candidate.id
        } label: {
            HStack(spacing: 14) {
                RemoteThumbnail(
                    url: candidate.thumbnailURL,
                    blurred: candidate.thumbnailIsSensitive && model.settings.hideSensitiveImages
                )
                .frame(width: 54, height: 76)
                .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text(candidate.title)
                        .font(.body.weight(.semibold))
                        .foregroundStyle(.primary)
                        .lineLimit(2)
                    let subtitle = [candidate.originalTitle, candidate.year].compactMap { $0 }.joined(separator: " · ")
                    if !subtitle.isEmpty {
                        Text(subtitle)
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    HStack(spacing: 6) {
                        if best {
                            Text("Best Match")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(.tint)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 2)
                                .background(Color.accentColor.opacity(0.14), in: Capsule())
                        }
                        Text(sources(candidate))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.top, 4)
                }
                Spacer(minLength: 0)
                Image(systemName: selected ? "checkmark.circle.fill" : "circle")
                    .font(.title2)
                    .foregroundStyle(selected ? Color.accentColor : Color(uiColor: .tertiaryLabel))
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? [.isSelected] : [])
    }

    private func sources(_ candidate: MatchCandidate) -> String {
        switch (candidate.vndb != nil, candidate.bangumi != nil) {
        case (true, true): return "VNDB · Bangumi"
        case (true, false): return String(localized: "VNDB only")
        default: return String(localized: "Bangumi only")
        }
    }

    private var searchField: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .foregroundStyle(.secondary)
            TextField("None of these? Search by title", text: $query)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .submitLabel(.search)
                .onSubmit { Task { await search() } }
            if searching && !candidates.isEmpty {
                ProgressView()
            }
        }
        .padding(.horizontal, 14)
        .frame(minHeight: 44)
        .background(Color(uiColor: .tertiarySystemFill), in: Capsule())
    }

    private var actions: some View {
        VStack(spacing: 4) {
            Button {
                Task { await link() }
            } label: {
                Group {
                    if linking {
                        ProgressView()
                    } else {
                        Text("Link")
                    }
                }
                .font(.headline)
                .frame(maxWidth: .infinity)
                .frame(minHeight: 30)
            }
            .nativeGlassButtonStyle(prominent: true)
            .controlSize(.large)
            .disabled(selectedCandidate == nil || linking)

            Button("Skip for Now") { dismiss() }
                .font(.body.weight(.medium))
                .frame(minHeight: 44)
                .disabled(linking)
        }
        .padding(.horizontal, 16)
        .padding(.top, 8)
        .background(.bar)
    }

    private var selectedCandidate: MatchCandidate? {
        candidates.first { $0.id == selection }
    }

    private func search() async {
        let text = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        searching = true
        error = nil
        defer { searching = false }
        do {
            let found = try await model.searchCandidates(text)
            candidates = found
            // Keep the current link selected, else the confident best match.
            let link = game.link
            if let current = found.first(where: {
                link != nil && $0.vndb?.id == link?.vndbID && $0.bangumi?.id == link?.bangumiID
            }) {
                selection = current.id
            } else if let first = found.first, MatchFinder.isConfident(first) {
                selection = first.id
            } else {
                selection = nil
            }
        } catch {
            candidates = []
            self.error = error.localizedDescription
        }
    }

    private func link() async {
        guard let candidate = selectedCandidate else { return }
        linking = true
        defer { linking = false }
        do {
            try await model.link(game, to: candidate)
            let art = model.record(game.id)?.link?.metadata
                .cardArt(hidingSensitive: model.settings.hideSensitiveImages) ?? []
            if art.isEmpty {
                dismiss()
            } else {
                choosingCover = true
            }
        } catch {
            model.alert = error.localizedDescription
        }
    }
}
