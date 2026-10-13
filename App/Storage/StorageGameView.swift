import SwiftUI
import VNCore

/// Storage › one game: what it takes, split into game files and saves,
/// with ways to export the saves or remove the game.
struct StorageGameView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openURL) private var openURL
    let gameID: UUID

    @State private var saves: Int64?
    @State private var exporting = false
    @State private var export: URL?
    @State private var showExport = false
    @State private var confirmOffload = false
    @State private var confirmDelete = false
    @State private var working = false

    private var game: GameRecord? { model.games.first { $0.id == gameID } }

    var body: some View {
        Group {
            if let game {
                content(game)
            } else {
                // Removed from here or from the Files app.
                Color.clear
                    .onAppear { dismiss() }
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationBarTitleDisplayMode(.inline)
        .sheet(isPresented: $showExport) {
            if let export { ActivitySheet(items: [export]) }
        }
    }

    private func content(_ game: GameRecord) -> some View {
        let savesBytes = saves ?? 0
        let gameFiles = max(0, game.byteCount - savesBytes)
        return Form {
            Section {
                header(game)
                    .listRowInsets(EdgeInsets(top: 0, leading: 4, bottom: 0, trailing: 4))
                    .listRowBackground(Color.clear)
            }

            Section {
                VStack(alignment: .leading, spacing: 10) {
                    HStack(alignment: .firstTextBaseline) {
                        Text("Total")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text(verbatim: AppModel.size(game.byteCount))
                            .font(.title2.bold())
                            .monospacedDigit()
                    }
                    SplitBar(gameFiles: gameFiles, saves: savesBytes)
                        .frame(height: 10)
                        .accessibilityElement()
                        .accessibilityLabel(Text("Game files \(AppModel.size(gameFiles)), saves \(AppModel.size(savesBytes))"))
                }
                .padding(.vertical, 6)
                legendRow("Game Files", color: .indigo, size: saves == nil ? nil : gameFiles)
                legendRow("Saves", color: SplitBar.savesColor, size: saves)
            }

            Section {
                if let url = model.filesAppURL(for: game) {
                    Button {
                        openURL(url)
                    } label: {
                        Label("Show in Files", systemImage: "folder")
                    }
                }
                Button {
                    exportSaves(game)
                } label: {
                    Label(exporting ? LocalizedStringKey("Exporting…") : LocalizedStringKey("Export Saves"), systemImage: "square.and.arrow.up")
                }
                .disabled(exporting || savesBytes == 0)
                .accessibilityIdentifier("storage-export-saves")
            }

            Section {
                Button {
                    confirmOffload = true
                } label: {
                    HStack {
                        Label("Remove Game, Keep Saves", systemImage: "arrow.down.to.line")
                            .fontWeight(.semibold)
                        Spacer()
                        if saves != nil {
                            Text("Frees \(AppModel.size(gameFiles))")
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                .disabled(working)
                .accessibilityIdentifier("storage-offload")
            } footer: {
                Text("Deletes the game files but keeps saves, play history and the cover. Import the game again later and your saves come back automatically.")
            }

            Section {
                Button(role: .destructive) {
                    confirmDelete = true
                } label: {
                    Label("Delete Game and Saves", systemImage: "trash")
                }
                .disabled(working)
                .accessibilityIdentifier("storage-delete")
            } footer: {
                Text("Game files and saves are deleted permanently. Your history on Bangumi and VNDB is kept.")
            }
        }
        .task(id: game.byteCount) {
            let repository = model.repository
            saves = await Task.detached(priority: .utility) {
                guard let url = try? repository.savesURL(for: game) else { return Int64(0) }
                return GameScanner.directorySize(url)
            }.value
        }
        .confirmationDialog(
            "Remove “\(model.displayTitle(game))” and keep its saves?",
            isPresented: $confirmOffload,
            titleVisibility: .visible
        ) {
            Button("Remove Game", role: .destructive) {
                run { await model.offload(game) }
            }
        } message: {
            Text("Import the same folder again to play; your saves come back automatically.")
        }
        .confirmationDialog(
            "Delete “\(model.displayTitle(game))” and its saves?",
            isPresented: $confirmDelete,
            titleVisibility: .visible
        ) {
            Button("Delete Game and Saves", role: .destructive) {
                run { await model.deleteGame(game) }
            }
        } message: {
            Text("This can’t be undone.")
        }
    }

    private func header(_ game: GameRecord) -> some View {
        HStack(spacing: 14) {
            GameCover(game: game)
                .frame(width: 112, height: 63)
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text(verbatim: model.displayTitle(game))
                    .font(.title3.bold())
                    .lineLimit(2)
                subtitle(game)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
    }

    private func subtitle(_ game: GameRecord) -> Text {
        var text = Text(verbatim: game.engine.displayName)
        if let status = game.listEntry.status {
            let name = Text(verbatim: status.displayName)
            text = text + Text(verbatim: " · ") + (status == .finished ? name.foregroundColor(.green).bold() : name)
        }
        return text + Text(verbatim: " · \(model.recency(game))")
    }

    private func legendRow(_ title: LocalizedStringKey, color: Color, size: Int64?) -> some View {
        HStack(spacing: 12) {
            Circle().fill(color).frame(width: 10, height: 10)
            Text(title)
            Spacer()
            if let size {
                Text(verbatim: AppModel.size(size))
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            } else {
                ProgressView()
            }
        }
    }

    private func exportSaves(_ game: GameRecord) {
        exporting = true
        Task {
            do {
                export = try await model.exportSaves(game)
                showExport = true
            } catch {
                model.alert = String(localized: "Couldn’t export saves: \(error.localizedDescription)")
            }
            exporting = false
        }
    }

    /// Once the game leaves the library, `body` closes this page.
    private func run(_ action: @escaping () async -> Bool) {
        working = true
        Task {
            _ = await action()
            working = false
        }
    }
}

/// Game files and saves side by side, in proportion.
private struct SplitBar: View {
    static let savesColor = Color(red: 195 / 255, green: 90 / 255, blue: 0)
    let gameFiles: Int64
    let saves: Int64

    var body: some View {
        GeometryReader { proxy in
            let total = max(1, Double(gameFiles + saves))
            // A sliver of saves stays visible next to gigabytes of game.
            let savesWidth = saves > 0 ? max(4, proxy.size.width * Double(saves) / total) : 0
            HStack(spacing: saves > 0 ? 2 : 0) {
                Rectangle().fill(Color.indigo)
                Rectangle().fill(Self.savesColor).frame(width: savesWidth)
            }
        }
        .clipShape(Capsule())
    }
}
