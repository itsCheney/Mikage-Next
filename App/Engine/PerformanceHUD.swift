import SwiftUI
import UIKit

/// Where the performance HUD rests: a corner of the safe area, or tucked
/// against the left or right edge like Picture in Picture, where a small
/// tab keeps the live frame rate in view.
struct PerformanceHUDPlacement: Equatable {
    var leading = true
    var top = true
    var tucked = false

    private static func key(_ detail: PerformanceDetail) -> String {
        "performanceHUD.placement.\(detail.rawValue)"
    }

    /// Each HUD remembers its own spot between sessions.
    static func saved(for detail: PerformanceDetail) -> PerformanceHUDPlacement {
        guard let stored = UserDefaults.standard.dictionary(forKey: key(detail)) as? [String: Bool] else {
            return PerformanceHUDPlacement()
        }
        return PerformanceHUDPlacement(
            leading: stored["leading"] ?? true,
            top: stored["top"] ?? true,
            tucked: stored["tucked"] ?? false
        )
    }

    func save(for detail: PerformanceDetail) {
        UserDefaults.standard.set(
            ["leading": leading, "top": top, "tucked": tucked],
            forKey: Self.key(detail)
        )
    }
}

extension KRKRPerformanceSnapshot {
    /// Nothing on screen changed in the last second: the HUD says "Idle"
    /// rather than 0 FPS.
    var isIdle: Bool { framesPerSecond < 0.5 }
}

private enum HUDPalette {
    static let green = Color(red: 0x34 / 255, green: 0xC7 / 255, blue: 0x59 / 255)
    static let amber = Color(red: 1, green: 0xB3 / 255, blue: 0x40 / 255)
    static let red = Color(red: 1, green: 0x69 / 255, blue: 0x61 / 255)
    static let idle = Color(red: 0x8E / 255, green: 0x8E / 255, blue: 0x93 / 255)
    static let ground = Color(red: 14 / 255, green: 16 / 255, blue: 30 / 255)

    /// Green from 50 FPS, amber from 30, red below.
    static func rate(_ stats: KRKRPerformanceSnapshot) -> Color {
        if stats.isIdle { return idle }
        if stats.framesPerSecond >= 50 { return green }
        return stats.framesPerSecond >= 30 ? amber : red
    }

    /// A frame within 60 FPS is plain, within 30 FPS amber, longer red.
    static func frameTime(_ milliseconds: Double) -> Color {
        if milliseconds <= 17.5 { return .white.opacity(0.55) }
        return milliseconds <= 33.4 ? amber : red
    }
}

private enum HUDFormat {
    static func bytes(_ count: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(count), countStyle: .memory)
    }

    static func milliseconds(_ value: Double) -> String {
        String(format: "%.1f ms", value)
    }

    static func percent(_ value: Double) -> String {
        String(format: value < 10 ? "%.1f%%" : "%.0f%%", value)
    }
}

/// Dark tinted glass, readable over any game art.
private struct HUDGlass<S: Shape>: ViewModifier {
    let shape: S

    func body(content: Content) -> some View {
        content
            .background(shape.fill(HUDPalette.ground.opacity(0.6)))
            .background(.ultraThinMaterial, in: shape)
            .overlay(shape.stroke(.white.opacity(0.2), lineWidth: 1))
            .shadow(color: .black.opacity(0.28), radius: 10, y: 6)
    }
}

/// Rounded only on the side facing into the screen.
private struct EdgeTabShape: Shape {
    let roundsRight: Bool
    let radius: CGFloat

    func path(in rect: CGRect) -> Path {
        let corners: UIRectCorner = roundsRight ? [.topRight, .bottomRight] : [.topLeft, .bottomLeft]
        return Path(UIBezierPath(
            roundedRect: rect,
            byRoundingCorners: corners,
            cornerRadii: CGSize(width: radius, height: radius)
        ).cgPath)
    }
}

