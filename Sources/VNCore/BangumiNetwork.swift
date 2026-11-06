import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// How Mikage looks up Bangumi's addresses. Some networks poison the DNS
/// answers for Bangumi's domains, so besides the system's resolver Mikage
/// has its own DNS over HTTPS, used for Bangumi's hosts only.
public enum BangumiConnectionMode: String, CaseIterable, Sendable {
    /// The system resolver first, the built-in one when that fails; the
    /// route that worked is remembered.
    case automatic
    case system
    case doh

    public var displayName: String {
        switch self {
        case .automatic: return String(localized: "Automatic")
        case .system: return String(localized: "System DNS")
        case .doh: return String(localized: "Built-In DNS over HTTPS")
        }
    }
}

/// Which of Bangumi's website domains to use for signing in and for the
/// website's own pages. Both serve the same site and accounts.
public enum BangumiWebDomain: String, CaseIterable, Sendable {
    /// bgm.tv, falling back to bangumi.tv when it can't be reached.
    case automatic
    case bgm = "bgm.tv"
    case bangumi = "bangumi.tv"

    public var displayName: String {
        switch self {
        case .automatic: return String(localized: "Automatic")
        case .bgm, .bangumi: return rawValue
        }
    }
}

/// The two ways a request can reach Bangumi.
public enum BangumiRoute: String, CaseIterable, Sendable {
    case system
    case doh
}

public enum BangumiHosts {
    public static let api = "api.bgm.tv"
    public static let images = "lain.bgm.tv"
    /// Website domains in the order Automatic tries them. chii.in serves the
    /// same site, but sits behind a browser check an app can't pass.
    public static let web = ["bgm.tv", "bangumi.tv"]
    /// Everything the connection test checks.
    public static let tested = ["bgm.tv", "bangumi.tv", "chii.in", api, images]

    public static func isBangumi(_ host: String) -> Bool {
        let host = host.lowercased()
        return ["bgm.tv", "bangumi.tv", "chii.in"].contains { host == $0 || host.hasSuffix("." + $0) }
    }
}

/// Sends every request to Bangumi's hosts, choosing between the system
/// resolver and the built-in DNS over HTTPS, and between bgm.tv and
/// bangumi.tv for the website. Requests to other hosts go through an
/// ordinary URLSession. Cookies are never stored and redirects are only
/// followed when asked, so callers keep full control of both.
public final class BangumiNetwork: @unchecked Sendable {
    public static let shared = BangumiNetwork()

    public static let connectionModeKey = "bangumi.connectionMode"
    public static let webDomainKey = "bangumi.webDomain"
    static let learnedRouteKey = "bangumi.learnedRoute"
    static let learnedWebDomainKey = "bangumi.learnedWebDomain"

    /// Reports routing changes to the app's diagnostics log.
    public var onEvent: (@Sendable (String, [String: String]) -> Void)?

    private let defaults: UserDefaults
    private let resolver: DoHResolver
    private let session: URLSession
    private let lock = NSLock()
    /// An address that passed the TLS check, per host, until it expires.
    private var verified: [String: (address: String, until: Date)] = [:]

    public init(defaults: UserDefaults = .standard, resolver: DoHResolver = DoHResolver()) {
        self.defaults = defaults
        self.resolver = resolver
        let configuration = URLSessionConfiguration.ephemeral
        configuration.httpCookieAcceptPolicy = .never
        configuration.httpShouldSetCookies = false
        configuration.urlCache = nil
        session = URLSession(configuration: configuration, delegate: NoRedirects(), delegateQueue: nil)
    }

    // MARK: Settings

    public var connectionMode: BangumiConnectionMode {
        get { defaults.string(forKey: Self.connectionModeKey).flatMap(BangumiConnectionMode.init(rawValue:)) ?? .automatic }
        set { defaults.set(newValue.rawValue, forKey: Self.connectionModeKey) }
    }

    public var webDomain: BangumiWebDomain {
        get { defaults.string(forKey: Self.webDomainKey).flatMap(BangumiWebDomain.init(rawValue:)) ?? .automatic }
        set { defaults.set(newValue.rawValue, forKey: Self.webDomainKey) }
    }

