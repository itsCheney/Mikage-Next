import SwiftUI
import VNCore

/// Bangumi's sign-in form, drawn natively the way Bangumi for Android and
/// czy0729/Bangumi do it: email, password and the website's captcha. Once
/// the website accepts them, `onSignedIn` finishes the job (for an account,
/// approving Mikage for the API) before the sheet closes.
struct BangumiSignInView: View {
    @Environment(\.dismiss) private var dismiss
    let onSignedIn: () async throws -> Void

    private enum Field { case email, password, captcha }

    @AppStorage("bangumi.signIn.email") private var email = ""
    @State private var password = ""
    @State private var captcha = ""
    @State private var captchaImage: UIImage?
    @State private var loadingCaptcha = false
    @State private var signingIn = false
    /// The website took the password; only the follow-up remains.
    @State private var webSignedIn = false
    @State private var error: String?
    @FocusState private var focus: Field?

    private var canSubmit: Bool {
        if webSignedIn { return !signingIn }
        return !signingIn && !email.trimmingCharacters(in: .whitespaces).isEmpty && !password.isEmpty
            && !captcha.trimmingCharacters(in: .whitespaces).isEmpty
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Email", text: $email)
                        .textContentType(.username)
                        .keyboardType(.emailAddress)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .focused($focus, equals: .email)
                        .submitLabel(.next)
                        .onSubmit { focus = .password }
                    SecureField("Password", text: $password)
                        .textContentType(.password)
                        .focused($focus, equals: .password)
                        .submitLabel(.next)
                        .onSubmit { focus = .captcha }
                } footer: {
                    Text("Your password goes only to bgm.tv and is not stored by Mikage.")
                }

                Section {
                    HStack(spacing: 12) {
                        TextField("Code", text: $captcha)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .focused($focus, equals: .captcha)
                            .submitLabel(.go)
                            .onSubmit { Task { await submit() } }
                        Button {
                            Task { await reloadCaptcha() }
                        } label: {
                            captchaView
                        }
                        .buttonStyle(.plain)
                        .disabled(loadingCaptcha || signingIn)
                        .accessibilityLabel("New Code")
                    }
                } header: {
                    Text("Verification Code")
                } footer: {
                    Text("Type the characters in the picture. Tap the picture for a new one.")
                }

                if let error {
                    Section {
                        Text(error)
                            .foregroundStyle(.red)
                    }
                }

                Section {
                    Link(destination: URL(string: "https://bgm.tv/signup")!) {
                        Label("Create a Bangumi Account", systemImage: "arrow.up.right.square")
                    }
                }
            }
            .navigationTitle("Sign In to Bangumi")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                        .disabled(signingIn)
                }
                ToolbarItem(placement: .confirmationAction) {
                    if signingIn {
                        ProgressView()
                    } else {
                        Button("Sign In") { Task { await submit() } }
                            .disabled(!canSubmit)
                    }
                }
            }
            .task { await start() }
        }
        .interactiveDismissDisabled(signingIn)
    }

    @ViewBuilder
    private var captchaView: some View {
        ZStack {
            Color(uiColor: .tertiarySystemFill)
            if let captchaImage {
                Image(uiImage: captchaImage)
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
            }
            if loadingCaptcha {
                ProgressView()
            } else if captchaImage == nil {
                Image(systemName: "arrow.clockwise")
                    .foregroundStyle(.secondary)
            }
        }
        .frame(width: 120, height: 44)
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private func start() async {
        loadingCaptcha = true
        defer { loadingCaptcha = false }
        do {
            captchaImage = UIImage(data: try await BangumiWebSession.shared.beginSignIn())
            focus = email.isEmpty ? .email : .password
        } catch {
            self.error = error.localizedDescription
        }
    }

    private func reloadCaptcha() async {
        loadingCaptcha = true
        defer { loadingCaptcha = false }
        captcha = ""
        do {
            captchaImage = UIImage(data: try await BangumiWebSession.shared.captcha())
        } catch {
            self.error = error.localizedDescription
        }
    }

    private func submit() async {
        guard canSubmit else { return }
        signingIn = true
        error = nil
        defer { signingIn = false }
        if !webSignedIn {
            do {
                try await BangumiWebSession.shared.signIn(
                    email: email.trimmingCharacters(in: .whitespaces),
                    password: password,
                    captcha: captcha
                )
                webSignedIn = true
            } catch {
                self.error = error.localizedDescription
                // A captcha is good for one attempt, and a refused sign-in
                // may have ended the session, so start the form again.
                captchaImage = nil
                captcha = ""
                await start()
                focus = .captcha
                return
            }
        }
        do {
            try await onSignedIn()
            dismiss()
        } catch {
            self.error = error.localizedDescription
        }
    }
}
