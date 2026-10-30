import SwiftUI
import VNCore

/// Shown once on first launch, and again from Settings: what Mikage does,
/// signing in to Bangumi and VNDB, and the floating game controls.
struct WelcomeGuideView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var page = Page.welcome
    @State private var tokenService: MetadataService?
    @State private var showingBangumiSignIn = false

    /// Set once the guide has been seen, so it doesn't open again by itself.
    static let seenKey = "welcomeGuide.seen"

    enum Page {
        case welcome
        case accounts
        case controls
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollView {
                Group {
                    switch page {
                    case .welcome: welcome
                    case .accounts: accounts
                    case .controls: controls
                    }
                }
                .padding(.horizontal, 28)
                .padding(.top, 44)
                .padding(.bottom, 20)
                .frame(maxWidth: 520)
                .frame(maxWidth: .infinity)
            }
            footer
                .padding(.horizontal, 28)
                .padding(.bottom, 20)
                .frame(maxWidth: 520)
        }
        .background(AppBackground())
        .animation(.easeInOut(duration: 0.25), value: page)
        .interactiveDismissDisabled(page != .controls)
        .sheet(item: $tokenService) { service in
            TokenEntryView(service: service).environmentObject(model)
        }
        .sheet(isPresented: $showingBangumiSignIn) {
            BangumiSignInView {
                try await model.connectBangumiWithWebSession()
            }
        }
    }

    // MARK: Pages

    private var welcome: some View {
        VStack(alignment: .leading, spacing: 34) {
            (Text("Welcome to") + Text(verbatim: "\nMikage").foregroundColor(.accentColor))
                .font(.largeTitle.weight(.bold))
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
            VStack(alignment: .leading, spacing: 26) {
                feature("books.vertical.fill", .indigo,
                        Text("Eight Engines, One Library"),
                        Text("KiriKiri, ONScripter, Artemis, Ren’Py, Siglus, FVP, BGI and YU-RIS games run natively."))
                feature("folder.fill.badge.plus", .blue,
                        Text("Drop In Game Folders"),
                        Text("Copy a game’s folder into Mikage in the Files app. It shows up the next time you open the app."))
                feature("checkmark.magnifyingglass", .orange,
                        Text("Matched with Bangumi & VNDB"),
                        Text("Covers, studio and synopsis are filled in. You confirm each match."))
                feature("circle.grid.cross.fill", .green,
                        Text("Controls That Float"),
                        Text("Skip, Auto, Save and the backlog are one tap away in every game."))
            }
        }
        .transition(.opacity)
    }

    private var accounts: some View {
        VStack(spacing: 18) {
            Image(systemName: "arrow.triangle.2.circlepath")
                .font(.system(size: 50, weight: .regular))
                .foregroundStyle(.tint)
                .accessibilityHidden(true)
            Text("Sync Your Lists")
                .font(.largeTitle.weight(.bold))
                .multilineTextAlignment(.center)
            Text("Sign in to keep play status, ratings and finish dates in sync with your accounts.")
                .font(.body)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            VStack(spacing: 0) {
                accountRow(.bangumi)
                Divider().padding(.leading, 66)
                accountRow(.vndb)
            }
            .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .padding(.top, 8)
            Text("Matching works without an account. You can sign in later in Settings › Accounts & Sync.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .transition(.opacity)
    }

    private var controls: some View {
        VStack(spacing: 18) {
            Text("Game Controls")
                .font(.largeTitle.weight(.bold))
            Text("Floating buttons sit in the black bars beside the game.")
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            FloatingControlsMap(
                showsMenuButton: true,
                actions: model.settings.quickActions,
                showsStats: true,
                numbered: true
            )
            .padding(.vertical, 6)
            VStack(alignment: .leading, spacing: 18) {
                numbered(1, Text("Quick Actions"),
                         Text("One tap to Skip, go Back and more. Pick two in Settings › Floating Button."))
                numbered(2, Text("Menu Button"),
                         Text("Backlog, saves, Auto, Skip, Mouse mode and Quit. Drag it anywhere."))
                numbered(3, Text("Stats"),
                         Text("Frame rate at a glance. Tap it for more, or turn it off in the menu."))
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .transition(.opacity)
    }

    // MARK: Parts

    @ViewBuilder
    private var footer: some View {
        VStack(spacing: 12) {
            if page == .welcome {
                Text("Mikage never changes your game files.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            Button {
                switch page {
                case .welcome: page = .accounts
                case .accounts: page = .controls
                case .controls: finish()
                }
            } label: {
                Text(buttonTitle)
                    .font(.body.weight(.semibold))
                    .frame(maxWidth: .infinity)
                    .frame(height: 50)
            }
            .buttonStyle(.borderedProminent)
            .buttonBorderShape(.roundedRectangle(radius: 14))
            .accessibilityIdentifier("welcome-guide-next")
        }
    }

    private var buttonTitle: LocalizedStringKey {
        switch page {
        case .welcome: return "Continue"
        case .accounts: return model.accounts.isEmpty ? "Not Now" : "Continue"
        case .controls: return "Start Playing"
        }
    }

    private func finish() {
        UserDefaults.standard.set(true, forKey: Self.seenKey)
        dismiss()
    }

    private func feature(_ symbol: String, _ color: Color, _ title: Text, _ detail: Text) -> some View {
        HStack(alignment: .top, spacing: 16) {
            Image(systemName: symbol)
                .font(.system(size: 30))
                .foregroundStyle(color)
                .frame(width: 44)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 3) {
                title.font(.headline)
                detail.font(.subheadline).foregroundStyle(.secondary)
            }
        }
        .accessibilityElement(children: .combine)
    }

    private func numbered(_ number: Int, _ title: Text, _ detail: Text) -> some View {
        HStack(alignment: .top, spacing: 14) {
            Text(verbatim: "\(number)")
                .font(.subheadline.weight(.bold))
                .foregroundStyle(.black)
                .frame(width: 26, height: 26)
                .background(Circle().fill(Color.yellow))
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 3) {
                title.font(.headline)
                detail.font(.subheadline).foregroundStyle(.secondary)
            }
        }
        .accessibilityElement(children: .combine)
    }

    private func accountRow(_ service: MetadataService) -> some View {
        let account = model.accounts[service]
        return HStack(spacing: 12) {
            ServiceAccountIcon(service: service, account: account)
            VStack(alignment: .leading, spacing: 2) {
                Text(service.displayName)
                    .font(.body.weight(.semibold))
                if let account {
                    Text("@\(account.username) · Connected")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                } else {
                    Text("Not Connected")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            Spacer()
            if account != nil {
                Image(systemName: "checkmark")
                    .font(.body.weight(.bold))
                    .foregroundStyle(.green)
                    .accessibilityLabel(Text("Signed In"))
            } else {
                Button("Sign In") {
                    if service == .bangumi, AppModel.bangumiApp != nil {
                        showingBangumiSignIn = true
                    } else {
                        tokenService = service
                    }
                }
                .buttonStyle(.bordered)
                .buttonBorderShape(.capsule)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
    }
}