    /// The route Automatic last found working.
    public var learnedRoute: BangumiRoute {
        get { defaults.string(forKey: Self.learnedRouteKey).flatMap(BangumiRoute.init(rawValue:)) ?? .system }
        set { defaults.set(newValue.rawValue, forKey: Self.learnedRouteKey) }
    }

    /// The website domain in use: the chosen one, or the one Automatic
    /// last reached.
    public var webHost: String {
        if webDomain != .automatic { return webDomain.rawValue }
        let learned = defaults.string(forKey: Self.learnedWebDomainKey)
        return learned.flatMap { BangumiHosts.web.contains($0) ? $0 : nil } ?? BangumiHosts.web[0]
    }

    /// A page on the website domain in use.
    public func webURL(_ path: String = "") -> URL {
        URL(string: "https://\(webHost)/\(path)")!
    }

    /// Routes in the order a request tries them.
    public var routes: [BangumiRoute] {
        switch connectionMode {
        case .system: return [.system]
        case .doh: return [.doh]
        case .automatic: return learnedRoute == .doh ? [.doh, .system] : [.system, .doh]
        }
    }

    // MARK: Sending

    /// Sends the request. For a Bangumi host each route is tried in turn
    /// while the failure is a connection failure; for a website page, the
    /// other website domain is tried after that.
    public func data(for request: URLRequest, followRedirects: Bool = false) async throws -> (Data, HTTPURLResponse) {
        guard let url = request.url, let host = url.host, BangumiHosts.isBangumi(host) else {
            return try await follow(request, followRedirects) { try await self.sendSystem($0) }
        }
        var hosts = [host]
        if webDomain == .automatic, BangumiHosts.web.contains(host.lowercased()) {
            hosts += BangumiHosts.web.filter { $0 != host.lowercased() }
        }
        var lastError: Error = URLError(.cannotConnectToHost)
        for candidate in hosts {
            let attempt = Self.request(request, host: candidate)
            let routes = self.routes
            for (index, route) in routes.enumerated() {
                // A poisoned address usually just never answers, so don't
                // wait the full timeout when another route is left to try.
                var timed = attempt
                if index < routes.count - 1 || candidate != hosts.last {
                    timed.timeoutInterval = min(attempt.timeoutInterval, 10)
                }
                do {
                    let result = try await follow(timed, followRedirects) { try await self.send($0, route: route) }
                    remember(route: route, webHost: candidate == host ? nil : candidate)
                    return result
                } catch where Self.isConnectionFailure(error) {
                    lastError = error
                    onEvent?("bangumi.route.failed", ["host": candidate, "route": route.rawValue, "error": Self.describe(error)])
                }
            }
        }
        throw lastError
    }

    private func remember(route: BangumiRoute, webHost: String?) {
        if connectionMode == .automatic, learnedRoute != route {
            learnedRoute = route
            onEvent?("bangumi.route.learned", ["route": route.rawValue])
        }
        if let webHost, webDomain == .automatic {
            defaults.set(webHost, forKey: Self.learnedWebDomainKey)
            onEvent?("bangumi.webDomain.learned", ["host": webHost])
        }
    }

    /// The same request for another website domain, with its Referer
    /// moved along.
    static func request(_ request: URLRequest, host: String) -> URLRequest {
        guard let url = request.url, url.host?.lowercased() != host.lowercased() else { return request }
        var moved = request
        moved.url = replacingHost(url, with: host)
        if let referer = request.value(forHTTPHeaderField: "Referer").flatMap(URL.init(string:)),
           let refererHost = referer.host, BangumiHosts.web.contains(refererHost.lowercased()) {
            moved.setValue(replacingHost(referer, with: host).absoluteString, forHTTPHeaderField: "Referer")
        }
        return moved
    }

    static func replacingHost(_ url: URL, with host: String) -> URL {
        var components = URLComponents(url: url, resolvingAgainstBaseURL: false)!
        components.host = host
        return components.url ?? url
    }

