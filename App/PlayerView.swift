import SwiftUI
import VNCore

struct PlayerView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase

    let game: GameRecord
    @State private var menu = false
    @State private var confirmExit = false
    @State private var elapsed = 0
    @State private var message: String?
    @State private var capture: UIImage?
    @State private var menuSnapshot: UIImage?
    @State private var share = false
    @State private var engineStarted = false
    @State private var engineStarting = false
    @State private var engineStopping = false
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                gameBackdrop
                PlayerHostAttachment { controller in
                    startEngine(in: controller)
                }
                .frame(width: 1, height: 1)
                .allowsHitTesting(false)

                if model.settings.performance {
                    Text("KRKRSDL3\n本次游玩 \(duration)")
                        .font(.system(size: 12, design: .monospaced))
                        .foregroundStyle(.white.opacity(0.8))
                        .padding(9)
                        .background(.black.opacity(0.6), in: RoundedRectangle(cornerRadius: 10))
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                        .padding(18)
                }

                if menu {
                    Color.black.opacity(0.40).ignoresSafeArea()
                    menuPanel(compact: proxy.size.width < 600)
                        .padding(proxy.size.width < 600 ? 20 : 50)
                        .frame(maxWidth: 640)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(.black)
        }
        .ignoresSafeArea()
        .statusBarHidden()
        .onDisappear {
            model.krkrSession.requestStop()
        }
        .onChange(of: scenePhase) { phase in
            model.krkrSession.setForeground(phase == .active)
        }
        .onReceive(timer) { _ in
            if scenePhase == .active && engineStarted { elapsed += 1 }
        }
        .confirmationDialog(
            "结束游戏并返回游戏库？",
            isPresented: $confirmExit,
            titleVisibility: .visible
        ) {
            Button("返回游戏库", role: .destructive) {
                engineStopping = true
                model.krkrSession.requestStop()
            }
            Button("继续游戏", role: .cancel) { }
        }
        .alert("Mikage", isPresented: Binding(
            get: { message != nil },
            set: { if !$0 { message = nil } }
        )) {
            Button("好", role: .cancel) { message = nil }
        } message: {
            Text(message ?? "")
        }
        .sheet(isPresented: $share) {
            if let capture { ActivitySheet(items: [capture]) }
        }
    }

    @ViewBuilder
    private var gameBackdrop: some View {
        if let menuSnapshot {
            Image(uiImage: menuSnapshot)
                .resizable()
                .scaledToFill()
                .ignoresSafeArea()
        } else {
            Color.black
            if engineStarting || engineStopping {
                ProgressView(engineStopping ? "正在结束游戏…" : "正在启动 KRKR…")
                    .tint(.white)
                    .foregroundStyle(.white)
            }
        }
    }

    private func menuPanel(compact: Bool) -> some View {
        VStack(spacing: 20) {
            HStack(spacing: 10) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(game.title).font(.headline).lineLimit(1)
                    Text("本次已游玩 \(duration)").font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
                RoundButton(symbol: "xmark", label: "关闭菜单") {
                    closeMenu()
                }
            }
            LazyVGrid(
                columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: 2),
                spacing: 12
            ) {
                action("截图", "camera") { screenshot() }
                action("退出", "rectangle.portrait.and.arrow.right", destructive: true) {
                    confirmExit = true
                }
            }
        }
        .padding(compact ? 18 : 22)
        .nativeGlassPanel(30)
        .foregroundStyle(.white)
    }

    private var duration: String {
        String(format: "%d:%02d:%02d", elapsed / 3600, elapsed / 60 % 60, elapsed % 60)
    }

    private func action(
        _ title: String,
        _ icon: String,
        destructive: Bool = false,
        perform: @escaping () -> Void
    ) -> some View {
        Button(role: destructive ? .destructive : nil, action: perform) {
            VStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.title2.weight(.medium))
                    .symbolRenderingMode(.hierarchical)
                Text(title).font(.body.weight(.medium))
            }
            .frame(maxWidth: .infinity)
            .frame(height: 72)
        }
        .nativeGlassButtonStyle()
    }

    private func startEngine(in controller: UIViewController) {
        guard !engineStarted, !engineStarting else { return }
        engineStarting = true
        do {
            let configuration = try model.launchConfiguration(for: game)
            model.krkrSession.onMenuRequested = {
                menuSnapshot = model.krkrSession.snapshot()
                withAnimation { menu = true }
            }
            model.krkrSession.onFinished = { result in
                model.recordPlayback(of: game, duration: TimeInterval(elapsed))
                if case .failure(let error) = result {
                    model.alert = error.localizedDescription
                }
                dismiss()
            }
            try model.krkrSession.start(configuration: configuration, in: controller)
            engineStarted = true
            engineStarting = false
        } catch {
            engineStarting = false
            model.alert = error.localizedDescription
            dismiss()
        }
    }

    private func closeMenu() {
        withAnimation { menu = false }
        model.krkrSession.hideMenuOverlay()
        menuSnapshot = nil
    }

    private func screenshot() {
        guard let image = model.krkrSession.snapshot() ?? menuSnapshot else {
            message = "截图生成失败。"
            return
        }
        capture = image
        share = true
    }
}

private struct PlayerHostAttachment: UIViewControllerRepresentable {
    let onReady: (UIViewController) -> Void

    func makeUIViewController(context: Context) -> AttachmentController {
        AttachmentController(onReady: onReady)
    }

    func updateUIViewController(_ uiViewController: AttachmentController, context: Context) { }

    final class AttachmentController: UIViewController {
        let onReady: (UIViewController) -> Void
        private var delivered = false

        init(onReady: @escaping (UIViewController) -> Void) {
            self.onReady = onReady
            super.init(nibName: nil, bundle: nil)
            view.backgroundColor = .clear
        }

        @available(*, unavailable)
        required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

        override func viewDidAppear(_ animated: Bool) {
            super.viewDidAppear(animated)
            guard !delivered else { return }
            delivered = true
            onReady(self)
        }
    }
}

struct ActivitySheet: UIViewControllerRepresentable {
    var items: [Any]

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }

    func updateUIViewController(_ uiViewController: UIActivityViewController, context: Context) { }
}
