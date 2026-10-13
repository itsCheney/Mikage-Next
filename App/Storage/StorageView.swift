import SwiftUI
import VNCore

/// Settings › Storage: what Mikage takes on the device, game by game, and
/// ways to get space back.
struct StorageView: View {
    @EnvironmentObject private var model: AppModel
    @State private var report: StorageReport?
    @State private var confirmClearLogs = false
    @State private var confirmOffloadFinished = false
    @State private var showMissingGames = false

    var body: some View {
        Form {
            Section {
                StorageSummaryCard(
                    used: usedBytes,
                    report: report,
                    gameCount: model.games.count,
                    canFree: canFreeBytes
                )
                .listRowInsets(EdgeInsets())
                .listRowBackground(Color.clear)
            }

            if let first = offloadCandidates.first {
                Section {
                    offloadRow(first: first)
                }
            }

            if !gamesBySize.isEmpty {
                Section {
                    ForEach(gamesBySize) { game in
                        NavigationLink {
                            StorageGameView(gameID: game.id)
                        } label: {
                            StorageGameRow(
                                game: game,
                                fraction: Double(game.byteCount) / Double(max(largestGame, 1))
                            )
                        }
                        .accessibilityIdentifier("storage-game-row")
                    }
                } header: {
                    HStack {
                        Text("Games")
                        Spacer()
                        Text("Largest First")
                    }
                }
            }

            Section {
                breakdownRow("Game Files", detail: gameFilesDetail, size: gameFilesBytes)
                breakdownRow("Saves", detail: savesDetail, size: report?.totalSaves)
                breakdownRow("Covers & Details", detail: String(localized: "Custom covers, Bangumi and VNDB artwork"), size: report?.metadata)
                HStack(spacing: 12) {
                    labelStack(Text("Diagnostic Logs"), detail: String(localized: "Up to about 16 MB, rotated automatically"))
                    sizeText(report?.logs)
                    Button("Clear", role: .destructive) { confirmClearLogs = true }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .disabled((report?.logs ?? 0) == 0)
                        .accessibilityIdentifier("storage-clear-logs")
                }
                if !model.missingGames.isEmpty {
                    Button {
                        showMissingGames = true
                    } label: {
                        HStack {
                            labelStack(
                                Text(verbatim: missingTitle),
                                detail: String(localized: "Link the folder again or delete the record")
                            )
                            Image(systemName: "chevron.right")
                                .font(.footnote.weight(.semibold))
                                .foregroundStyle(.tertiary)
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            } header: {
                Text("Everything Else")
            } footer: {
                Text("You can also manage game files and saves directly in the Files app under Mikage Next.")
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Storage")
        .navigationBarTitleDisplayMode(.inline)
        .task(id: measureKey) { report = await model.measureStorage() }
        .refreshable {
            await model.refreshLibrary()
            report = await model.measureStorage()
        }
        .sheet(isPresented: $showMissingGames) {
            MissingGamesView()
                .environmentObject(model)
        }
        .confirmationDialog("Clear recorded diagnostic logs?", isPresented: $confirmClearLogs, titleVisibility: .visible) {
            Button("Clear Logs", role: .destructive) {
                do { try AppDiagnostics.shared.clear() }
                catch { model.alert = String(localized: "Couldn’t clear logs: \(error.localizedDescription)") }
                Task { report = await model.measureStorage() }
            }
        }
        .confirmationDialog(
            "Remove finished games?",
            isPresented: $confirmOffloadFinished,
            titleVisibility: .visible
        ) {
            Button("Remove \(offloadCandidates.count) Games", role: .destructive) {
                let list = offloadCandidates
                Task { await model.offload(list) }
            }
        } message: {
            Text("Deletes their game files but keeps saves, play history and covers. Import a game again later and its saves come back automatically.")
        }
    }

    // MARK: Rows

    @ViewBuilder
    private func offloadRow(first: GameRecord) -> some View {
        let label = HStack(spacing: 12) {
            Image(systemName: "arrow.down.to.line")
                .font(.body.weight(.semibold))
                .foregroundStyle(.tint)
                .frame(width: 34, height: 34)
                .background(Color.indigo.opacity(0.14), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text("Remove Finished Games")
                    .foregroundStyle(.primary)
                Text(offloadSubtitle(first: first))
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 4)

        if offloadCandidates.count == 1 {
            NavigationLink {
                StorageGameView(gameID: first.id)
            } label: { label }
        } else {
            Button { confirmOffloadFinished = true } label: { label }
        }
    }

    private func offloadSubtitle(first: GameRecord) -> String {
        let size = AppModel.size(canFreeBytes)
        if offloadCandidates.count == 1 {
            return String(localized: "\(model.displayTitle(first)) · Keep the saves, free \(size)")
        }
        return String(localized: "\(model.displayTitle(first)) and \(offloadCandidates.count - 1) more · Keep the saves, free \(size)")
    }

    private func breakdownRow(_ title: LocalizedStringKey, detail: String, size: Int64?) -> some View {
        HStack(spacing: 12) {
            labelStack(Text(title), detail: detail)
            sizeText(size)
        }
    }

    private func labelStack(_ title: Text, detail: String) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            title
            Text(verbatim: detail)
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder
    private func sizeText(_ size: Int64?) -> some View {
        if let size {
            Text(verbatim: AppModel.size(size))
                .foregroundStyle(.secondary)
                .monospacedDigit()
        } else {
            ProgressView()
        }
    }

    // MARK: Numbers

    /// Re-measures when the library changes, such as after removing a game.
    private var measureKey: String {
        (model.games + model.missingGames).map { "\($0.id)\($0.byteCount)" }.joined()
    }

    private var gamesBySize: [GameRecord] {
        model.games.sorted { $0.byteCount > $1.byteCount }
    }

    private var largestGame: Int64 { gamesBySize.first?.byteCount ?? 0 }

    /// Game folders without the saves inside them, which have their own row.
    private var gameFilesBytes: Int64 {
        let folders = model.games.reduce(0) { $0 + $1.byteCount }
        let saves = report?.saves.values.reduce(0, +) ?? 0
        return max(0, folders - saves)
    }

    private var usedBytes: Int64? {
        guard let report else { return nil }
        return model.games.reduce(0) { $0 + $1.byteCount }
            + report.keptSaves.values.reduce(0, +)
            + report.metadata
            + report.logs
    }

    private var offloadCandidates: [GameRecord] {
        model.finishedGames.filter { gameFiles($0) > 0 }
    }

    private var canFreeBytes: Int64 {
        offloadCandidates.reduce(0) { $0 + gameFiles($1) }
    }

    private func gameFiles(_ game: GameRecord) -> Int64 {
        max(0, game.byteCount - (report?.saves[game.id] ?? 0))
    }

    private var gameFilesDetail: String {
        String(localized: "\(model.games.count) game folders in Documents")
    }

    private var savesDetail: String {
        let kept = report?.keptSaves.count ?? 0
        return kept > 0
            ? String(localized: "Each game’s savedata, and saves kept for \(kept) removed games")
            : String(localized: "Each game’s savedata")
    }

    private var missingTitle: String {
        String(localized: "\(model.missingGames.count) missing game records")
    }
}

/// The headline over a slowly drifting wall of the library's covers.
private struct StorageSummaryCard: View {
    @EnvironmentObject private var model: AppModel
    let used: Int64?
    let report: StorageReport?
    let gameCount: Int
    let canFree: Int64

    var body: some View {
        VStack(alignment: .leading, spacing: 24) {
            VStack(alignment: .leading, spacing: 2) {
                Text("USED BY MIKAGE")
                    .font(.footnote.weight(.semibold))
                    .tracking(1)
                    .opacity(0.85)
                if let used {
                    let parts = StorageFormat.parts(used)
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        Text(verbatim: parts.number)
                            .font(.system(size: 44, weight: .bold))
                            .monospacedDigit()
                        Text(verbatim: parts.unit)
                            .font(.title2.weight(.semibold))
                    }
                } else {
                    ProgressView()
                        .tint(.white)
                        .frame(height: 52)
                }
                if let total = report?.deviceTotal, let available = report?.deviceAvailable {
                    Text("\(AppModel.size(available)) left of \(AppModel.size(total)) on this \(UIDevice.current.model)")
                        .font(.subheadline)
                        .opacity(0.88)
                        .padding(.top, 2)
                }
            }

            HStack(spacing: 8) {
                chip("Games", value: gameCount.formatted())
                chip("Saves", value: report.map { AppModel.size($0.totalSaves) } ?? "–")
                chip("Can Free", value: report == nil ? "–" : AppModel.size(canFree))
            }
        }
        .foregroundStyle(.white)
        .padding(.horizontal, 18)
        .padding(.top, 22)
        .padding(.bottom, 18)
        .frame(maxWidth: .infinity, minHeight: 250, alignment: .leading)
        .background {
            ZStack {
                Color(red: 20 / 255, green: 20 / 255, blue: 42 / 255)
                CoverWall(games: model.games)
                LinearGradient(
                    stops: [
                        .init(color: Color(red: 14 / 255, green: 14 / 255, blue: 36 / 255).opacity(0.62), location: 0),
                        .init(color: Color(red: 14 / 255, green: 14 / 255, blue: 36 / 255).opacity(0.40), location: 0.46),
                        .init(color: Color(red: 14 / 255, green: 14 / 255, blue: 36 / 255).opacity(0.72), location: 1)
                    ],
                    startPoint: .top,
                    endPoint: .bottom
                )
            }
            .accessibilityHidden(true)
        }
        .clipShape(RoundedRectangle(cornerRadius: 30, style: .continuous))
        .shadow(color: Color(red: 30 / 255, green: 30 / 255, blue: 70 / 255).opacity(0.22), radius: 18, y: 14)
        .environment(\.colorScheme, .dark)
        .accessibilityElement(children: .combine)
    }

    private func chip(_ title: LocalizedStringKey, value: String) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(title)
                .font(.caption)
                .opacity(0.85)
            Text(verbatim: value)
                .font(.body.weight(.bold))
                .monospacedDigit()
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 9)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .strokeBorder(.white.opacity(0.28), lineWidth: 1)
        )
    }
}

/// Lanes of covers tilted behind the summary, sliding in alternate
/// directions. Still when Reduce Motion is on.
private struct CoverWall: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    let games: [GameRecord]
    @State private var moving = false

    private static let tile = CGSize(width: 92, height: 52)
    private static let spacing: CGFloat = 8
    private static let perLane = 6
    private static let lanes = 7

    var body: some View {
        let order = games.filter { model.coverURL($0) != nil }
        if !order.isEmpty {
            VStack(spacing: Self.spacing) {
                ForEach(0..<Self.lanes, id: \.self) { lane in
                    laneView(lane, order: order)
                }
            }
            .frame(width: 560, alignment: .leading)
            .rotationEffect(.degrees(-14))
            .offset(x: -40, y: -20)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .clipped()
            .onAppear { moving = !reduceMotion }
            .onChange(of: reduceMotion) { moving = !$0 }
        }
    }

    private func laneView(_ lane: Int, order: [GameRecord]) -> some View {
        let picks = (0..<Self.perLane).map { order[(lane * 3 + $0 * 5) % order.count] }
        let width = CGFloat(Self.perLane) * (Self.tile.width + Self.spacing)
        let leftward = lane % 2 == 0
        let duration = Double(48 + (lane * 7) % 20)
        return HStack(spacing: Self.spacing) {
            // Doubled so the loop has no seam.
            ForEach(Array((picks + picks).enumerated()), id: \.offset) { _, game in
                ArtworkImage(url: model.coverURL(game))
                    .frame(width: Self.tile.width, height: Self.tile.height)
                    .grayscale(game.availability == .unsupported ? 0.85 : 0)
                    .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
                    .shadow(color: .black.opacity(0.25), radius: 5, y: 4)
            }
        }
        .offset(x: (lane % 2 == 1 ? -50 : 0) + (leftward == moving ? -width : 0))
        .animation(
            moving ? .linear(duration: duration).repeatForever(autoreverses: false) : .default,
            value: moving
        )
    }
}

/// One game in the storage list: art, size, and a bar against the largest.
private struct StorageGameRow: View {
    @EnvironmentObject private var model: AppModel
    let game: GameRecord
    let fraction: Double

