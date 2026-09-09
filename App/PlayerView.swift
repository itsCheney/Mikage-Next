import SwiftUI
import VNCore

struct PlayerView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    let game: GameRecord
    @State private var menu = true
    @State private var autoAdvance = false
    @State private var mouse = false
    @State private var confirmExit = false
    @State private var elapsed = 0
    @State private var message: String?
    @State private var capture: UIImage?
    @State private var share = false
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                if model.settings.background == "游戏封面" { SkyArtwork().blur(radius: 35).opacity(0.35) }
                else { Color.black }
                demoScene.frame(maxWidth: proxy.size.width, maxHeight: proxy.size.height)
                ThreeFingerGesture(enabled: model.settings.threeFingerMenu) { menu = true }.allowsHitTesting(false)
                if mouse { Image(systemName: "cursorarrow").font(.system(size: 30)).foregroundStyle(.white).shadow(radius: 3).offset(x: 80, y: 35) }
                if model.settings.performance {
                    Text("界面演示 · 未连接引擎\nFPS / GPU / 内存：暂无数据")
                        .font(.system(size: 12, design: .monospaced)).foregroundStyle(.white.opacity(0.75))
                        .padding(9).background(.black.opacity(0.6), in: RoundedRectangle(cornerRadius: 10))
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading).padding(18)
                }
                if !menu {
                    Button { withAnimation { menu = true } } label: {
                        Label(
                            "唤出游戏菜单",
                            systemImage: model.settings.floatingButton ? "pawprint.fill" : "line.3.horizontal"
                        )
                        .labelStyle(.iconOnly)
                        .font(.title3.weight(.medium))
                        .frame(width: 44, height: 44)
                    }
                    .nativeGlassButtonStyle()
                    .opacity(model.settings.floatingButton ? model.settings.idleOpacity : 0.8)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing).padding(20)
                        .accessibilityLabel("唤出游戏菜单")
                }
                if menu {
                    Color.black.opacity(0.40).ignoresSafeArea()
                    menuPanel(compact: proxy.size.width < 600).padding(proxy.size.width < 600 ? 20 : 50)
                        .frame(maxWidth: 950)
                }
            }.frame(maxWidth: .infinity, maxHeight: .infinity).background(.black)
        }.ignoresSafeArea().statusBarHidden()
            .onReceive(timer) { _ in if scenePhase == .active { elapsed += 1 } }
            .confirmationDialog("返回游戏库？演示不会产生游戏存档。", isPresented: $confirmExit, titleVisibility: .visible) {
                Button("返回游戏库", role: .destructive) { dismiss() }
                Button("继续演示", role: .cancel) { }
            }
            .alert("界面演示", isPresented: Binding(get: { message != nil }, set: { if !$0 { message = nil } })) {
                Button("好", role: .cancel) { message = nil }
            } message: { Text(message ?? "") }
            .sheet(isPresented: $share) { if let capture { ActivitySheet(items: [capture]) } }
    }

    private var demoScene: some View {
        ZStack {
            SkyArtwork()
            HStack {
                VStack(alignment: .leading, spacing: 20) {
                    Text("青空下的加缪").font(.system(size: 38, weight: .light, design: .serif))
                    Text("Camus In The Blue Sky").font(.system(size: 12)).tracking(4)
                    Spacer()
                    Text(autoAdvance ? "自动模式 · 演示" : "界面演示").font(.title3)
                    Text("这是一张菜单预览，尚未运行游戏。")
                        .font(.footnote).foregroundStyle(.secondary)
                }.padding(40)
                Spacer()
            }.background(LinearGradient(colors: [.white.opacity(0.96), .white.opacity(0.76), .clear], startPoint: .leading, endPoint: .trailing))
                .foregroundStyle(Color(red: 0.18, green: 0.36, blue: 0.40))
        }.aspectRatio(16 / 9, contentMode: .fit).clipped()
    }
    private func menuPanel(compact: Bool) -> some View {
        VStack(spacing: 20) {
            HStack(spacing: 10) {
                Text(game.title).font(.system(size: 17, weight: .semibold)).lineLimit(1)
                if !compact {
                    Text("本次已游玩 \(duration)").font(.system(size: 15)).foregroundStyle(.secondary)
                        .padding(.horizontal, 12).padding(.vertical, 6).background(.white.opacity(0.06), in: Capsule())
                }
                Spacer(minLength: 0)
                RoundButton(symbol: "xmark", label: "关闭菜单") { withAnimation { menu = false } }
            }
            if compact { Text("本次演示 \(duration)").font(.caption).foregroundStyle(.secondary) }
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: compact ? 3 : 5), spacing: 12) {
                action("菜单", "list.bullet") { message = "真实游戏菜单需要由引擎提供；此处暂时演示菜单布局。" }
                action("自动", autoAdvance ? "pause.circle" : "play.circle", selected: autoAdvance) { autoAdvance.toggle() }
                action("鼠标", "cursorarrow.motionlines", selected: mouse) { mouse.toggle() }
                action("截图", "camera") { screenshot() }
                action("退出", "rectangle.portrait.and.arrow.right", destructive: true) { confirmExit = true }
            }
        }.padding(compact ? 18 : 22).nativeGlassPanel(30).foregroundStyle(.white)
    }
    private var duration: String { String(format: "%d:%02d:%02d", elapsed / 3600, elapsed / 60 % 60, elapsed % 60) }
    private func action(_ title: String, _ icon: String, selected: Bool = false, destructive: Bool = false, perform: @escaping () -> Void) -> some View {
        Button(role: destructive ? .destructive : nil, action: perform) {
            VStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.title2.weight(.medium))
                    .symbolRenderingMode(.hierarchical)
                Text(title).font(.body.weight(.medium))
            }
            .frame(maxWidth: .infinity)
            .frame(height: 82)
        }
        .nativeGlassButtonStyle(prominent: selected)
    }

    private func screenshot() {
        let renderer = ImageRenderer(content: demoScene.frame(width: 1280, height: 720))
        renderer.scale = 1
        guard let image = renderer.uiImage else { message = "截图生成失败。"; return }
        capture = image; share = true
    }
}

