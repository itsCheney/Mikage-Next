import SwiftUI
import UIKit
import VNCore

/// Mikage's backlog, save and load screens for ONScripter games. They open
/// in place of the game's own lookback, save and load screens (and from the
/// game menu) while the engine waits for the player's choice.
enum ONSPanel: Equatable {
    case backlog, save, load
}

struct ONSBacklogLine: Identifiable, Equatable {
    /// Index in the runtime's backlog.
    let id: Int
    let line: ONSLine
    let hasVoice: Bool
    let canJump: Bool
    let isCurrent: Bool
}

@MainActor
final class ONSPanelModel: ObservableObject {
    @Published var panel: ONSPanel?
    @Published var lines: [ONSBacklogLine] = []
    @Published var slotCount = 0
    @Published var saves: [Int: ONSSave] = [:]
    @Published var thumbnails: [Int: UIImage] = [:]
    /// The picked line index or slot number, waiting for confirmation.
    @Published var selection: Int?

    let gameTitle: String
    var onCancel: (() -> Void)?
    var onSave: ((Int) -> Void)?
    var onLoad: ((Int) -> Void)?
    var onJump: ((Int) -> Void)?
    var onReplayVoice: ((Int) -> Void)?

    init(gameTitle: String) {
        self.gameTitle = gameTitle
    }

    /// The most recently written save, marked Latest.
    var latestSlot: Int? {
        saves.values.max { $0.date < $1.date }?.slot
    }
}

/// Shows the panels over the engine window, above the game menu.
@MainActor
final class ONSPanelCoordinator {
    private let model: ONSPanelModel
    private let savesDirectory: URL
    private var hostingController: UIHostingController<ONSPanelView>?
    private weak var containerView: UIView?

    init(gameTitle: String, savesDirectory: URL) {
        model = ONSPanelModel(gameTitle: gameTitle)
        self.savesDirectory = savesDirectory
    }

    var isPresented: Bool { model.panel != nil }

    func install(
        in window: UIWindow,
        onCancel: @escaping () -> Void,
        onSave: @escaping (Int) -> Void,
        onLoad: @escaping (Int) -> Void,
        onJump: @escaping (Int) -> Void,
        onReplayVoice: @escaping (Int) -> Void
    ) {
        guard let rootController = window.rootViewController else { return }
        let controller = UIHostingController(rootView: ONSPanelView(model: model))
        controller.view.backgroundColor = .clear
        controller.view.isOpaque = false
        controller.view.isUserInteractionEnabled = false
        controller.view.translatesAutoresizingMaskIntoConstraints = false
        rootController.addChild(controller)
        rootController.view.addSubview(controller.view)
        NSLayoutConstraint.activate([
            controller.view.leadingAnchor.constraint(equalTo: rootController.view.leadingAnchor),
            controller.view.trailingAnchor.constraint(equalTo: rootController.view.trailingAnchor),
            controller.view.topAnchor.constraint(equalTo: rootController.view.topAnchor),
            controller.view.bottomAnchor.constraint(equalTo: rootController.view.bottomAnchor)
        ])
        controller.didMove(toParent: rootController)
        hostingController = controller
        containerView = rootController.view

        model.onCancel = onCancel
        model.onSave = onSave
        model.onLoad = onLoad
        model.onJump = onJump
        model.onReplayVoice = onReplayVoice
    }

    func present(_ panel: ONSPanel, lines: [ONSBacklogLine], slotCount: Int) {
        AppDiagnostics.shared.event("onsPanel", "show", ["panel": String(describing: panel), "lines": String(lines.count)])
        guard let controller = hostingController else { return }
        model.selection = nil
        model.lines = lines
        model.slotCount = slotCount
        if panel != .backlog {
            loadSaves()
        }
        controller.view.isUserInteractionEnabled = true
        containerView?.bringSubviewToFront(controller.view)
        withAnimation(.easeOut(duration: 0.2)) { model.panel = panel }
    }

    func dismiss() {
        guard model.panel != nil else { return }
        AppDiagnostics.shared.event("onsPanel", "hide")
        withAnimation(.easeIn(duration: 0.15)) { model.panel = nil }
        model.selection = nil
        hostingController?.view.isUserInteractionEnabled = false
    }

    func remove() {
        hostingController?.willMove(toParent: nil)
        hostingController?.view.removeFromSuperview()
        hostingController?.removeFromParent()
        hostingController = nil
        containerView = nil
    }

    private func loadSaves() {
        let saves = ONSSaveFiles.saves(in: savesDirectory)
        let urls = saves.compactMapValues(\.thumbnailURL)
        let target = model
        target.saves = saves
        target.thumbnails = [:]
        Task { [weak target] in
            let images = await Task.detached(priority: .userInitiated) {
                urls.compactMapValues { UIImage(contentsOfFile: $0.path) }
            }.value
            target?.thumbnails = images
        }
    }
}