    private var runnable: Bool { game.engine.isLaunchSupported }

    var body: some View {
        HStack(spacing: 12) {
            GameCover(game: game)
                .frame(width: 64, height: 36)
                .grayscale(runnable ? 0 : 0.85)
                .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
            VStack(alignment: .leading, spacing: 5) {
                HStack(spacing: 8) {
                    Text(verbatim: model.displayTitle(game))
                        .lineLimit(1)
                    Spacer(minLength: 0)
                    Text(verbatim: AppModel.size(game.byteCount))
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                GeometryReader { proxy in
                    ZStack(alignment: .leading) {
                        Capsule().fill(Color(uiColor: .systemGray5))
                        Capsule()
                            .fill(runnable ? AnyShapeStyle(TintShapeStyle()) : AnyShapeStyle(Color(uiColor: .systemGray2)))
                            .frame(width: max(4, proxy.size.width * min(1, max(0, fraction))))
                    }
                }
                .frame(height: 4)
                subtitle
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .padding(.vertical, 4)
        .accessibilityElement(children: .combine)
    }

    private var subtitle: Text {
        guard runnable else {
            return Text(verbatim: "\(game.engine.displayName) · ") + Text("Runtime not available yet")
        }
        let recency = Text(verbatim: " · \(model.recency(game))")
        if let status = game.listEntry.status {
            let name = Text(verbatim: status.displayName)
            return (status == .finished ? name.foregroundColor(.green).bold() : name) + recency
        }
        return Text(verbatim: game.engine.displayName) + recency
    }
}

enum StorageFormat {
    /// "32.5" and "GB" apart, for a headline that sets them in two sizes.
    static func parts(_ count: Int64) -> (number: String, unit: String) {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        formatter.includesUnit = false
        let number = formatter.string(fromByteCount: count)
        formatter.includesUnit = true
        formatter.includesCount = false
        return (number, formatter.string(fromByteCount: count))
    }
}
