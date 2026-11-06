import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// Looks up a host's IPv4 addresses over DNS over HTTPS, so a poisoned
/// local resolver can't hand Mikage a wrong address. Several public
/// resolvers are asked at once, since which of them a network can reach
/// differs by region. Answers are only candidates: the caller connects
/// with TLS for the real host name, so a wrong address fails the
/// certificate check instead of being trusted.
public struct DoHResolver: Sendable {
    public struct Server: Sendable, Equatable {
        public var name: String
        public var url: URL

        public init(name: String, url: URL) {
            self.name = name
            self.url = url
        }
    }

    /// All of them answer the JSON form (`application/dns-json`).
    /// Cloudflare and Google are addressed by IP, which their certificates
    /// cover, so their own names never need the local resolver.
    public static let servers: [Server] = [
        Server(name: "AliDNS", url: URL(string: "https://dns.alidns.com/resolve")!),
        Server(name: "DNSPod", url: URL(string: "https://doh.pub/dns-query")!),
        Server(name: "360", url: URL(string: "https://doh.360.cn/resolve")!),
        Server(name: "Cloudflare", url: URL(string: "https://1.1.1.1/dns-query")!),
        Server(name: "Google", url: URL(string: "https://8.8.8.8/resolve")!)
    ]

    /// One address and the resolvers that returned it.
    public struct Candidate: Sendable, Equatable {
        public var address: String
        public var servers: [String]
    }

    private let servers: [Server]
    private let session: URLSession
    private let timeout: TimeInterval

    public init(servers: [Server] = DoHResolver.servers, timeout: TimeInterval = 5) {
        self.servers = servers
        self.timeout = timeout
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = timeout
        configuration.timeoutIntervalForResource = timeout
        configuration.httpCookieAcceptPolicy = .never
        configuration.httpShouldSetCookies = false
        session = URLSession(configuration: configuration)
    }

    /// The host's addresses, those most resolvers agree on first. Poisoned
    /// answers tend to differ from query to query, while the real ones
    /// repeat, so agreement is a cheap first guess.
    public func resolve(_ host: String) async -> [Candidate] {
        let answers = await withTaskGroup(of: (String, [String]).self) { group in
            for server in servers {
                group.addTask { (server.name, (try? await lookup(host, on: server)) ?? []) }
            }
            var all: [(String, [String])] = []
            for await answer in group { all.append(answer) }
            return all
        }
        return Self.rank(answers, order: servers.map(\.name))
    }

    static func rank(_ answers: [(String, [String])], order: [String]) -> [Candidate] {
        var byAddress: [String: [String]] = [:]
        var firstSeen: [String: Int] = [:]
        let sorted = answers.sorted { (order.firstIndex(of: $0.0) ?? .max) < (order.firstIndex(of: $1.0) ?? .max) }
        var index = 0
        for (server, addresses) in sorted {
            for address in addresses {
                byAddress[address, default: []].append(server)
                if firstSeen[address] == nil {
                    firstSeen[address] = index
                    index += 1
                }
            }
        }
        return byAddress
            .map { Candidate(address: $0.key, servers: $0.value) }
            .sorted {
                $0.servers.count != $1.servers.count
                    ? $0.servers.count > $1.servers.count
                    : firstSeen[$0.address]! < firstSeen[$1.address]!
            }
    }

    func lookup(_ host: String, on server: Server) async throws -> [String] {
        var components = URLComponents(url: server.url, resolvingAgainstBaseURL: false)!
        components.queryItems = [URLQueryItem(name: "name", value: host), URLQueryItem(name: "type", value: "A")]
        var request = URLRequest(url: components.url!, timeoutInterval: timeout)
        request.setValue("application/dns-json", forHTTPHeaderField: "Accept")
        let (data, response) = try await session.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else { return [] }
        return Self.addresses(in: data)
    }

    /// The A records of a JSON DNS answer. CNAME steps are skipped, and
    /// anything that isn't a plain dotted IPv4 address is ignored.
    static func addresses(in data: Data) -> [String] {
        struct Answer: Decodable {
            var type: Int
            var data: String
        }
        struct Response: Decodable {
            var Status: Int
            var Answer: [Answer]?
        }
        guard let response = try? JSONDecoder().decode(Response.self, from: data), response.Status == 0 else { return [] }
        return (response.Answer ?? []).compactMap { answer in
            guard answer.type == 1 else { return nil }
            let parts = answer.data.split(separator: ".")
            guard parts.count == 4, parts.allSatisfy({ UInt8($0) != nil }) else { return nil }
            return answer.data
        }
    }
}