/// The HUD the coordinator hosts: the simple or full panel, or the tab it
/// tucks into at the screen edge.
struct PerformanceHUDView: View {
    /// Seconds of frame times kept for the graph.
    static let historyLength = FrameTimeGraph.slots

    @ObservedObject var model: KRKROverlayModel

    static func tabSize(_ detail: PerformanceDetail) -> CGSize {
        detail == .full ? CGSize(width: 42, height: 116) : CGSize(width: 36, height: 72)
    }

    var body: some View {
        Group {
            if model.hudPlacement.tucked {
                HUDTab(
                    stats: model.performance,
                    detail: model.performanceDetail,
                    onLeadingEdge: model.hudPlacement.leading
                ) {
                    model.onTuckHUD?(false)
                }
            } else {
                switch model.performanceDetail {
                case .simple:
                    SimplePerformanceHUD(stats: model.performance, history: model.frameHistory)
                case .full:
                    FullPerformanceHUD(
                        stats: model.performance,
                        history: model.frameHistory,
                        engineName: model.engineName
                    ) {
                        model.onTuckHUD?(true)
                    }
                    .frame(width: model.hudWidth)
                }
            }
        }
        .environment(\.colorScheme, .dark)
        // Placement is physical: a HUD on the left edge stays on the left.
        .environment(\.layoutDirection, .leftToRight)
    }
}

// MARK: Simple

/// Two rows: the frame rate with its trend and draw calls, then CPU,
/// memory and GPU memory.
private struct SimplePerformanceHUD: View {
    let stats: KRKRPerformanceSnapshot
    let history: [Double?]

    var body: some View {
        let color = HUDPalette.rate(stats)
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 10) {
                Circle()
                    .fill(color)
                    .frame(width: 8, height: 8)
                HStack(alignment: .firstTextBaseline, spacing: 4) {
                    Text(verbatim: stats.isIdle ? "—" : String(format: "%.0f", stats.framesPerSecond))
                        .font(.system(size: 17, weight: .bold, design: .monospaced))
                    Group {
                        if stats.isIdle {
                            Text("Idle")
                        } else {
                            Text(verbatim: "FPS")
                        }
                    }
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(.white.opacity(0.72))
                }
                if !stats.isIdle {
                    Sparkline(values: history.suffix(20).map { $0 ?? 0 }, color: color)
                        .frame(width: 44, height: 18)
                }
                if stats.drawCallsPerFrame >= 0 {
                    Spacer(minLength: 6)
                    metric(Text(verbatim: "DC"), String(format: "%.0f", stats.drawCallsPerFrame))
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            HStack(spacing: 12) {
                if stats.processCPUPercent >= 0 {
                    metric(Text(verbatim: "CPU"), String(format: "%.0f%%", stats.processCPUPercent))
                }
                metric(Text("RAM"), HUDFormat.bytes(stats.residentMemoryBytes))
                metric(Text(verbatim: "GPU"), HUDFormat.bytes(stats.gpuMemoryBytes))
            }
            .padding(.leading, 18)
        }
        .padding(EdgeInsets(top: 8, leading: 12, bottom: 9, trailing: 14))
        .fixedSize()
        .foregroundStyle(.white)
        .modifier(HUDGlass(shape: RoundedRectangle(cornerRadius: 16, style: .continuous)))
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text("Performance"))
        .accessibilityIdentifier("krkr-performance-hud")
    }

    private func metric(_ key: Text, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 4) {
            key
                .font(.system(size: 11))
                .foregroundStyle(.white.opacity(0.64))
            Text(verbatim: value)
                .font(.system(size: 13, weight: .semibold, design: .monospaced))
        }
        .lineLimit(1)
    }
}

/// Frame interval per second; the line rises when frames take longer.
private struct Sparkline: View {
    let values: [Double]
    let color: Color

