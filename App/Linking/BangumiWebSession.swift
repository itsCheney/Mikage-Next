import SwiftUI
import Security
import VNCore

enum BangumiWebError: LocalizedError, Equatable {
    case signInRequired
    case http(Int)
    case notRemoved
    case authorizationRefused
    case signInRefused(String?)

    var errorDescription: String? {
        switch self {
        case .signInRequired:
            return String(localized: "Your Bangumi sign-in has expired. Sign in again under Settings › Accounts & Sync.")
        case .http(let code):
            return String(localized: "Bangumi’s website returned error \(code).")
        case .notRemoved:
            return String(localized: "Bangumi still lists the game after the removal request. The website may have changed.")
        case .authorizationRefused:
            return String(localized: "Bangumi didn’t let Mikage connect to your account. Try signing in again.")
        case .signInRefused(let message):
            return message ?? String(localized: "Bangumi didn’t accept the email, password or code. Check them and try again.")
        }
    }
}

/// A Bangumi website session. Like the community clients, Mikage signs in
/// with Bangumi's own sign-in form (email, password and captcha) and keeps
/// the session's cookies in the Keychain. The session approves Mikage for
/// the API and removes collections, which API v0 cannot do.
@MainActor
final class BangumiWebSession {
    static let shared = BangumiWebSession()

    /// Bangumi ties a session to the user agent that signed in, so every
    /// request sends the same one.
    static let userAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"

    private let keychain = KeychainItem(account: "bangumi.web")
    private var cookies: [String: String]
    /// The sign-in form's formhash, from the page that issued the captcha.
    private var signInFormhash: String?

