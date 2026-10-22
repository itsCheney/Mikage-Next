import SwiftUI
import VNCore

/// Game details › Saves for an ONScripter game: the same save slots as its
/// in-game save and load screens, newest first, to start from, export or
/// delete.
struct GameSavesView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.openURL) private var openURL
    let gameID: UUID
    /// Closes the game page and plays from the slot.
    let onPlay: (Int) -> Void

    @State private var saves: [ONSSave] = []
    @State private var loaded = false
    @State private var confirmDelete: ONSSave?
    @State private var shareItem: URL?
    @State private var exporting = false

    private var game: GameRecord? { model.record(gameID) }

    private var savesDirectory: URL? {
        game.flatMap { try? model.repository.savesURL(for: $0) }
    }

    var body: some View {
        List {
            if let game {
                Section {
                    header(game)
                        .listRowInsets(EdgeInsets(top: 0, leading: 4, bottom: 0, trailing: 4))
                        .listRowBackground(Color.clear)
                    if let latest = saves.first {
                        continueButton(latest)
                            .listRowInsets(EdgeInsets())
                            .listRowBackground(Color.clear)
                    }
                }

                if !saves.isEmpty {
                    Section {
                        ForEach(saves) { save in
                            row(save)
                        }
                        .onDelete { offsets in
                            if let index = offsets.first {
                                confirmDelete = saves[index]
                            }
                        }
                    } header: {
                        HStack {
                            Text("Save Slots")
                            Spacer()
                            Text("Newest First")
                        }
                    } footer: {
                        Text("Empty slots aren’t shown. Deleted saves can’t be recovered.")
                    }
                } else if loaded {
                    Section {
                        Text("No saves yet. Saves made in the game appear here.")
                            .foregroundStyle(.secondary)
                    }
                }

                Section {
                    Button {
                        exportAll(game)
                    } label: {
                        Label(exporting ? LocalizedStringKey("Exporting…") : LocalizedStringKey("Export All Saves"), systemImage: "square.and.arrow.up")
                    }
                    .disabled(exporting || saves.isEmpty)
                    if let url = filesURL {
                        Button {
                            openURL(url)
                        } label: {
                            Label("Show in Files", systemImage: "folder")
                        }
                    }
                } footer: {
                    Text("Saves live in the game folder’s savedata, the same files the in-game save and load menus use.")
                }
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Saves")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            if !saves.isEmpty {
                EditButton()
            }
        }
        .task { reload() }
        .refreshable { reload() }
        .confirmationDialog(
            "Delete \(confirmDelete.map(Self.slotName) ?? "")?",
            isPresented: Binding(get: { confirmDelete != nil }, set: { if !$0 { confirmDelete = nil } }),
            titleVisibility: .visible,
            presenting: confirmDelete
        ) { save in
            Button("Delete Save", role: .destructive) { delete(save) }
        } message: { _ in
            Text("This can’t be undone.")
        }
        .sheet(isPresented: Binding(get: { shareItem != nil }, set: { if !$0 { shareItem = nil } })) {
            if let shareItem { ActivitySheet(items: [shareItem]) }
        }
    }

    private func header(_ game: GameRecord) -> some View {
        HStack(spacing: 14) {
            Group {
                if let url = saves.first?.thumbnailURL {
                    SaveThumbnail(url: url)
                } else {
                    GameCover(game: game)
                }
            }
            .frame(width: 112, height: 63)
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text(verbatim: model.displayTitle(game))
                    .font(.title2.bold())
                    .lineLimit(2)
                Text(verbatim: "\(game.engine.displayName) · ") + Text("\(saves.count) saves · \(AppModel.size(saves.reduce(0) { $0 + $1.byteCount }))")
            }
            .font(.subheadline)
            .foregroundStyle(.secondary)
        }
        .padding(.vertical, 6)
    }

    private func continueButton(_ save: ONSSave) -> some View {
        Button {
            onPlay(save.slot)
        } label: {
            HStack(spacing: 12) {
                Image(systemName: "play.fill")
                    .font(.title3)
                VStack(alignment: .leading, spacing: 1) {
                    Text("Continue from Latest Save")
                        .font(.headline)
                    Text(verbatim: "\(Self.slotName(save)) · \(Self.relativeDate(save.date))")
                        .font(.footnote)
                        .opacity(0.88)
                }
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 6)
            .frame(maxWidth: .infinity, minHeight: 48, alignment: .leading)
        }
        .nativeGlassButtonStyle(prominent: true)
        .controlSize(.large)
        .accessibilityIdentifier("saves-continue")
    }

    private func row(_ save: ONSSave) -> some View {
        HStack(spacing: 12) {
            Group {
                if let url = save.thumbnailURL {
                    SaveThumbnail(url: url)
                } else {
                    Color(uiColor: .tertiarySystemFill)
                        .overlay(Image(systemName: "photo").foregroundStyle(.tertiary))
                }
            }
            .frame(width: 80, height: 45)
            .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
            VStack(alignment: .leading, spacing: 2) {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(verbatim: Self.slotName(save))
                        .font(.body.weight(.semibold))
                    Text(verbatim: ONSSaveDate.short(save.date))
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                if !save.line.text.isEmpty {
                    Text(verbatim: save.line.text)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            Spacer(minLength: 0)
            Menu {
                Button {
                    onPlay(save.slot)
                } label: {
                    Label("Start from This Save", systemImage: "play.fill")
                }
                if let directory = savesDirectory {
                    Button {
                        shareItem = ONSSaveFiles.dataURL(slot: save.slot, in: directory)
                    } label: {
                        Label("Export", systemImage: "square.and.arrow.up")
                    }
                }
                Button(role: .destructive) {
                    confirmDelete = save
                } label: {
                    Label("Delete Save", systemImage: "trash")
                }
            } label: {
                Image(systemName: "ellipsis")
                    .font(.body.weight(.semibold))
                    .frame(width: 44, height: 44)
                    .contentShape(Circle())
            }
            .accessibilityLabel(Text("More Actions"))
        }
        .padding(.vertical, 4)
    }

    /// Files' own path for the savedata folder, which opens it in Files.
    private var filesURL: URL? {
        guard let directory = savesDirectory,
              FileManager.default.fileExists(atPath: directory.path),
              let path = directory.path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) else {
            return nil
        }
        return URL(string: "shareddocuments://\(path)")
    }

    private func reload() {
        guard let directory = savesDirectory else {
            saves = []
            loaded = true
            return
        }
        saves = ONSSaveFiles.saves(in: directory).values.sorted { $0.date > $1.date }
        loaded = true
    }

    private func delete(_ save: ONSSave) {
        guard let directory = savesDirectory else { return }
        AppDiagnostics.shared.event("saves", "delete", ["slot": String(save.slot)])
        do {
            try ONSSaveFiles.delete(slot: save.slot, in: directory)
        } catch {
            model.alert = String(localized: "Couldn’t delete the save: \(error.localizedDescription)")
        }
        reload()
    }

    private func exportAll(_ game: GameRecord) {
        exporting = true
        Task {
            do {
                shareItem = try await model.exportSaves(game)
            } catch {
                model.alert = String(localized: "Couldn’t export saves: \(error.localizedDescription)")
            }
            exporting = false
        }
    }

    private static let relativeFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .short
        formatter.timeStyle = .short
        formatter.doesRelativeDateFormatting = true
        return formatter
    }()

    static func slotName(_ save: ONSSave) -> String {
        save.isQuickSave ? String(localized: "Quick Save") : "No.\(save.slot)"
    }

    static func relativeDate(_ date: Date) -> String {
        relativeFormatter.string(from: date)
    }
}

/// A save's small screen picture, decoded off the main thread.
private struct SaveThumbnail: View {
    let url: URL
    @State private var image: UIImage?

    var body: some View {
        ZStack {
            Color(uiColor: .tertiarySystemFill)
            if let image {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFill()
            }
        }
        .task(id: url) {
            let url = self.url
            image = await Task.detached(priority: .utility) {
                UIImage(contentsOfFile: url.path)
            }.value
        }
    }
}