    var body: some View {
        Canvas { context, size in
            guard values.count > 1 else { return }
            var path = Path()
            for (index, value) in values.enumerated() {
                let point = CGPoint(
                    x: CGFloat(index) * size.width / CGFloat(values.count - 1),
                    y: size.height - 1 - CGFloat(min(value, 50) / 50) * (size.height - 2)
                )
                if index == 0 { path.move(to: point) } else { path.addLine(to: point) }
            }
            context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round))
        }
        .accessibilityHidden(true)
    }
}

// MARK: Full

private struct FullPerformanceHUD: View {
    let stats: KRKRPerformanceSnapshot
    let history: [Double?]
    let engineName: String
    let tuck: () -> Void

    var body: some View {
        VStack(spacing: 8) {
            Button(action: tuck) {
                Capsule()
                    .fill(.white.opacity(0.4))
                    .frame(width: 36, height: 5)
                    .frame(width: 72, height: 14)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .padding(.top, -4)
            .padding(.bottom, -6)
            .accessibilityLabel(Text("Tuck Away Performance Stats"))
            .accessibilityHint(Text("Drag the panel to move it. Drag it past the screen edge to tuck it away."))
            header
            FrameTimeGraph(history: history)
            cards
        }
        .padding(12)
        .foregroundStyle(.white)
        .modifier(HUDGlass(shape: RoundedRectangle(cornerRadius: 24, style: .continuous)))
        .accessibilityIdentifier("krkr-performance-hud")
    }

    private var header: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(HUDPalette.rate(stats))
                .frame(width: 9, height: 9)
            Text(verbatim: stats.isIdle ? "—" : String(format: "%.1f", stats.framesPerSecond))
                .font(.system(size: 28, weight: .bold, design: .monospaced))
                .lineLimit(1)
            Group {
                if stats.isIdle {
                    Text("Idle")
                } else {
                    Text(verbatim: "FPS")
                }
            }
            .font(.system(size: 12, weight: .semibold))
            .foregroundStyle(.white.opacity(0.72))
            .frame(maxHeight: .infinity, alignment: .bottom)
            .padding(.bottom, 6)
            if !stats.isIdle && stats.frameTimeMilliseconds > 0 {
                (Text("Interval") + Text(verbatim: " ")
                    + Text(verbatim: HUDFormat.milliseconds(stats.frameTimeMilliseconds))
                        .font(.system(size: 12, weight: .semibold, design: .monospaced))
                        .foregroundColor(.white))
                    .font(.system(size: 12))
                    .foregroundStyle(.white.opacity(0.72))
                    .lineLimit(1)
                    .frame(maxHeight: .infinity, alignment: .bottom)
                    .padding(.bottom, 6)
                    .padding(.leading, 6)
            }
            Spacer(minLength: 4)
            VStack(alignment: .trailing, spacing: 1) {
                if !engineName.isEmpty {
                    Text(verbatim: engineName)
                        .font(.system(size: 11, weight: .semibold))
                        .padding(.horizontal, 7)
                        .padding(.vertical, 2)
                        .background(.white.opacity(0.14), in: RoundedRectangle(cornerRadius: 7, style: .continuous))
                }
                Text("Session \(TimeInterval(stats.elapsedSeconds).playTimeClock)")
                    .font(.system(size: 11))
                    .foregroundStyle(.white.opacity(0.7))
                    .monospacedDigit()
            }
            .lineLimit(1)
        }
        .frame(height: 36)
    }

    // MARK: Cards

    private var cards: some View {
        let cards = [frameCard, renderCard, memoryCard, cpuCard].compactMap { $0 }
        let rows = stride(from: 0, to: cards.count, by: 2).map { Array(cards[$0..<min($0 + 2, cards.count)]) }
        return Grid(horizontalSpacing: 8, verticalSpacing: 8) {
            ForEach(rows.indices, id: \.self) { index in
                GridRow {
                    ForEach(rows[index]) { card in
                        HUDCardView(card: card)
                    }
                    if rows[index].count == 1 {
                        Color.clear.frame(height: 0)
                    }
                }
            }
        }
    }

    private var frameCard: HUDCard? {
        var rows: [HUDRow] = []
        if stats.cpuFrameTimeMilliseconds > 0 || stats.maxCpuFrameTimeMilliseconds > 0 {
            rows.append(HUDRow(
                id: "main", label: Text("Main thread"),
                value: HUDFormat.milliseconds(stats.cpuFrameTimeMilliseconds),
                share: stats.cpuFrameTimeMilliseconds
            ))
            rows.append(HUDRow(
                id: "peak", label: Text("Peak"),
                value: HUDFormat.milliseconds(stats.maxCpuFrameTimeMilliseconds),
                valueColor: stats.maxCpuFrameTimeMilliseconds > 17.5
                    ? HUDPalette.frameTime(stats.maxCpuFrameTimeMilliseconds) : .white
            ))
        }
        if stats.gpuSubmissionTimeMilliseconds >= 0 {
            rows.append(HUDRow(
                id: "gpu", label: Text("GPU submit"),
                value: HUDFormat.milliseconds(stats.gpuSubmissionTimeMilliseconds),
                share: stats.gpuSubmissionTimeMilliseconds
            ))
        }
        if stats.presentationWaitTimeMilliseconds >= 0 {
            rows.append(HUDRow(
                id: "present", label: Text("Present wait"),
                value: HUDFormat.milliseconds(stats.presentationWaitTimeMilliseconds),
                share: stats.presentationWaitTimeMilliseconds
            ))
        }
        return rows.isEmpty ? nil : HUDCard(id: "frame", title: Text("Frame time"), rows: rows)
    }

    private var renderCard: HUDCard {
        let drawable = "\(stats.drawableWidth)×\(stats.drawableHeight)"
        var rows: [HUDRow] = []
        if stats.showsLayerCompositing {
            rows.append(HUDRow(id: "drawable", label: Text("Drawable"), value: "\(drawable) · \(stats.renderer)"))
            rows.append(HUDRow(
                id: "layers", label: Text("Layers"),
                value: stats.gpuLayerComposition ? "GPU Metal" : String(localized: "Software")
            ))
            if stats.gpuLayerComposition {
                rows.append(HUDRow(
                    id: "ops", label: Text("Ops / CPU fallbacks"),
                    value: "\(stats.gpuLayerOperations) / \(stats.layerCPUFallbacks)"
                ))
            }
        } else {
            rows.append(HUDRow(id: "drawable", label: Text("Drawable"), value: drawable))
            rows.append(HUDRow(id: "renderer", label: Text("Renderer"), value: stats.renderer))
        }
        if stats.drawCallsPerFrame >= 0 {
            rows.append(HUDRow(
                id: "dc", label: Text("Draw calls"),
                value: String(format: String(localized: "%.0f /frame"), stats.drawCallsPerFrame)
            ))
        }
        return HUDCard(id: "render", title: Text("Rendering"), rows: rows)
    }

    private var memoryCard: HUDCard {
        var rows = [
            HUDRow(id: "ram", label: Text("Memory"), value: HUDFormat.bytes(stats.residentMemoryBytes)),
            HUDRow(id: "gpu", label: Text("GPU memory"), value: HUDFormat.bytes(stats.gpuMemoryBytes))
        ]
        let transfers = stats.showsLayerCompositing && stats.gpuLayerComposition
        if transfers {
            rows.append(HUDRow(id: "upload", label: Text("Upload"), value: HUDFormat.bytes(stats.layerUploadedBytes)))
            rows.append(HUDRow(id: "readback", label: Text("Readback"), value: HUDFormat.bytes(stats.layerReadbackBytes)))
        }
        return HUDCard(id: "memory", title: transfers ? Text("Memory & transfer") : Text("Memory"), rows: rows)
    }

    private var cpuCard: HUDCard? {
        guard stats.processCPUPercent >= 0 else { return nil }
        var rows = [HUDRow(
            id: "all", label: Text("All threads"),
            value: HUDFormat.percent(stats.processCPUPercent),
            // A bar of one core's time.
            share: min(stats.processCPUPercent, 100) / 100 * HUDRow.frameBudget
        )]
        for group in stats.busiestThreads {
            let name = group.name == "unnamed" ? String(localized: "unnamed") : group.name
            let count = group.threads > 1 ? " ×\(group.threads)" : ""
            rows.append(HUDRow(
                id: "thread-\(group.name)",
                label: Text(verbatim: name + count),
                value: HUDFormat.percent(group.percent)
            ))
        }
        return HUDCard(id: "cpu", title: Text(verbatim: "CPU"), rows: rows)
    }
}