// MARK: Views

private struct ONSPanelView: View {
    @ObservedObject var model: ONSPanelModel

    var body: some View {
        ZStack {
            if let panel = model.panel {
                Color(red: 10 / 255, green: 14 / 255, blue: 30 / 255).opacity(0.5)
                    .ignoresSafeArea()
                    .transition(.opacity)
                Group {
                    switch panel {
                    case .backlog: ONSBacklogPanel(model: model)
                    case .save, .load: ONSSlotPanel(model: model, saving: panel == .save)
                    }
                }
                .padding(.horizontal, 18)
                .padding(.vertical, 16)
                .frame(maxWidth: 860, maxHeight: 620)
                .background(
                    Color(red: 22 / 255, green: 26 / 255, blue: 46 / 255).opacity(0.46),
                    in: RoundedRectangle(cornerRadius: 30, style: .continuous)
                )
                .nativeGlassPanel(30)
                .padding(.horizontal, 56)
                .padding(.vertical, 14)
                .transition(.scale(scale: 0.96).combined(with: .opacity))
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .foregroundStyle(.white)
        .environment(\.colorScheme, .dark)
        .tint(.indigo)
    }
}

private struct ONSPanelHeader: View {
    let symbol: String
    let title: LocalizedStringKey
    let subtitle: Text
    let onClose: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: symbol)
                .font(.body.weight(.semibold))
                .frame(width: 36, height: 36)
                .background(Color.white.opacity(0.14), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .strokeBorder(Color.white.opacity(0.26))
                )
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 1) {
                Text(title)
                    .font(.system(size: 18, weight: .bold))
                    .accessibilityAddTraits(.isHeader)
                subtitle
                    .font(.footnote)
                    .foregroundStyle(Color.white.opacity(0.75))
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            RoundButton(symbol: "xmark", label: "Back to Game", action: onClose)
        }
        .frame(minHeight: 44)
    }
}

/// The bottom row: a hint, or the question for the picked line or slot.
private struct ONSConfirmBar: View {
    let symbol: String?
    let title: Text
    let subtitle: Text
    let action: Text
    let onCancel: () -> Void
    let onConfirm: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            if let symbol {
                Image(systemName: symbol)
                    .font(.body.weight(.semibold))
                    .accessibilityHidden(true)
            }
            VStack(alignment: .leading, spacing: 1) {
                title
                    .font(.subheadline.weight(.semibold))
                subtitle
                    .font(.caption)
                    .foregroundStyle(Color.white.opacity(0.78))
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            Button(action: onCancel) {
                Text("Cancel")
                    .font(.subheadline.weight(.semibold))
                    .padding(.horizontal, 6)
                    .frame(minHeight: 32)
            }
            .nativeGlassButtonStyle()
            Button(action: onConfirm) {
                action
                    .font(.subheadline.weight(.semibold))
                    .padding(.horizontal, 6)
                    .frame(minHeight: 32)
            }
            .nativeGlassButtonStyle(prominent: true)
        }
        .padding(.leading, 16)
        .padding(.trailing, 6)
        .frame(minHeight: 54)
        .background(Color.white.opacity(0.14), in: Capsule())
        .overlay(Capsule().strokeBorder(Color.white.opacity(0.26)))
    }
}

private struct ONSHint: View {
    var symbol: String?
    let text: Text

    var body: some View {
        HStack(spacing: 8) {
            if let symbol {
                Image(systemName: symbol)
                    .accessibilityHidden(true)
            }
            text
                .lineLimit(2)
        }
        .font(.footnote)
        .foregroundStyle(Color.white.opacity(0.75))
        .padding(.horizontal, 6)
        .frame(maxWidth: .infinity, minHeight: 48, alignment: .leading)
    }
}

// MARK: Backlog

private struct ONSBacklogPanel: View {
    @ObservedObject var model: ONSPanelModel

    var body: some View {
        VStack(spacing: 10) {
            ONSPanelHeader(
                symbol: "text.alignleft",
                title: "Backlog",
                subtitle: Text(verbatim: "\(model.gameTitle) · ") + Text("Tap a line to jump back to it"),
                onClose: { model.onCancel?() }
            )
            lineList
            footer
        }
    }