private struct ThreeFingerGesture: UIViewRepresentable {
    let enabled: Bool
    let action: () -> Void
    func makeCoordinator() -> Coordinator { Coordinator(action: action) }
    func makeUIView(context: Context) -> AttachmentView {
        let view = AttachmentView()
        view.onWindow = { [weak coordinator = context.coordinator] window in coordinator?.attach(to: window) }
        context.coordinator.recognizer.isEnabled = enabled
        return view
    }
    func updateUIView(_ uiView: AttachmentView, context: Context) { context.coordinator.recognizer.isEnabled = enabled }
    static func dismantleUIView(_ uiView: AttachmentView, coordinator: Coordinator) { coordinator.attach(to: nil) }
    class AttachmentView: UIView {
        var onWindow: ((UIWindow?) -> Void)?
        override func didMoveToWindow() { super.didMoveToWindow(); onWindow?(window) }
    }
    class Coordinator: NSObject, UIGestureRecognizerDelegate {
        let action: () -> Void
        let recognizer = UITapGestureRecognizer()
        init(action: @escaping () -> Void) {
            self.action = action; super.init()
            recognizer.numberOfTouchesRequired = 3
            recognizer.cancelsTouchesInView = false
            recognizer.delegate = self
            recognizer.addTarget(self, action: #selector(invoke))
        }
        func attach(to window: UIWindow?) { recognizer.view?.removeGestureRecognizer(recognizer); window?.addGestureRecognizer(recognizer) }
        @objc func invoke() { action() }
        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool { true }
    }
}

struct ActivitySheet: UIViewControllerRepresentable {
    var items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: items, applicationActivities: nil) }
    func updateUIViewController(_ uiViewController: UIActivityViewController, context: Context) { }
}