private struct HUDCard: Identifiable {
    let id: String
    let title: Text
    let rows: [HUDRow]
}

private struct HUDRow: Identifiable {
    /// A 60 FPS frame; bars show each time's share of it.
    static let frameBudget = 1000.0 / 60

    let id: String
    let label: Text
    let value: String
    var valueColor: Color = .white
    /// Milliseconds drawn as a bar under the row; nil for no bar.
    var share: Double?
}

private struct HUDCardView: View {
    let card: HUDCard

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            card.title
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(.white.opacity(0.6))
            ForEach(card.rows) { row in
                VStack(alignment: .leading, spacing: 2) {
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        row.label
                            .font(.system(size: 12))
                            .foregroundStyle(.white.opacity(0.7))
                        Spacer(minLength: 0)
                        Text(verbatim: row.value)
                            .font(.system(size: 12, weight: .semibold, design: .monospaced))
                            .foregroundStyle(row.valueColor)
                    }
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
                    if let share = row.share {
                        ShareBar(fraction: share / HUDRow.frameBudget)
                    }
                }
                .accessibilityElement(children: .combine)
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }
}

private struct ShareBar: View {
    let fraction: Double

    var body: some View {
        let clamped = min(max(fraction, 0), 1)
        Capsule()
            .fill(.white.opacity(0.14))
            .frame(height: 3)
            .overlay(alignment: .leading) {
                GeometryReader { proxy in
                    Capsule()
                        .fill(clamped > 0.9 ? HUDPalette.amber : HUDPalette.green)
                        .frame(width: proxy.size.width * clamped)
                }
            }
            .accessibilityHidden(true)
    }
}

