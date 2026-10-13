import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// Mikage's registered Bangumi app (bgm.tv/dev/app).
public struct BangumiAppCredentials: Equatable, Sendable {
    public var clientID: String
    public var clientSecret: String
    public var redirectURI: String

    public init(clientID: String, clientSecret: String, redirectURI: String) {
        self.clientID = clientID
        self.clientSecret = clientSecret
        self.redirectURI = redirectURI
    }
}

/// An OAuth token from bgm.tv. Access tokens last a week; the refresh
/// token gets a new pair without signing in again.
public struct BangumiToken: Codable, Equatable, Sendable {
    public var accessToken: String
    public var refreshToken: String
    public var expiresAt: Date
    public var userID: Int?

    public init(accessToken: String, refreshToken: String, expiresAt: Date, userID: Int? = nil) {
        self.accessToken = accessToken
        self.refreshToken = refreshToken
        self.expiresAt = expiresAt
        self.userID = userID
    }

    /// True within a day of expiry, so a sync never starts on a token that
    /// lapses halfway through.
    public func needsRefresh(now: Date = Date()) -> Bool {
        now.addingTimeInterval(24 * 60 * 60) >= expiresAt
    }

    /// How the token is kept in the Keychain. A personal access token is
    /// kept as the bare token, so the two never read as each other.
    public var stored: String {
        let data = (try? JSONEncoder().encode(self)) ?? Data()
        return String(decoding: data, as: UTF8.self)
    }

    public init?(stored: String) {
        guard stored.hasPrefix("{"),
              let token = try? JSONDecoder().decode(BangumiToken.self, from: Data(stored.utf8)) else { return nil }
        self = token
    }
}

/// Bangumi's OAuth flow as the community clients drive it (Bangumi for
/// Android, czy0729/Bangumi): the player signs in on bgm.tv itself, then the
/// website session approves Mikage by posting the authorize form with its
/// formhash. The redirect carries a code, which becomes a token pair.
/// A lapsed access token is refreshed with the refresh token, and when that
/// fails too, the website session approves Mikage again.
public enum BangumiOAuth {
    public static let tokenURL = URL(string: "https://bgm.tv/oauth/access_token")!

    public static func authorizeURL(_ app: BangumiAppCredentials) -> URL {
        var components = URLComponents(string: "https://bgm.tv/oauth/authorize")!
        components.queryItems = [
            URLQueryItem(name: "client_id", value: app.clientID),
            URLQueryItem(name: "response_type", value: "code"),
            URLQueryItem(name: "redirect_uri", value: app.redirectURI)
        ]
        return components.url!
    }

    /// The authorize form's fields. The redirect is left empty, as the
    /// website's own form does, so the registered one applies.
    public static func authorizeForm(formhash: String, app: BangumiAppCredentials) -> Data {
        formBody([
            ("formhash", formhash),
            ("redirect_uri", ""),
            ("client_id", app.clientID),
            ("submit", "授权")
        ])
    }

    /// The code in the authorize redirect, nil when the website refused.
    public static func code(fromRedirect location: String) -> String? {
        URLComponents(string: location)?.queryItems?.first { $0.name == "code" }?.value?.nonEmpty
    }

    public static func tokenRequest(code: String, app: BangumiAppCredentials) -> URLRequest {
        tokenRequest([
            ("grant_type", "authorization_code"),
            ("client_id", app.clientID),
            ("client_secret", app.clientSecret),
            ("code", code),
            ("redirect_uri", app.redirectURI)
        ])
    }

    public static func refreshRequest(refreshToken: String, app: BangumiAppCredentials) -> URLRequest {
        tokenRequest([
            ("grant_type", "refresh_token"),
            ("client_id", app.clientID),
            ("client_secret", app.clientSecret),
            ("refresh_token", refreshToken),
            ("redirect_uri", app.redirectURI)
        ])
    }

    private struct TokenResponse: Decodable {
        var accessToken: String
        var refreshToken: String
        var expiresIn: Double
        var userID: Int?

        enum CodingKeys: String, CodingKey {
            case accessToken = "access_token"
            case refreshToken = "refresh_token"
            case expiresIn = "expires_in"
            case userID = "user_id"
        }

        init(from decoder: Decoder) throws {
            let container = try decoder.container(keyedBy: CodingKeys.self)
            accessToken = try container.decode(String.self, forKey: .accessToken)
            refreshToken = try container.decode(String.self, forKey: .refreshToken)
            expiresIn = (try? container.decode(Double.self, forKey: .expiresIn)) ?? 604_800
            // Bangumi sends the user ID as a string.
            if let number = try? container.decode(Int.self, forKey: .userID) {
                userID = number
            } else {
                userID = (try? container.decode(String.self, forKey: .userID)).flatMap { Int($0) }
            }
        }
    }

    public static func token(from data: Data, receivedAt: Date = Date()) throws -> BangumiToken {
        guard let response = try? JSONDecoder().decode(TokenResponse.self, from: data),
              !response.accessToken.isEmpty, !response.refreshToken.isEmpty else {
            throw MetadataError.invalidResponse(.bangumi)
        }
        return BangumiToken(
            accessToken: response.accessToken,
            refreshToken: response.refreshToken,
            expiresAt: receivedAt.addingTimeInterval(response.expiresIn),
            userID: response.userID
        )
    }

    /// Exchanges a code or refresh token for a token pair.
    public static func requestToken(_ request: URLRequest, session: URLSession = .shared) async throws -> BangumiToken {
        let data = try await MetadataHTTP(service: .bangumi, session: session).send(request)
        return try token(from: data)
    }

    private static func tokenRequest(_ fields: [(String, String)]) -> URLRequest {
        var request = URLRequest(url: tokenURL, timeoutInterval: 20)
        request.httpMethod = "POST"
        request.setValue(MetadataHTTP.userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        request.httpBody = formBody(fields)
        return request
    }

    static func formBody(_ fields: [(String, String)]) -> Data {
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-._~")
        let body = fields.map { name, value in
            "\(name)=\(value.addingPercentEncoding(withAllowedCharacters: allowed) ?? "")"
        }.joined(separator: "&")
        return Data(body.utf8)
    }
}
