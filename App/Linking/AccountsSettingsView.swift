import SwiftUI
import VNCore

/// Settings › Accounts & Sync: Bangumi and VNDB tokens, what syncs, and how
/// linked games look in the library.
struct AccountsSettingsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var tokenService: MetadataService?
    @State private var disconnectService: MetadataService?
    @State private var showingBangumiWeb = false
    @State private var confirmingWebSignOut = false
    @State private var showingBangumiSignIn = false

    /// Bangumi signs in on its website when Mikage has a registered app;
    /// otherwise, and for anyone who prefers it, a pasted token works too.
    private var bangumiSignInAvailable: Bool { AppModel.bangumiApp != nil }

    var body: some View {
        Form {
            Section {
                ForEach(MetadataService.allCases) { service in
                    accountRow(service)
                }
                if bangumiSignInAvailable, model.accounts[.bangumi] == nil {
                    Button("Use a Bangumi Access Token Instead") { tokenService = .bangumi }
                        .font(.footnote)
                }
            } header: {
                Text("Accounts")
            } footer: {
                Text("Covers and synopses load without an account. Connect to sync your status, ratings and comments to your lists.")
            }

            // Signing in covers the website already; a pasted token does not.
            if model.accounts[.bangumi] != nil, !model.bangumiUsesSignIn {
                Section {
                    bangumiWebRow
                } footer: {
                    Text("Bangumi’s API cannot remove a game from your collection, so Mikage uses the website for that. Sign in once here; Mikage only uses the session to remove collections.")
                }
            }

            Section {
                Toggle("Update Status While Playing", isOn: $model.settings.autoStatus)
                Toggle("Sync Ratings and Comments", isOn: $model.settings.syncRatings)
                Toggle("Record Start and Finish Dates", isOn: $model.settings.syncDates)
            } header: {
                Text("Sync")
            } footer: {
                Text("After a session, a game with no status, or one you want to play or put on hold, moves to Playing. Dates go to your VNDB list. Playtime on this device stays in Mikage.")
            }

            Section {
                Picker("Titles", selection: $model.settings.titleDisplay) {
                    ForEach(TitleDisplay.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
                .pickerStyle(.menu)
                Picker("Banner Image", selection: $model.settings.bannerArt) {
                    ForEach(BannerArt.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
                .pickerStyle(.menu)
                Toggle("Hide Sensitive Images", isOn: $model.settings.hideSensitiveImages)
            } header: {
                Text("Details")
            } footer: {
                Text("A cover you set yourself is never replaced.")
            }

            Section {
                syncStatus
                Button("Sync Now") {
                    Task { await model.syncPending() }
                }
                .disabled(model.syncing || model.accounts.isEmpty)
            } footer: {
                Text("Tokens are stored in this device’s Keychain and only used to read and update your lists.")
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Accounts & Sync")
        .navigationBarTitleDisplayMode(.inline)
        .sheet(item: $tokenService) { service in
            TokenEntryView(service: service).environmentObject(model)
        }
        .sheet(isPresented: $showingBangumiSignIn) {
            BangumiSignInView {
                try await model.connectBangumiWithWebSession()
            }
        }
        .sheet(isPresented: $showingBangumiWeb) {
            BangumiSignInView {
                await model.refreshBangumiWebSession()
            }
        }
        .confirmationDialog(
            "Sign out of the Bangumi website?",
            isPresented: $confirmingWebSignOut,
            titleVisibility: .visible
        ) {
            Button("Sign Out", role: .destructive) {
                model.signOutOfBangumiWeb()
            }
        } message: {
            Text("Games you clear the status of will stay in your Bangumi collection until you sign in again.")
        }
        .task {
            await model.refreshBangumiWebSession()
            await model.refreshAccountAvatar()
        }
        .confirmationDialog(
            "Disconnect this account?",
            isPresented: Binding(
                get: { disconnectService != nil },
                set: { if !$0 { disconnectService = nil } }
            ),
            titleVisibility: .visible,
            presenting: disconnectService
        ) { service in
            Button("Disconnect", role: .destructive) {
                model.disconnect(service)
                disconnectService = nil
            }
        } message: { _ in
            Text("The token is removed from this device. Your lists and the games linked here stay as they are.")
        }
    }

    @ViewBuilder
    private func accountRow(_ service: MetadataService) -> some View {
        if let account = model.accounts[service] {
            let expired = service == .bangumi && model.bangumiSignInExpired && bangumiSignInAvailable
            Button {
                if expired {
                    showingBangumiSignIn = true
                } else {
                    disconnectService = service
                }
            } label: {
                HStack(spacing: 12) {
                    ServiceAccountIcon(service: service, account: account)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(service.displayName)
                            .font(.body.weight(.semibold))
                            .foregroundStyle(.primary)
                        if service == .bangumi, model.bangumiSignInExpired {
                            Text("@\(account.username) · Sign-In Expired")
                                .font(.footnote)
                                .foregroundStyle(.orange)
                        } else {
                            Text("@\(account.username) · Connected")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }
                    Spacer()
                    if expired {
                        Text("Sign In")
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(Color.accentColor)
                    } else {
                        Image(systemName: "chevron.right")
                            .font(.footnote.weight(.semibold))
                            .foregroundStyle(.tertiary)
                    }
                }
                .padding(.vertical, 6)
            }
        } else {
            HStack(spacing: 12) {
                ServiceAccountIcon(service: service, account: nil)
                VStack(alignment: .leading, spacing: 2) {
                    Text(service.displayName)
                        .font(.body.weight(.semibold))
                    Text("Not Connected")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                if service == .bangumi, bangumiSignInAvailable {
                    signInButton
                } else {
                    Button("Add Token") { tokenService = service }
                        .buttonStyle(.bordered)
                        .buttonBorderShape(.capsule)
                }
            }
            .padding(.vertical, 6)
        }
    }

    private var signInButton: some View {
        Button("Sign In") { showingBangumiSignIn = true }
            .buttonStyle(.bordered)
            .buttonBorderShape(.capsule)
    }

    private var bangumiWebRow: some View {
        HStack(spacing: 12) {
            Image(systemName: "globe")
                .font(.headline)
                .frame(width: 40, height: 40)
                .foregroundStyle(model.bangumiWebSignedIn ? Color.accentColor : Color.secondary)
                .background(
                    (model.bangumiWebSignedIn ? Color.accentColor.opacity(0.16) : Color(uiColor: .tertiarySystemFill)),
                    in: Circle()
                )
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 2) {
                Text("Bangumi Website")
                    .font(.body.weight(.semibold))
                Text(model.bangumiWebSignedIn ? LocalizedStringKey("Signed In") : LocalizedStringKey("Not Signed In"))
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if model.bangumiWebSignedIn {
                Button("Sign Out") { confirmingWebSignOut = true }
                    .buttonStyle(.bordered)
                    .buttonBorderShape(.capsule)
            } else {
                Button("Sign In") { showingBangumiWeb = true }
                    .buttonStyle(.bordered)
                    .buttonBorderShape(.capsule)
            }
        }
        .padding(.vertical, 6)
    }

    @ViewBuilder
    private var syncStatus: some View {
        let pending = model.pendingSyncCount
        HStack(spacing: 10) {
            if model.syncing {
                ProgressView()
                Text("Syncing…")
            } else if model.accounts.isEmpty {
                Image(systemName: "person.crop.circle.badge.questionmark")
                    .foregroundStyle(.secondary)
                Text("No account connected")
            } else if pending > 0 {
                Image(systemName: "clock.fill")
                    .foregroundStyle(.orange)
                Text("\(pending) games waiting to sync")
            } else {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                Text("Everything Synced")
            }
            Spacer()
            if !model.syncing, pending == 0, let last = model.lastSyncedAt, !model.accounts.isEmpty {
                Text(last, style: .relative)
                    .foregroundStyle(.secondary)
                    .font(.subheadline)
            }
        }
        if let error = model.lastSyncError, pending > 0 {
            Text(error)
                .font(.footnote)
                .foregroundStyle(.red)
        }
    }
}

/// Paste a personal access token; it is checked with the service before it
/// is saved.
struct TokenEntryView: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let service: MetadataService

    @State private var token = ""
    @State private var connecting = false
    @State private var error: String?

    private var tokenPage: URL {
        service == .bangumi ? BangumiClient.tokenPageURL : VNDBClient.tokenPageURL
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    SecureField("Token", text: $token)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .submitLabel(.done)
                        .onSubmit { Task { await connect() } }
                } footer: {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(service == .bangumi
                             ? LocalizedStringKey("Create a personal access token on Bangumi, then paste it here. Signing in also lets search find R18 titles.")
                             : LocalizedStringKey("Create a token on VNDB with permission to read and write your list, then paste it here."))
                        Link(destination: tokenPage) {
                            Label("Get a Token", systemImage: "arrow.up.right.square")
                        }
                    }
                }
                if let error {
                    Section {
                        Text(error)
                            .foregroundStyle(.red)
                    }
                }
            }
            .navigationTitle(Text(verbatim: service.displayName))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                        .disabled(connecting)
                }
                ToolbarItem(placement: .confirmationAction) {
                    if connecting {
                        ProgressView()
                    } else {
                        Button("Connect") { Task { await connect() } }
                            .disabled(token.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    }
                }
            }
        }
        .interactiveDismissDisabled(connecting)
    }

    private func connect() async {
        guard !connecting else { return }
        connecting = true
        error = nil
        defer { connecting = false }
        do {
            try await model.connect(service, token: token)
            dismiss()
        } catch {
            self.error = error.localizedDescription
        }
    }
}