/// One bar per second of play, the frame interval averaged over it, with
/// guides at 60 and 30 FPS. Engines report frame times per second rather
/// than per frame, so a single long frame shows under Peak instead.
struct FrameTimeGraph: View {
    static let slots = 40
    private static let ceiling = 40.0

    let history: [Double?]

    var body: some View {
        Canvas { context, size in
            let plot = CGRect(x: 6, y: 0, width: size.width - 32, height: size.height)
            let slot = (plot.width + 2) / CGFloat(Self.slots)
            let start = Self.slots - history.count
            for (index, value) in history.enumerated() {
                let height = value.map { max(2, (CGFloat(min($0, Self.ceiling) / Self.ceiling) * plot.height).rounded()) } ?? 2
                let bar = CGRect(
                    x: plot.minX + CGFloat(start + index) * slot,
                    y: plot.maxY - height,
                    width: max(1, slot - 2),
                    height: height
                )
                let color = value.map(HUDPalette.frameTime) ?? HUDPalette.idle.opacity(0.5)
                context.fill(Path(roundedRect: bar, cornerSize: CGSize(width: 1.5, height: 1.5)), with: .color(color))
            }
            let guides: [(milliseconds: Double, label: String, line: Color, text: Color)] = [
                (1000 / 60, "60", .white.opacity(0.3), .white.opacity(0.55)),
                (1000 / 30, "30", HUDPalette.amber.opacity(0.45), HUDPalette.amber.opacity(0.8))
            ]
            for guide in guides {
                let y = plot.maxY - CGFloat(guide.milliseconds / Self.ceiling) * plot.height
                var line = Path()
                line.move(to: CGPoint(x: plot.minX, y: y))
                line.addLine(to: CGPoint(x: plot.maxX, y: y))
                context.stroke(line, with: .color(guide.line), style: StrokeStyle(lineWidth: 1, dash: [3, 3]))
                context.draw(
                    Text(verbatim: guide.label)
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundColor(guide.text),
                    at: CGPoint(x: size.width - 5, y: y),
                    anchor: .trailing
                )
            }
        }
        .frame(height: 44)
        .background(.white.opacity(0.05), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        .accessibilityElement()
        .accessibilityLabel(Text("Frame times for the last 40 seconds"))
    }
}

// MARK: Tucked

/// What stays at the screen edge: the frame rate and its color. Tap or
/// pull it out to bring the panel back.
private struct HUDTab: View {
    let stats: KRKRPerformanceSnapshot
    let detail: PerformanceDetail
    let onLeadingEdge: Bool
    let untuck: () -> Void

