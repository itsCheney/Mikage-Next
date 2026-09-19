import SwiftUI
import VNCore

struct PlayerView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase

    let game: GameRecord
    @State private var elapsed = 0
    @State private var engineStarted = false
    @State private var engineStarting = false
    @State private var engineStopping = false
    @State private var backdrop: UIImage?
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            backdropLayer
            PlayerHostAttachment { controller in
                startEngine(in: controller)
            }
            .frame(width: 1, height: 1)
            .allowsHitTesting(false)

            if engineStarting || engineStopping {
                ProgressView(engineStopping ? "正在结束游戏…" : "正在准备横屏…")
                    .tint(.white)
                    .foregroundStyle(.white)
            }
        }
        .ignoresSafeArea()
        .statusBarHidden()
        .task { await loadBackdrop() }
        .onDisappear {
            AppDiagnostics.shared.event("player", "view.disappeared")
            model.krkrSession.requestStop()
        }
        .onChange(of: scenePhase) { phase in
            AppDiagnostics.shared.event("player", "scenePhase.changed", ["phase": String(describing: phase)])
            model.krkrSession.setForeground(phase == .active)
        }
        .onReceive(timer) { _ in
            if scenePhase == .active && engineStarted {
                elapsed += 1
            }
        }
    }

    /// Fills the letterbox during the launch/exit transitions, before and after
    /// the engine window covers this view. Falls back to black when the game has
    /// no cover, when the file cannot be decoded, or when the user chose black.
    @ViewBuilder
    private var backdropLayer: some View {
        Color.black.ignoresSafeArea()
        if let backdrop {
            Image(uiImage: backdrop)
                .resizable()
                .scaledToFill()
                .ignoresSafeArea()
                .overlay(.black.opacity(0.45))
                .accessibilityHidden(true)
                .transition(.opacity)
        }
    }

    private func loadBackdrop() async {
        guard model.settings.background == .cover,
              let url = model.coverURL(game) else { return }
        // Decoding a full-size cover blocks; keep it off the main thread while
        // the engine is starting up.
        let image = await Task.detached(priority: .userInitiated) {
            UIImage(contentsOfFile: url.path)
        }.value
        guard let image, !Task.isCancelled else { return }
        withAnimation(.easeOut(duration: 0.25)) { backdrop = image }
    }

    private func startEngine(in controller: UIViewController) {
        guard !engineStarted, !engineStarting else { return }
        engineStarting = true
        Task { @MainActor in
            do {
                let configuration = try model.launchConfiguration(for: game)
                model.krkrSession.onWarning = { error in
                    AppDiagnostics.shared.event("player", "runtime.warning", ["error": error.localizedDescription])
                    model.alert = error.localizedDescription
                }
                model.krkrSession.onReturningToLibrary = {
                    withAnimation(.easeInOut(duration: 0.2)) {
                        engineStopping = true
                    }
                }
                model.krkrSession.onFinished = { result in
                    model.recordPlayback(of: game, duration: TimeInterval(elapsed))
                    if case .failure(let error) = result {
                        model.alert = error.localizedDescription
                    }
                    dismiss()
                }
                try await model.krkrSession.start(
                    configuration: configuration,
                    in: controller
                )
                engineStarted = true
                engineStarting = false
            } catch {
                AppDiagnostics.shared.event("player", "startup.error", ["error": error.localizedDescription])
                AppDiagnostics.shared.endGame()
                engineStarting = false
                model.alert = error.localizedDescription
                dismiss()
            }
        }
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
        required init?(coder: NSCoder) {
            fatalError("init(coder:) has not been implemented")
        }

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
