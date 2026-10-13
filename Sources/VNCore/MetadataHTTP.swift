import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

public enum MetadataError: LocalizedError, Equatable {
    /// The token was rejected or lacks the needed permission.
    case unauthorized(MetadataService)
    case rateLimited(MetadataService)
    case http(MetadataService, Int, String)
    case invalidResponse(MetadataService)

    public var errorDescription: String? {
        switch self {
        case .unauthorized(let service):
            return String(localized: "\(service.displayName) didn’t accept the token. Check it in Settings › Accounts & Sync.")
        case .rateLimited(let service):
            return String(localized: "\(service.displayName) is busy. Try again in a minute.")
        case .http(let service, let code, let message):
            return message.isEmpty
                ? String(localized: "\(service.displayName) returned error \(code).")
                : String(localized: "\(service.displayName) returned error \(code): \(message)")
        case .invalidResponse(let service):
            return String(localized: "\(service.displayName) sent a response Mikage couldn’t read.")
        }
    }
}

/// Shared request plumbing for the two service clients.
struct MetadataHTTP: Sendable {
    let service: MetadataService
    let session: URLSession

    static let userAgent = "itsCheney/Mikage-Next (https://github.com/itsCheney/Mikage-Next)"

    func request(
        _ url: URL,
        method: String = "GET",
        authorization: String? = nil,
        json body: Any? = nil
    ) throws -> URLRequest {
        var request = URLRequest(url: url, timeoutInterval: 20)
        request.httpMethod = method
        request.setValue(Self.userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if let authorization {
            request.setValue(authorization, forHTTPHeaderField: "Authorization")
        }
        if let body {
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        return request
    }

    /// Sends the request and returns the body of a 2xx response.
    @discardableResult
    func send(_ request: URLRequest) async throws -> Data {
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else {
            throw MetadataError.invalidResponse(service)
        }
        switch http.statusCode {
        case 200..<300:
            return data
        case 401, 403:
            throw MetadataError.unauthorized(service)
        case 429:
            throw MetadataError.rateLimited(service)
        default:
            throw MetadataError.http(service, http.statusCode, Self.message(in: data))
        }
    }

    func decode<T: Decodable>(_ type: T.Type, from data: Data) throws -> T {
        do {
            return try JSONDecoder().decode(type, from: data)
        } catch {
            throw MetadataError.invalidResponse(service)
        }
    }

    /// Both services answer errors with a short text or a JSON object holding
    /// a `description` or `message`; keep it short enough for an alert.
    private static func message(in data: Data) -> String {
        if let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            for key in ["description", "message", "title", "id"] {
                if let text = object[key] as? String, !text.isEmpty {
                    return String(text.prefix(200))
                }
            }
        }
        let text = String(decoding: data.prefix(200), as: UTF8.self)
        return text.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

/// A service account confirmed by its token.
public struct ServiceAccount: Codable, Equatable, Sendable {
    public var service: MetadataService
    public var username: String
    /// VNDB's user id, such as "u2"; list queries need it.
    public var userID: String?

    public init(service: MetadataService, username: String, userID: String? = nil) {
        self.service = service
        self.username = username
        self.userID = userID
    }
}

/// Reads list dates as the plain days both services store.
enum ListDay {
    static func date(_ day: String?) -> Date? {
        guard let day, day.count >= 10 else { return nil }
        let parts = day.prefix(10).split(separator: "-").compactMap { Int($0) }
        guard parts.count == 3 else { return nil }
        return Calendar(identifier: .gregorian).date(
            from: DateComponents(timeZone: .current, year: parts[0], month: parts[1], day: parts[2], hour: 12)
        )
    }
}