    var body: some View {
        let full = detail == .full
        let size = PerformanceHUDView.tabSize(detail)
        Button(action: untuck) {
            VStack(spacing: full ? 6 : 5) {
                if full { chevron(size: 14, opacity: 1) }
                Circle()
                    .fill(HUDPalette.rate(stats))
                    .frame(width: 8, height: 8)
                Text(verbatim: stats.isIdle ? "—" : String(format: "%.0f", stats.framesPerSecond))
                    .font(.system(size: full ? 15 : 14, weight: .bold, design: .monospaced))
                    .lineLimit(1)
                    .minimumScaleFactor(0.6)
                if full {
                    Text(verbatim: "FPS")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(.white.opacity(0.72))
                        .padding(.top, -4)
                } else {
                    chevron(size: 12, opacity: 0.7)
                }
            }
            .foregroundStyle(.white)
            .frame(width: size.width, height: size.height)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .modifier(HUDGlass(shape: EdgeTabShape(roundsRight: onLeadingEdge, radius: size.width / 2)))
        .accessibilityLabel(Text("Show Performance Stats"))
        .accessibilityValue(stats.isIdle ? Text("Idle") : Text(verbatim: String(format: "%.0f FPS", stats.framesPerSecond)))
        .accessibilityIdentifier("krkr-performance-tab")
    }

    private func chevron(size: CGFloat, opacity: Double) -> some View {
        Image(systemName: onLeadingEdge ? "chevron.right" : "chevron.left")
            .font(.system(size: size, weight: .bold))
            .foregroundStyle(.white.opacity(opacity))
            .accessibilityHidden(true)
    }
}

// MARK: Hosting

/// Full-screen layer that holds only the HUD: touches anywhere else reach
/// the game. Drag the HUD and it settles in the nearest corner; drag it past
/// the left or right edge and it tucks away.
@MainActor
final class PerformanceHUDContainer: UIView {
    private static let margin: CGFloat = 12

    private let model: KRKROverlayModel
    private let hosting: UIHostingController<PerformanceHUDView>
    private var dragStart: CGPoint?

