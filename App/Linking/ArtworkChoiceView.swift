import SwiftUI
import VNCore

/// Picks the card art for a linked game from the screenshots and covers
/// Bangumi and VNDB supplied, widest 16:9 images first, then 4:3. Also the
/// step right after linking, pushed inside the match sheet.
struct ArtworkChoiceView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let game: GameRecord
    /// Shown as the last step of linking, which `onFinish` closes.
    var afterLinking = false
    var onFinish: (() -> Void)?

    @State private var saving: String?

    private var current: GameRecord { model.record(game.id) ?? game }

    private var candidates: [ArtworkCandidate] {
        current.link?.metadata.cardArt(hidingSensitive: model.settings.hideSensitiveImages) ?? []
    }

    private let columns = [GridItem(.adaptive(minimum: 150), spacing: 12)]

    var body: some View {
        if afterLinking {
            content
        } else {
            NavigationStack { content }
                .interactiveDismissDisabled(saving != nil)
        }
    }

    private func finish() {
        if let onFinish { onFinish() } else { dismiss() }
    }

    private var content: some View {
        ScrollView {
            if afterLinking {
                Text("Linked. Pick the cover for this game, or keep Mikage’s choice.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 16)
                    .padding(.top, 8)
            }
            if candidates.isEmpty {
                Text("Bangumi and VNDB have no images for this game.")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.top, 60)
            } else {
                LazyVGrid(columns: columns, spacing: 12) {
                    ForEach(candidates, id: \.url) { candidate in
                        tile(candidate)
                    }
                }
                .padding(16)
            }
        }
        .background(AppBackground())
        .navigationTitle("Choose Cover")
        .navigationBarTitleDisplayMode(.inline)
        .navigationBarBackButtonHidden(afterLinking)
        .toolbar {
            ToolbarItem(placement: afterLinking ? .confirmationAction : .cancellationAction) {
                Button(afterLinking ? LocalizedStringKey("Done") : LocalizedStringKey("Cancel")) { finish() }
                    .disabled(saving != nil)
            }
        }
    }

    private func tile(_ candidate: ArtworkCandidate) -> some View {
        let selected = current.link?.bannerChoice == candidate.url
        return Button {
            guard saving == nil else { return }
            saving = candidate.url
            Task {
                await model.chooseCardArt(candidate, for: current)
                saving = nil
                finish()
            }
        } label: {
            VStack(alignment: .leading, spacing: 6) {
                RemoteThumbnail(url: candidate.url, blurred: false)
                    .aspectRatio(16 / 9, contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                    .overlay {
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .strokeBorder(selected ? Color.accentColor : .clear, lineWidth: 3)
                    }
                    .overlay {
                        if saving == candidate.url {
                            ProgressView()
                        }
                    }
                label(for: candidate)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private func label(for candidate: ArtworkCandidate) -> Text {
        switch LinkedMetadata.cardRank(candidate) {
        case 0: return Text(verbatim: "16:9")
        case 1: return Text(verbatim: "4:3")
        case 2: return Text("Wide")
        case 3: return Text("Image")
        default: return Text("Cover")
        }
    }
}