    private func follow(
        _ request: URLRequest,
        _ followRedirects: Bool,
        _ send: (URLRequest) async throws -> (Data, HTTPURLResponse)
    ) async throws -> (Data, HTTPURLResponse) {
        var request = request
        for _ in 0..<6 {
            let (data, response) = try await send(request)
            guard followRedirects, (300..<400).contains(response.statusCode),
                  let location = response.value(forHTTPHeaderField: "Location"),
                  let base = request.url,
                  let next = URL(string: location, relativeTo: base)?.absoluteURL else {
                return (data, response)
            }
            request.url = next
            if response.statusCode != 307, response.statusCode != 308 {
                request.httpMethod = "GET"
                request.httpBody = nil
                request.setValue(nil, forHTTPHeaderField: "Content-Type")
            }
        }
        throw URLError(.httpTooManyRedirects)
    }

    func send(_ request: URLRequest, route: BangumiRoute) async throws -> (Data, HTTPURLResponse) {
        switch route {
        case .system: return try await sendSystem(request)
        case .doh: return try await sendDoH(request).0
        }
    }

    private func sendSystem(_ request: URLRequest) async throws -> (Data, HTTPURLResponse) {
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else { throw URLError(.badServerResponse) }
        return (data, http)
    }

    /// Sends over the built-in resolver: a verified address first, then
    /// each candidate until one passes the TLS check for the host.
    /// - Returns: the response and the address that answered.
    func sendDoH(_ request: URLRequest) async throws -> ((Data, HTTPURLResponse), String) {
        #if canImport(Network)
        guard let host = request.url?.host?.lowercased() else { throw URLError(.badURL) }
        let timeout = min(request.timeoutInterval, 15)
        if let address = cachedAddress(for: host) {
            do {
                return (try await DirectHTTP.send(request, address: address, timeout: timeout), address)
            } catch is DirectHTTP.Failure {
                forget(host)
            }
        }
        let candidates = await resolver.resolve(host)
        guard !candidates.isEmpty else { throw URLError(.dnsLookupFailed) }
        var lastError: Error = URLError(.cannotConnectToHost)
        for candidate in candidates.prefix(4) {
            do {
                let result = try await DirectHTTP.send(request, address: candidate.address, timeout: timeout)
                remember(candidate.address, for: host)
                return (result, candidate.address)
            } catch let error as DirectHTTP.Failure {
                lastError = error
            }
        }
        throw lastError
        #else
        throw URLError(.unsupportedURL)
        #endif
    }

    private func cachedAddress(for host: String) -> String? {
        lock.lock()
        defer { lock.unlock() }
        guard let entry = verified[host], entry.until > Date() else { return nil }
        return entry.address
    }

    private func remember(_ address: String, for host: String) {
        lock.lock()
        verified[host] = (address, Date().addingTimeInterval(30 * 60))
        lock.unlock()
    }

    private func forget(_ host: String) {
        lock.lock()
        verified[host] = nil
        lock.unlock()
    }

    /// Failures another route or domain might get past. An offline device
    /// or an HTTP error answer is not one of them.
    static func isConnectionFailure(_ error: Error) -> Bool {
        #if canImport(Network)
        if error is DirectHTTP.Failure { return true }
        #endif
        guard let error = error as? URLError else { return false }
        switch error.code {
        case .timedOut, .cannotFindHost, .cannotConnectToHost, .dnsLookupFailed,
             .networkConnectionLost, .secureConnectionFailed, .serverCertificateUntrusted,
             .serverCertificateHasBadDate, .serverCertificateHasUnknownRoot,
             .serverCertificateNotYetValid, .cannotLoadFromNetwork, .badServerResponse:
            return true
        default:
            return false
        }
    }

    static func describe(_ error: Error) -> String {
        if let error = error as? URLError { return "URLError \(error.code.rawValue)" }
        return String(describing: error)
    }

    // MARK: Connection test

    public struct Probe: Sendable, Identifiable {
        public enum Outcome: Sendable, Equatable {
            /// The host answered over HTTPS.
            case reachable(status: Int)
            /// It answered with Cloudflare's browser check, which an app
            /// can't pass.
            case challenged
            case failed(String)
        }

