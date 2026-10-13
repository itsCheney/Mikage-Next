import SwiftUI
import VNCore

/// Rates a game, and in `finish` mode also marks it finished. Opened from
/// the status menu, the long-press menu, the rating button on the details
/// page, or the in-game menu.
struct RatingSheet: View {
    enum Mode: String, Identifiable {
        case finish
        case rate
        var id: String { rawValue }
    }

    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let game: GameRecord
    let mode: Mode

    @State private var rating: Int?
    @State private var comment = ""
    @State private var loaded = false

    private var current: GameRecord { model.record(game.id) ?? game }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                header
                ratingPicker
                commentField
                syncNote
            }
            .padding(.horizontal, 18)
            .padding(.top, 20)
        }
        .scrollDismissesKeyboard(.interactively)
        .safeAreaInset(edge: .bottom) { actions }
        .presentationDetents([.height(600), .large])
        .presentationDragIndicator(.visible)
        .onAppear {
            guard !loaded else { return }
            loaded = true
            rating = current.listEntry.rating
            comment = current.listEntry.comment ?? ""
        }
    }

    private var header: some View {
        HStack(spacing: 14) {
            ArtworkImage(url: model.posterURL(current))
                .frame(width: 48, height: 68)
                .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                .shadow(color: .black.opacity(0.2), radius: 6, y: 3)
            VStack(alignment: .leading, spacing: 3) {
                Text(mode == .finish ? LocalizedStringKey("Finished!") : LocalizedStringKey("Rate This Game"))
                    .font(.title2.bold())
                Text(subtitle)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
            Spacer(minLength: 0)
            RoundButton(symbol: "xmark", label: "Close") { dismiss() }
        }
    }

    private var subtitle: String {
        guard current.playTime >= 60 else { return model.displayTitle(current) }
        return "\(model.displayTitle(current)) · " + String(localized: "\(current.playTime.playTimeSummary) in total")
    }

    private var ratingPicker: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text("Rating")
                    .font(.subheadline.weight(.semibold))
                Spacer()
                if let rating {
                    Text(verbatim: "\(rating) · \(RatingLabel.text(for: rating))")
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.tint)
                }
            }
            HStack(spacing: 4) {
                ForEach(1...10, id: \.self) { value in
                    Button {
                        rating = rating == value ? nil : value
                    } label: {
                        Text(verbatim: "\(value)")
                            .font(.subheadline.weight(.semibold))
                            .monospacedDigit()
                            .frame(maxWidth: .infinity, minHeight: 44)
                            .foregroundStyle(value == rating ? Color.white : Color.primary)
                            .background(fill(for: value), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(Text(verbatim: "\(value), \(RatingLabel.text(for: value))"))
                    .accessibilityAddTraits(value == rating ? [.isSelected] : [])
                }
            }
        }
        .padding(.top, 22)
    }

    private func fill(for value: Int) -> Color {
        guard let rating else { return Color(uiColor: .secondarySystemGroupedBackground) }
        if value == rating { return .accentColor }
        return value < rating ? Color.accentColor.opacity(0.16) : Color(uiColor: .secondarySystemGroupedBackground)
    }

    private var commentField: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Comment")
                .font(.subheadline.weight(.semibold))
            TextField("A line about it (optional)", text: $comment, axis: .vertical)
                .lineLimit(2...4)
                .padding(.horizontal, 12)
                .padding(.vertical, 10)
                .background(
                    Color(uiColor: .secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 16, style: .continuous)
                )
        }
        .padding(.top, 20)
    }

    @ViewBuilder
    private var syncNote: some View {
        let services = (current.link?.services ?? []).filter { model.accounts[$0] != nil }
        if !services.isEmpty {
            Label {
                Text(services.count > 1
                     ? String(localized: "Syncs to Bangumi and VNDB")
                     : String(localized: "Syncs to \(services[0].displayName)"))
            } icon: {
                Image(systemName: "arrow.triangle.2.circlepath")
            }
            .font(.footnote)
            .foregroundStyle(.secondary)
            .padding(.top, 14)
        }
    }

    private var actions: some View {
        VStack(spacing: 4) {
            Button {
                model.saveRating(rating, comment: comment, finished: mode == .finish, for: current)
                dismiss()
            } label: {
                Text("Save")
                    .font(.headline)
                    .frame(maxWidth: .infinity, minHeight: 30)
            }
            .nativeGlassButtonStyle(prominent: true)
            .controlSize(.large)

            if mode == .finish {
                Button("Mark Finished Without Rating") {
                    model.setStatus(.finished, for: current)
                    dismiss()
                }
                .frame(minHeight: 44)
            } else if current.listEntry.rating != nil {
                Button("Remove Rating", role: .destructive) {
                    model.saveRating(nil, comment: comment, finished: false, for: current)
                    dismiss()
                }
                .frame(minHeight: 44)
            }
        }
        .padding(.horizontal, 18)
        .padding(.top, 8)
        .padding(.bottom, 8)
        .background(.bar)
    }
}
