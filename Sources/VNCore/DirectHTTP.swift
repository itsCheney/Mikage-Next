import Foundation
#if canImport(Network)
import Network
import Security

/// A minimal HTTP/1.1 client that connects to a given IP address while
/// still speaking TLS for the real host name: the host goes in SNI and the
/// certificate must be valid for it. URLSession can't do this, because it
/// always resolves the URL's host itself. One request per connection, with
/// `Connection: close`, and redirects are returned rather than followed.
enum DirectHTTP {
    enum Failure: Error, LocalizedError {
        case connection(String)
        case timedOut
        case malformedResponse

        var errorDescription: String? {
            switch self {
            case .connection(let reason): return reason
            case .timedOut: return "The connection timed out."
            case .malformedResponse: return "The server's response couldn't be read."
            }
        }
    }

    static func send(_ request: URLRequest, address: String, timeout: TimeInterval) async throws -> (Data, HTTPURLResponse) {
        guard let url = request.url, let host = url.host, url.scheme == "https" else {
            throw Failure.connection("Only HTTPS URLs can use the built-in DNS.")
        }
        let exchange = Exchange(
            host: host,
            address: address,
            port: UInt16(url.port ?? 443),
            payload: requestBytes(request, url: url, host: host),
            url: url,
            timeout: timeout
        )
        return try await withTaskCancellationHandler {
            try await exchange.run()
        } onCancel: {
            exchange.cancel()
        }
    }

    static func requestBytes(_ request: URLRequest, url: URL, host: String) -> Data {
        var target = url.path.isEmpty ? "/" : URLComponents(url: url, resolvingAgainstBaseURL: false)?.percentEncodedPath ?? url.path
        if target.isEmpty { target = "/" }
        if let query = url.query { target += "?" + query }
        let method = request.httpMethod ?? "GET"
        let body = request.httpBody ?? Data()
        var head = "\(method) \(target) HTTP/1.1\r\nHost: \(url.port.map { "\(host):\($0)" } ?? host)\r\n"
        let skipped: Set<String> = ["host", "connection", "content-length", "accept-encoding", "transfer-encoding"]
        for (name, value) in (request.allHTTPHeaderFields ?? [:]).sorted(by: { $0.key < $1.key })
        where !skipped.contains(name.lowercased()) {
            head += "\(name): \(value)\r\n"
        }
        if !body.isEmpty || ["POST", "PUT", "PATCH"].contains(method) {
            head += "Content-Length: \(body.count)\r\n"
        }
        head += "Accept-Encoding: gzip\r\nConnection: close\r\n\r\n"
        return Data(head.utf8) + body
    }

    /// One connection's lifetime. Every callback runs on `queue`, so the
    /// mutable state needs no further locking.
    private final class Exchange: @unchecked Sendable {
        private let host: String
        private let payload: Data
        private let url: URL
        private let timeout: TimeInterval
        private let queue = DispatchQueue(label: "moe.cheney233.mikage.direct-http")
        private let connection: NWConnection
        private var buffer = Data()
        private var continuation: CheckedContinuation<(Data, HTTPURLResponse), Error>?
        private var finished = false

        init(host: String, address: String, port: UInt16, payload: Data, url: URL, timeout: TimeInterval) {
            self.host = host
            self.payload = payload
            self.url = url
            self.timeout = timeout

            let tls = NWProtocolTLS.Options()
            let security = tls.securityProtocolOptions
            sec_protocol_options_set_tls_server_name(security, host)
            sec_protocol_options_add_tls_application_protocol(security, "http/1.1")
            let verifyQueue = DispatchQueue(label: "moe.cheney233.mikage.direct-http.verify")
            sec_protocol_options_set_verify_block(security, { _, trust, complete in
                // The certificate must name the host we meant, not the
                // address we dialled.
                let secTrust = sec_trust_copy_ref(trust).takeRetainedValue()
                SecTrustSetPolicies(secTrust, SecPolicyCreateSSL(true, host as CFString))
                SecTrustEvaluateAsyncWithError(secTrust, verifyQueue) { _, trusted, _ in
                    complete(trusted)
                }
            }, verifyQueue)
            let tcp = NWProtocolTCP.Options()
            tcp.connectionTimeout = max(1, Int(timeout.rounded(.up)))
            let parameters = NWParameters(tls: tls, tcp: tcp)
            connection = NWConnection(
                host: NWEndpoint.Host(address),
                port: NWEndpoint.Port(rawValue: port) ?? 443,
                using: parameters
            )
        }

        func run() async throws -> (Data, HTTPURLResponse) {
            try await withCheckedThrowingContinuation { continuation in
                queue.async {
                    // Cancelled before it began.
                    guard !self.finished else {
                        continuation.resume(throwing: CancellationError())
                        return
                    }
                    self.continuation = continuation
                    self.start()
                }
            }
        }

        func cancel() {
            queue.async { self.finish(.failure(CancellationError())) }
        }

        private func start() {
            connection.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                switch state {
                case .ready:
                    self.connection.send(content: self.payload, completion: .contentProcessed { error in
                        if let error {
                            self.finish(.failure(Failure.connection(error.localizedDescription)))
                        } else {
                            self.receive()
                        }
                    })
                case .waiting(let error), .failed(let error):
                    // Waiting means the path can't be used right now; for a
                    // probe that is as good as a failure.
                    self.finish(.failure(Failure.connection(error.localizedDescription)))
                default:
                    break
                }
            }
            connection.start(queue: queue)
            queue.asyncAfter(deadline: .now() + timeout) { [weak self] in
                self?.finish(.failure(Failure.timedOut))
            }
        }