        public var host: String
        public var route: BangumiRoute
        public var outcome: Outcome
        public var milliseconds: Int
        /// The address the built-in resolver connected to.
        public var address: String?

        public var id: String { "\(host)|\(route.rawValue)" }

        public var isUsable: Bool {
            if case .reachable = outcome { return true }
            return false
        }
    }

    /// Opens one host's front page over one route.
    public func probe(_ host: String, route: BangumiRoute) async -> Probe {
        var request = URLRequest(url: URL(string: "https://\(host)/")!, timeoutInterval: 10)
        request.setValue(Self.browserUserAgent, forHTTPHeaderField: "User-Agent")
        let start = Date()
        var address: String?
        let outcome: Probe.Outcome
        do {
            let response: HTTPURLResponse
            switch route {
            case .system:
                response = try await sendSystem(request).1
            case .doh:
                let result = try await sendDoH(request)
                response = result.0.1
                address = result.1
            }
            if response.value(forHTTPHeaderField: "cf-mitigated")?.lowercased() == "challenge" {
                outcome = .challenged
            } else {
                outcome = .reachable(status: response.statusCode)
            }
        } catch {
            outcome = .failed(Self.failureText(error))
        }
        return Probe(
            host: host,
            route: route,
            outcome: outcome,
            milliseconds: Int(Date().timeIntervalSince(start) * 1000),
            address: address
        )
    }

    /// Tests every host over both routes. In Automatic it then keeps the
    /// fastest route that reaches the API, and the first website domain
    /// that route reaches.
    public func runConnectionTest() async -> [Probe] {
        let probes = await withTaskGroup(of: Probe.self) { group in
            for host in BangumiHosts.tested {
                for route in BangumiRoute.allCases {
                    group.addTask { await self.probe(host, route: route) }
                }
            }
            var all: [Probe] = []
            for await probe in group { all.append(probe) }
            return all
        }
        let best = probes
            .filter { $0.host == BangumiHosts.api && $0.isUsable }
            .min { $0.milliseconds < $1.milliseconds }
        if let best, connectionMode == .automatic {
            learnedRoute = best.route
        }
        let route = connectionMode == .automatic ? best?.route : routes.first
        if webDomain == .automatic, let route,
           let host = BangumiHosts.web.first(where: { host in probes.contains { $0.host == host && $0.route == route && $0.isUsable } }) {
            defaults.set(host, forKey: Self.learnedWebDomainKey)
        }
        onEvent?("bangumi.connectionTest", Dictionary(uniqueKeysWithValues: probes.map {
            ("\($0.host).\($0.route.rawValue)", $0.isUsable ? "\($0.milliseconds)ms" : "\($0.outcome)")
        }))
        return probes.sorted {
            let left = BangumiHosts.tested.firstIndex(of: $0.host) ?? 0
            let right = BangumiHosts.tested.firstIndex(of: $1.host) ?? 0
            return left != right ? left < right : $0.route.rawValue > $1.route.rawValue
        }
    }

    static let browserUserAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"

    static func failureText(_ error: Error) -> String {
        #if canImport(Network)
        if let error = error as? DirectHTTP.Failure {
            switch error {
            case .timedOut: return String(localized: "Timed out")
            case .malformedResponse: return String(localized: "Unreadable response")
            case .connection: return String(localized: "Connection failed")
            }
        }
        #endif
        if let error = error as? URLError {
            switch error.code {
            case .timedOut: return String(localized: "Timed out")
            case .cannotFindHost, .dnsLookupFailed: return String(localized: "Address not found")
            case .secureConnectionFailed, .serverCertificateUntrusted, .serverCertificateHasBadDate,
                 .serverCertificateHasUnknownRoot, .serverCertificateNotYetValid:
                return String(localized: "Secure connection failed")
            case .notConnectedToInternet: return String(localized: "Offline")
            default: return String(localized: "Connection failed")
            }
        }
        return String(localized: "Connection failed")
    }
}

/// Keeps a request's redirect as the response.
private final class NoRedirects: NSObject, URLSessionTaskDelegate {
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