    private var lineList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(model.lines) { entry in
                        ONSBacklogRow(
                            entry: entry,
                            selected: model.selection == entry.id,
                            onPick: { pick(entry) },
                            onReplay: { model.onReplayVoice?(entry.id) }
                        )
                        .id(entry.id)
                    }
                }
                .padding(.top, 28)
            }
            .mask(
                VStack(spacing: 0) {
                    LinearGradient(colors: [.clear, .black], startPoint: .top, endPoint: .bottom)
                        .frame(height: 40)
                    Color.black
                }
            )
            .onAppear {
                if let last = model.lines.last {
                    proxy.scrollTo(last.id, anchor: .bottom)
                }
            }
        }
    }

    @ViewBuilder
    private var footer: some View {
        if let index = model.selection {
            ONSConfirmBar(
                symbol: "arrow.uturn.backward",
                title: Text("Jump back to this line?"),
                subtitle: Text("Progress after it is replaced, and anything unsaved is lost."),
                action: Text("Jump Back"),
                onCancel: { model.selection = nil },
                onConfirm: { model.onJump?(index) }
            )
        } else {
            ONSHint(
                symbol: "arrow.uturn.backward",
                text: Text("You can jump back to any of the last 200 lines this session. Earlier lines are read-only.")
            )
        }
    }

    private func pick(_ entry: ONSBacklogLine) {
        guard entry.canJump else { return }
        model.selection = model.selection == entry.id ? nil : entry.id
    }
}

private struct ONSBacklogRow: View {
    let entry: ONSBacklogLine
    let selected: Bool
    let onPick: () -> Void
    let onReplay: () -> Void