    init(model: KRKROverlayModel) {
        self.model = model
        hosting = UIHostingController(rootView: PerformanceHUDView(model: model))
        super.init(frame: .zero)
        backgroundColor = .clear
        hosting.view.backgroundColor = .clear
        hosting.view.isOpaque = false
        addSubview(hosting.view)
        hosting.view.addGestureRecognizer(
            UIPanGestureRecognizer(target: self, action: #selector(drag(_:)))
        )
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func attach(to parent: UIViewController) {
        parent.addChild(hosting)
        frame = parent.view.bounds
        autoresizingMask = [.flexibleWidth, .flexibleHeight]
        parent.view.addSubview(self)
        hosting.didMove(toParent: parent)
    }

    func detach() {
        hosting.willMove(toParent: nil)
        removeFromSuperview()
        hosting.removeFromParent()
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        let hit = super.hitTest(point, with: event)
        return hit === self ? nil : hit
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let safe = bounds.inset(by: safeAreaInsets)
        let width = min(400, max(240, safe.width - 2 * Self.margin))
        if model.hudWidth != width {
            model.hudWidth = width
        }
        guard dragStart == nil else { return }
        hosting.view.frame = restingFrame(model.hudPlacement)
    }

    func setTucked(_ tucked: Bool) {
        var placement = model.hudPlacement
        placement.tucked = tucked
        settle(at: placement)
    }

    private func restingFrame(_ placement: PerformanceHUDPlacement) -> CGRect {
        let safe = bounds.inset(by: safeAreaInsets)
        let size = placement.tucked
            ? PerformanceHUDView.tabSize(model.performanceDetail)
            : hosting.sizeThatFits(in: CGSize(width: model.hudWidth, height: safe.height))
        let x: CGFloat
        if placement.tucked {
            x = placement.leading ? safe.minX : safe.maxX - size.width
        } else {
            x = placement.leading ? safe.minX + Self.margin : safe.maxX - Self.margin - size.width
        }
        let y = placement.top ? safe.minY + Self.margin : safe.maxY - Self.margin - size.height
        return CGRect(x: x.rounded(), y: y.rounded(), width: size.width, height: size.height)
    }

    @objc private func drag(_ recognizer: UIPanGestureRecognizer) {
        guard let view = recognizer.view else { return }
        let translation = recognizer.translation(in: self)
        switch recognizer.state {
        case .began:
            dragStart = view.center
        case .changed:
            guard let start = dragStart else { return }
            view.center = CGPoint(x: start.x + translation.x, y: start.y + translation.y)
        case .ended, .cancelled, .failed:
            guard dragStart != nil else { return }
            dragStart = nil
            settle(after: view.frame, translation: translation, velocity: recognizer.velocity(in: self))
        default:
            break
        }
    }

    private func settle(after frame: CGRect, translation: CGPoint, velocity: CGPoint) {
        let safe = bounds.inset(by: safeAreaInsets)
        // Where a flick would carry it.
        let predicted = CGPoint(x: frame.midX + velocity.x * 0.15, y: frame.midY + velocity.y * 0.15)
        var placement = model.hudPlacement
        placement.top = predicted.y < safe.midY
        if placement.tucked {
            let outward = placement.leading ? translation.x : -translation.x
            let flungOut = placement.leading ? velocity.x > 500 : velocity.x < -500
            if outward > 24 || flungOut {
                placement.tucked = false
            }
        } else {
            let threshold = frame.width * 0.35
            if safe.minX - frame.minX > threshold || (predicted.x < safe.minX && velocity.x < -600) {
                placement.tucked = true
                placement.leading = true
            } else if frame.maxX - safe.maxX > threshold || (predicted.x > safe.maxX && velocity.x > 600) {
                placement.tucked = true
                placement.leading = false
            } else {
                placement.leading = predicted.x < safe.midX
            }
        }
        settle(at: placement)
    }

    private func settle(at placement: PerformanceHUDPlacement) {
        let tuckChanged = placement.tucked != model.hudPlacement.tucked
        model.hudPlacement = placement
        placement.save(for: model.performanceDetail)
        AppDiagnostics.shared.event("overlay", "performanceHUD.placed", [
            "leading": String(placement.leading), "top": String(placement.top), "tucked": String(placement.tucked)
        ])
        UIView.animate(
            withDuration: 0.4,
            delay: 0,
            usingSpringWithDamping: 0.86,
            initialSpringVelocity: 0,
            options: [.allowUserInteraction, .beginFromCurrentState]
        ) {
            self.hosting.view.frame = self.restingFrame(placement)
        }
        if tuckChanged {
            // The panel's size settles once SwiftUI has swapped in the new content.
            DispatchQueue.main.async { [weak self] in self?.setNeedsLayout() }
        }
    }
}