        private func receive() {
            connection.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
                guard let self, !self.finished else { return }
                if let data { self.buffer.append(data) }
                switch DirectHTTP.parse(self.buffer, url: self.url, complete: isComplete || error != nil) {
                case .done(let body, let response):
                    self.finish(.success((body, response)))
                case .failed:
                    self.finish(.failure(Failure.malformedResponse))
                case .needMore:
                    if let error {
                        self.finish(.failure(Failure.connection(error.localizedDescription)))
                    } else if isComplete {
                        self.finish(.failure(Failure.malformedResponse))
                    } else {
                        self.receive()
                    }
                }
            }
        }

        private func finish(_ result: Result<(Data, HTTPURLResponse), Error>) {
            guard !finished else { return }
            finished = true
            connection.stateUpdateHandler = nil
            connection.cancel()
            continuation?.resume(with: result)
            continuation = nil
        }
    }

    enum ParseResult {
        case needMore
        case failed
        case done(Data, HTTPURLResponse)
    }

    /// Reads a response from the bytes received so far. `complete` means
    /// the server closed the connection, which ends a body that has no
    /// length.
    static func parse(_ buffer: Data, url: URL, complete: Bool) -> ParseResult {
        let separator = Data("\r\n\r\n".utf8)
        guard let headEnd = buffer.range(of: separator) else { return complete ? .failed : .needMore }
        let head = String(decoding: buffer[buffer.startIndex..<headEnd.lowerBound], as: UTF8.self)
        var lines = head.components(separatedBy: "\r\n")
        let statusLine = lines.removeFirst().split(separator: " ", maxSplits: 2)
        guard statusLine.count >= 2, statusLine[0].hasPrefix("HTTP/"), let status = Int(statusLine[1]) else { return .failed }

        var fields: [String: String] = [:]
        var lowercased: [String: String] = [:]
        for line in lines {
            guard let colon = line.firstIndex(of: ":") else { continue }
            let name = String(line[..<colon]).trimmingCharacters(in: .whitespaces)
            let value = String(line[line.index(after: colon)...]).trimmingCharacters(in: .whitespaces)
            // Repeated headers (Set-Cookie above all) are joined the way
            // URLSession joins them, which HTTPCookie reads back.
            let key = fields.keys.first { $0.lowercased() == name.lowercased() } ?? name
            fields[key] = fields[key].map { "\($0), \(value)" } ?? value
            lowercased[name.lowercased()] = fields[key]
        }

        let rest = buffer[headEnd.upperBound...]
        var body: Data
        if status == 204 || status == 304 || (100..<200).contains(status) {
            body = Data()
        } else if lowercased["transfer-encoding"]?.lowercased().contains("chunked") == true {
            switch dechunk(Data(rest)) {
            case .some(let data): body = data
            case .none: return complete ? .failed : .needMore
            }
        } else if let length = lowercased["content-length"].flatMap({ Int($0) }) {
            guard rest.count >= length else { return complete ? .failed : .needMore }
            body = Data(rest.prefix(length))
        } else {
            guard complete else { return .needMore }
            body = Data(rest)
        }

        if lowercased["content-encoding"]?.lowercased() == "gzip" {
            guard let inflated = gunzip(body) else { return .failed }
            body = inflated
            fields = fields.filter { $0.key.lowercased() != "content-encoding" }
        }
        guard let response = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1", headerFields: fields) else {
            return .failed
        }
        return .done(body, response)
    }

    /// A chunked body, or nil until its last chunk has arrived.
    static func dechunk(_ data: Data) -> Data? {
        var output = Data()
        var index = data.startIndex
        let crlf = Data("\r\n".utf8)
        while true {
            guard let lineEnd = data.range(of: crlf, in: index..<data.endIndex) else { return nil }
            let sizeText = String(decoding: data[index..<lineEnd.lowerBound], as: UTF8.self)
                .split(separator: ";").first.map(String.init)?
                .trimmingCharacters(in: .whitespaces) ?? ""
            guard let size = Int(sizeText, radix: 16) else { return nil }
            let start = lineEnd.upperBound
            if size == 0 { return output }
            guard data.distance(from: start, to: data.endIndex) >= size + 2 else { return nil }
            let end = data.index(start, offsetBy: size)
            output.append(data[start..<end])
            index = data.index(end, offsetBy: 2)
        }
    }

    /// A gzip member's contents. The header is skipped by hand, then the
    /// raw deflate stream goes to Foundation's zlib decoder.
    static func gunzip(_ data: Data) -> Data? {
        let bytes = [UInt8](data)
        guard bytes.count > 18, bytes[0] == 0x1f, bytes[1] == 0x8b, bytes[2] == 8 else { return nil }
        let flags = bytes[3]
        var offset = 10
        if flags & 0x04 != 0 {
            guard bytes.count > offset + 2 else { return nil }
            offset += 2 + Int(bytes[offset]) + Int(bytes[offset + 1]) << 8
        }
        if flags & 0x08 != 0 {
            while offset < bytes.count, bytes[offset] != 0 { offset += 1 }
            offset += 1
        }
        if flags & 0x10 != 0 {
            while offset < bytes.count, bytes[offset] != 0 { offset += 1 }
            offset += 1
        }
        if flags & 0x02 != 0 { offset += 2 }
        guard offset < bytes.count - 8 else { return nil }
        let deflated = Data(bytes[offset..<(bytes.count - 8)])
        return try? (deflated as NSData).decompressed(using: .zlib) as Data
    }
}
#endif