    /// Cookies are sent and stored by hand, so redirects can be read and
    /// nothing leaks into the shared cookie storage.
    private let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.httpCookieAcceptPolicy = .never
        configuration.httpShouldSetCookies = false
        configuration.timeoutIntervalForRequest = 20
        return URLSession(configuration: configuration)
    }()

    private init() {
        cookies = keychain.read()
            .flatMap { try? JSONDecoder().decode([String: String].self, from: $0) } ?? [:]
    }

    private func saveCookies() {
        if cookies.isEmpty {
            keychain.delete()
        } else if let data = try? JSONEncoder().encode(cookies) {
            keychain.write(data)
        }
    }

    private func absorbCookies(from response: HTTPURLResponse, for url: URL) {
        var fields: [String: String] = [:]
        for (name, value) in response.allHeaderFields {
            if let name = name as? String, let value = value as? String {
                fields[name] = value
            }
        }
        for cookie in HTTPCookie.cookies(withResponseHeaderFields: fields, for: url) {
            if cookie.value == "deleted" || cookie.value == "delete" || (cookie.expiresDate.map { $0 < Date() } ?? false) {
                cookies[cookie.name] = nil
            } else {
                cookies[cookie.name] = cookie.value
            }
        }
    }

    /// Sends a request with the session's cookies and keeps any it sets.
    /// Redirects are followed by hand so every hop's cookies are kept,
    /// except when the caller needs the redirect itself.
    private func send(
        _ url: URL,
        method: String = "GET",
        form: Data? = nil,
        referer: URL? = nil,
        followRedirects: Bool = true
    ) async throws -> (Data, HTTPURLResponse) {
        var url = url
        var method = method
        var form = form
        for _ in 0..<6 {
            var request = URLRequest(url: url)
            request.httpMethod = method
            request.setValue(Self.userAgent, forHTTPHeaderField: "User-Agent")
            if !cookies.isEmpty {
                request.setValue(
                    cookies.map { "\($0.key)=\($0.value)" }.sorted().joined(separator: "; "),
                    forHTTPHeaderField: "Cookie"
                )
            }
            if let referer {
                request.setValue(referer.absoluteString, forHTTPHeaderField: "Referer")
            }
            if let form {
                request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
                request.httpBody = form
            }
            let (data, response) = try await session.data(for: request, delegate: NoRedirects.shared)
            guard let http = response as? HTTPURLResponse else { throw BangumiWebError.http(0) }
            absorbCookies(from: http, for: url)
            saveCookies()
            if followRedirects, (300..<400).contains(http.statusCode),
               let location = http.value(forHTTPHeaderField: "Location"),
               let next = URL(string: location, relativeTo: url)?.absoluteURL {
                url = next
                method = "GET"
                form = nil
                continue
            }
            if !(200..<400).contains(http.statusCode) {
                throw BangumiWebError.http(http.statusCode)
            }
            return (data, http)
        }
        throw BangumiWebError.http(310)
    }

    private func page(_ url: URL) async throws -> String {
        guard !cookies.isEmpty else { throw BangumiWebError.signInRequired }
        return String(decoding: try await send(url).0, as: UTF8.self)
    }

    // MARK: Signing in

    /// Starts a sign-in: a fresh session from bgm.tv/login, then its
    /// captcha image.
    func beginSignIn() async throws -> Data {
        cookies = [:]
        saveCookies()
        let html = String(decoding: try await send(BangumiWeb.loginURL).0, as: UTF8.self)
        signInFormhash = BangumiWeb.signInFormhash(in: html)
        return try await captcha()
    }

    /// Another captcha for the same session.
    func captcha() async throws -> Data {
        try await send(BangumiWeb.captchaURL(), referer: BangumiWeb.loginURL).0
    }

    /// Posts the sign-in form. Bangumi answers with its own page either way;
    /// a session cookie and a signed-in page mean it worked.
    func signIn(email: String, password: String, captcha: String) async throws {
        let fields = BangumiWeb.signInForm(
            formhash: signInFormhash ?? "",
            email: email,
            password: password,
            captcha: captcha.replacingOccurrences(of: " ", with: "")
        )
        let (data, _) = try await send(
            BangumiWeb.signInURL,
            method: "POST",
            form: BangumiOAuth.formBody(fields),
            referer: BangumiWeb.loginURL
        )
        let html = String(decoding: data, as: UTF8.self)
        var signedIn = BangumiWeb.formhash(in: html) != nil
        if !signedIn, cookies["chii_auth"] != nil {
            signedIn = (try? await isSignedIn()) == true
        }
        guard signedIn else {
            AppDiagnostics.shared.event("accounts", "bangumi.signIn.refused")
            throw BangumiWebError.signInRefused(BangumiWeb.signInError(in: html))
        }
        AppDiagnostics.shared.event("accounts", "bangumi.signIn.end")
    }

    // MARK: Using the session

    /// Approves Mikage on Bangumi with the website session, as the
    /// community clients do, and exchanges the code for a token pair.
    func authorize(_ app: BangumiAppCredentials) async throws -> BangumiToken {
        guard !cookies.isEmpty else { throw BangumiWebError.signInRequired }
        let url = BangumiOAuth.authorizeURL(app)
        let (data, page) = try await send(url, followRedirects: false)
        var response = page
        // An app approved before may be sent straight back with a code.
        if page.value(forHTTPHeaderField: "Location").flatMap(BangumiOAuth.code(fromRedirect:)) == nil {
            // The approval page carries the form's own formhash; the
            // session's, from any signed-in page, works the same.
            var formhash = BangumiWeb.authorizeFormhash(in: String(decoding: data, as: UTF8.self))
            if formhash == nil {
                formhash = BangumiWeb.formhash(in: try await self.page(BangumiWeb.baseURL))
            }
            guard let formhash else {
                AppDiagnostics.shared.event("accounts", "bangumi.authorize.signedOut")
                throw BangumiWebError.signInRequired
            }
            response = try await send(
                url,
                method: "POST",
                form: BangumiOAuth.authorizeForm(formhash: formhash, app: app),
                referer: url,
                followRedirects: false
            ).1
        }
        guard let location = response.value(forHTTPHeaderField: "Location"),
              let code = BangumiOAuth.code(fromRedirect: location) else {
            AppDiagnostics.shared.event("accounts", "bangumi.authorize.refused", ["status": String(response.statusCode)])
            throw BangumiWebError.authorizationRefused
        }
        return try await BangumiOAuth.requestToken(BangumiOAuth.tokenRequest(code: code, app: app))
    }

    /// Whether the stored cookies still sign the website in.
    func isSignedIn() async throws -> Bool {
        BangumiWeb.formhash(in: try await page(BangumiWeb.baseURL)) != nil
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

    func signOut() {
        cookies = [:]
        signInFormhash = nil
        saveCookies()
    }
}

/// Keeps a request's redirect as the response.
private final class NoRedirects: NSObject, URLSessionTaskDelegate {
    static let shared = NoRedirects()

    func urlSession(
        _ session: URLSession,
        task: URLSessionTask,
        willPerformHTTPRedirection response: HTTPURLResponse,
        newRequest request: URLRequest,
        completionHandler: @escaping (URLRequest?) -> Void
    ) {
        completionHandler(nil)
    }
}

/// One generic password in this device's Keychain.
private struct KeychainItem {
    let account: String
    private let service = "moe.cheney233.mikage.accounts"

    private var query: [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
    }

    func read() -> Data? {
        var lookup = query
        lookup[kSecReturnData as String] = true
        lookup[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        guard SecItemCopyMatching(lookup as CFDictionary, &result) == errSecSuccess else { return nil }
        return result as? Data
    }

    func write(_ data: Data) {
        let status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        if status == errSecItemNotFound {
            var item = query
            item[kSecValueData as String] = data
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            SecItemAdd(item as CFDictionary, nil)
        }
    }

    func delete() {
        SecItemDelete(query as CFDictionary)
    }
}