    var body: some View {
        HStack(spacing: 6) {
            Button(action: onPick) {
                VStack(alignment: .leading, spacing: 2) {
                    if !entry.line.name.isEmpty {
                        Text(verbatim: entry.line.name)
                            .font(.footnote.weight(.semibold))
                            .foregroundStyle(Self.color(for: entry.line.name))
                    }
                    Text(verbatim: entry.line.text)
                        .font(.body)
                        .lineSpacing(3)
                        .foregroundStyle(entry.line.name.isEmpty ? Color.white.opacity(0.82) : Color.white)
                        .multilineTextAlignment(.leading)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(!entry.canJump)
            .accessibilityAddTraits(selected ? .isSelected : [])
            .accessibilityHint(entry.canJump ? Text("Jump back to this line") : Text(verbatim: ""))
            if entry.isCurrent {
                Text("Now")
                    .font(.caption.weight(.semibold))
                    .padding(.horizontal, 9)
                    .padding(.vertical, 4)
                    .background(Color.white.opacity(0.18), in: RoundedRectangle(cornerRadius: 9, style: .continuous))
                    .padding(.trailing, entry.hasVoice ? 0 : 8)
            }
            if entry.hasVoice {
                Button(action: onReplay) {
                    Image(systemName: "speaker.wave.2")
                        .font(.body.weight(.medium))
                        .frame(width: 44, height: 44)
                        .contentShape(Circle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(Text("Replay Voice"))
            }
        }
        .padding(.trailing, 4)
        .background(
            selected ? Color.indigo.opacity(0.34) : Color.clear,
            in: RoundedRectangle(cornerRadius: 18, style: .continuous)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .strokeBorder(selected ? Color.white.opacity(0.45) : Color.clear)
        )
    }

    private static let palette: [Color] = [
        Color(red: 180 / 255, green: 218 / 255, blue: 255 / 255),
        Color(red: 255 / 255, green: 217 / 255, blue: 168 / 255),
        Color(red: 201 / 255, green: 240 / 255, blue: 196 / 255),
        Color(red: 245 / 255, green: 194 / 255, blue: 231 / 255),
        Color(red: 217 / 255, green: 200 / 255, blue: 255 / 255),
        Color(red: 255 / 255, green: 229 / 255, blue: 163 / 255)
    ]

    /// Each speaker keeps a color, picked from the name.
    static func color(for name: String) -> Color {
        let hash = name.unicodeScalars.reduce(UInt32(2166136261)) { ($0 ^ $1.value) &* 16777619 }
        return palette[Int(hash % UInt32(palette.count))]
    }
}

// MARK: Save and load

private struct ONSSlotPanel: View {
    @ObservedObject var model: ONSPanelModel
    let saving: Bool

    private let columns = Array(repeating: GridItem(.flexible(minimum: 0), spacing: 8), count: 3)

    var body: some View {
        VStack(spacing: 10) {
            ONSPanelHeader(
                symbol: saving ? "square.and.arrow.down" : "square.and.arrow.up",
                title: saving ? "Save" : "Load",
                subtitle: Text(verbatim: model.gameTitle),
                onClose: { model.onCancel?() }
            )
            ScrollView {
                LazyVGrid(columns: columns, spacing: 8) {
                    ForEach(1...max(model.slotCount, 1), id: \.self) { slot in
                        ONSSlotCell(
                            slot: slot,
                            save: model.saves[slot],
                            thumbnail: model.thumbnails[slot],
                            latest: model.latestSlot == slot,
                            saving: saving,
                            selected: model.selection == slot
                        ) {
                            pick(slot)
                        }
                    }
                }
            }
            footer
        }
    }

    @ViewBuilder
    private var footer: some View {
        if let slot = model.selection {
            if saving {
                let exists = model.saves[slot] != nil
                ONSConfirmBar(
                    symbol: nil,
                    title: exists ? Text("Overwrite No.\(slot)?") : Text("Save to No.\(slot)"),
                    subtitle: exists ? Text("The old save is replaced with your current progress.") : Text("Saves your current progress and screen."),
                    action: exists ? Text("Overwrite") : Text("Save"),
                    onCancel: { model.selection = nil },
                    onConfirm: { model.onSave?(slot) }
                )
            } else {
                ONSConfirmBar(
                    symbol: nil,
                    title: Text("Load No.\(slot)?"),
                    subtitle: Text("Unsaved progress will be lost."),
                    action: Text("Load"),
                    onCancel: { model.selection = nil },
                    onConfirm: { model.onLoad?(slot) }
                )
            }
        } else {
            ONSHint(text: Text("Each slot is the game’s own save file in savedata, so its original menu can still read them."))
        }
    }

    private func pick(_ slot: Int) {
        guard saving || model.saves[slot] != nil else { return }
        model.selection = model.selection == slot ? nil : slot
    }
}

private struct ONSSlotCell: View {
    let slot: Int
    let save: ONSSave?
    let thumbnail: UIImage?
    let latest: Bool
    let saving: Bool
    let selected: Bool
    let onPick: () -> Void

    var body: some View {
        Button(action: onPick) {
            HStack(spacing: 9) {
                picture
                    .frame(width: 88, height: 50)
                    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                VStack(alignment: .leading, spacing: 1) {
                    HStack(spacing: 6) {
                        Text(verbatim: "No.\(slot)")
                            .font(.caption.weight(.bold))
                            .foregroundStyle(Color.white.opacity(0.75))
                        if latest {
                            Text("Latest")
                                .font(.system(size: 10, weight: .semibold))
                                .padding(.horizontal, 6)
                                .padding(.vertical, 1)
                                .background(Color.indigo.opacity(0.62), in: RoundedRectangle(cornerRadius: 6, style: .continuous))
                        }
                    }
                    if let save {
                        Text(verbatim: ONSSaveDate.short(save.date))
                            .font(.footnote.weight(.semibold))
                            .monospacedDigit()
                            .lineLimit(1)
                        Text(verbatim: save.line.text)
                            .font(.caption)
                            .foregroundStyle(Color.white.opacity(0.78))
                            .lineLimit(1)
                    } else {
                        Text("Empty Slot")
                            .font(.footnote.weight(.semibold))
                            .lineLimit(1)
                        if saving {
                            Text("Tap to save here")
                                .font(.caption)
                                .foregroundStyle(Color.white.opacity(0.78))
                                .lineLimit(1)
                        }
                    }
                }
                Spacer(minLength: 0)
            }
            .padding(7)
            .frame(maxWidth: .infinity, minHeight: 66, alignment: .leading)
            .background(
                selected ? Color.indigo.opacity(0.36) : Color.white.opacity(0.08),
                in: RoundedRectangle(cornerRadius: 18, style: .continuous)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .strokeBorder(Color.white.opacity(selected ? 0.85 : 0.18), lineWidth: 1.5)
            )
            .contentShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        }
        .buttonStyle(.plain)
        .opacity(save == nil && !saving ? 0.4 : 1)
        .disabled(save == nil && !saving)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    @ViewBuilder
    private var picture: some View {
        if let thumbnail {
            Image(uiImage: thumbnail)
                .resizable()
                .scaledToFill()
        } else if save != nil {
            Color.white.opacity(0.16)
                .overlay(Image(systemName: "photo").foregroundStyle(Color.white.opacity(0.5)))
        } else {
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(Color.white.opacity(0.4), style: StrokeStyle(lineWidth: 1.5, dash: [4, 3]))
                .overlay {
                    if saving {
                        Image(systemName: "plus")
                            .font(.body.weight(.medium))
                            .foregroundStyle(Color.white.opacity(0.7))
                    }
                }
        }
    }
}

enum ONSSaveDate {
    private static let formatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.setLocalizedDateFormatFromTemplate("MMddHHmm")
        return formatter
    }()

    /// Month, day and time, as on a save slot: 10/14 21:32.
    static func short(_ date: Date) -> String {
        formatter.string(from: date)
    }
}
