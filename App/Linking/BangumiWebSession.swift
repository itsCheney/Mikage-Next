import SwiftUI
import WebKit
import VNCore

enum BangumiWebError: LocalizedError, Equatable {
    case signInRequired
    case http(Int)
    case notRemoved

    var errorDescription: String? {
        switch self {
        case .signInRequired:
            return String(localized: "Removing a game from your Bangumi collection needs Bangumi web sign-in. Sign in under Settings › Accounts & Sync.")
        case .http(let code):
            return String(localized: "Bangumi’s website returned error \(code).")
        case .notRemoved:
            return String(localized: "Bangumi still lists the game after the removal request. The website may have changed.")
        }
    }
}

/// A Bangumi website session, kept as cookies in the app's default web
/// data store. Personal access tokens cannot act on the website, so
/// removing a collection signs in here once through `BangumiWebLoginView`.
@MainActor
final class BangumiWebSession {
    static let shared = BangumiWebSession()

    /// Bangumi's pages answer a browser; a bare client may be refused.
    static let userAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"

    private var cookieStore: WKHTTPCookieStore {
        WKWebsiteDataStore.default().httpCookieStore
    }

    private func cookies() async -> [HTTPCookie] {
        await cookieStore.allCookies().filter { $0.domain.hasSuffix("bgm.tv") }
    }

    /// Loads a page with the session's cookies.
    private func page(_ url: URL) async throws -> String {
        let cookies = await cookies()
        guard !cookies.isEmpty else { throw BangumiWebError.signInRequired }
        var request = URLRequest(url: url, timeoutInterval: 20)
        request.httpShouldHandleCookies = false
        request.setValue(Self.userAgent, forHTTPHeaderField: "User-Agent")
        for (field, value) in HTTPCookie.requestHeaderFields(with: cookies) {
            request.setValue(value, forHTTPHeaderField: field)
        }
        let (data, response) = try await URLSession.shared.data(for: request)
        if let http = response as? HTTPURLResponse, !(200..<400).contains(http.statusCode) {
            throw BangumiWebError.http(http.statusCode)
        }
        return String(decoding: data, as: UTF8.self)
    }

    /// Whether the stored cookies still sign the website in.
    func isSignedIn() async -> Bool {
        guard let html = try? await page(BangumiWeb.baseURL) else { return false }
        return BangumiWeb.formhash(in: html) != nil
    }

    /// Sends the website's remove request for the subject. The caller
    /// confirms the result through the API.
    func removeCollection(subjectID: Int) async throws {
        let html = try await page(BangumiWeb.subjectURL(subjectID))
        guard let formhash = BangumiWeb.formhash(in: html, subjectID: subjectID) else {
            throw BangumiWebError.signInRequired
        }
        AppDiagnostics.shared.event("linking", "bangumi.web.remove", ["subject": String(subjectID)])
        _ = try await page(BangumiWeb.removeURL(subjectID: subjectID, formhash: formhash))
    }

    func signOut() async {
        let store = WKWebsiteDataStore.default()
        let records = await store.dataRecords(ofTypes: WKWebsiteDataStore.allWebsiteDataTypes())
        await store.removeData(
            ofTypes: WKWebsiteDataStore.allWebsiteDataTypes(),
            for: records.filter { $0.displayName.hasSuffix("bgm.tv") }
        )
        for cookie in await cookies() {
            await cookieStore.deleteCookie(cookie)
        }
    }
}

/// Bangumi's own sign-in page. Finishes once a loaded page carries a
/// logout link, which only signed-in pages have.
struct BangumiWebLoginView: View {
    @Environment(\.dismiss) private var dismiss
    let onSignedIn: () -> Void

    var body: some View {
        NavigationStack {
            BangumiWebView {
                onSignedIn()
                dismiss()
            }
            .ignoresSafeArea(edges: .bottom)
            .navigationTitle("Sign In to Bangumi")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
    }
}

private struct BangumiWebView: UIViewRepresentable {
    let onSignedIn: () -> Void

    func makeCoordinator() -> Coordinator { Coordinator(onSignedIn: onSignedIn) }

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .default()
        let view = WKWebView(frame: .zero, configuration: configuration)
        view.navigationDelegate = context.coordinator
        view.load(URLRequest(url: BangumiWeb.loginURL))
        return view
    }

    func updateUIView(_ uiView: WKWebView, context: Context) { }

    final class Coordinator: NSObject, WKNavigationDelegate {
        let onSignedIn: () -> Void
        private var finished = false

        init(onSignedIn: @escaping () -> Void) {
            self.onSignedIn = onSignedIn
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            guard !finished, webView.url?.host?.hasSuffix("bgm.tv") == true else { return }
            webView.evaluateJavaScript("document.documentElement.outerHTML") { [weak self] result, _ in
                guard let self, !self.finished, let html = result as? String,
                      BangumiWeb.formhash(in: html) != nil else { return }
                self.finished = true
                self.onSignedIn()
            }
        }
    }
}