/// A service's icon, or once signed in, the account's picture with the
/// service's icon as a badge in its corner.
struct ServiceAccountIcon: View {
    let service: MetadataService
    let account: ServiceAccount?
    var size: CGFloat = 40

    var body: some View {
        Group {
            if let account {
                avatar(account)
                    .frame(width: size, height: size)
                    .clipShape(Circle())
                    .overlay(alignment: .bottomTrailing) {
                        Self.serviceImage(service)
                            .frame(width: size * 0.45, height: size * 0.45)
                            .clipShape(RoundedRectangle(cornerRadius: size * 0.12, style: .continuous))
                            .overlay(
                                RoundedRectangle(cornerRadius: size * 0.12, style: .continuous)
                                    .strokeBorder(Color(uiColor: .systemBackground), lineWidth: 2)
                            )
                            .offset(x: size * 0.08, y: size * 0.08)
                    }
            } else {
                Self.serviceImage(service)
                    .frame(width: size, height: size)
                    .clipShape(RoundedRectangle(cornerRadius: size * 0.22, style: .continuous))
                    .overlay(
                        RoundedRectangle(cornerRadius: size * 0.22, style: .continuous)
                            .strokeBorder(Color.primary.opacity(0.1), lineWidth: 0.5)
                    )
            }
        }
        .accessibilityHidden(true)
    }

    @ViewBuilder
    private func avatar(_ account: ServiceAccount) -> some View {
        let initial = Text(verbatim: account.username.first.map { String($0).uppercased() } ?? "?")
            .font(.system(size: size * 0.42, weight: .bold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(LinearGradient(
                colors: [Color.accentColor.opacity(0.7), Color.accentColor],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            ))
        if let url = account.avatarURL.flatMap(URL.init(string:)) {
            AsyncImage(url: url) { image in
                image.resizable().scaledToFill()
            } placeholder: {
                initial
            }
        } else {
            initial
        }
    }

    /// The services' own icons. VNDB only publishes a 16 px favicon, so
    /// it is scaled without smoothing to keep its pixels crisp.
    static func serviceImage(_ service: MetadataService) -> some View {
        switch service {
        case .bangumi:
            return Image("BangumiIcon").resizable().interpolation(.high)
        case .vndb:
            return Image("VNDBIcon").resizable().interpolation(.none)
        }
    }
}
