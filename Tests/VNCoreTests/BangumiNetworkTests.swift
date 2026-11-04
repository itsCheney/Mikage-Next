import XCTest
@testable import VNCore

final class BangumiNetworkTests: XCTestCase {
    private func makeNetwork() -> (BangumiNetwork, UserDefaults) {
        let name = "BangumiNetworkTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: name)!
        defaults.removePersistentDomain(forName: name)
        return (BangumiNetwork(defaults: defaults), defaults)
    }

    // MARK: Settings

    func testDefaultsUseBgmTvOverTheSystemResolverFirst() {
        let (network, _) = makeNetwork()
        XCTAssertEqual(network.connectionMode, .automatic)
        XCTAssertEqual(network.webHost, "bgm.tv")
        XCTAssertEqual(network.routes, [.system, .doh])
        XCTAssertEqual(network.webURL("login").absoluteString, "https://bgm.tv/login")
    }

    func testAutomaticTriesTheLearnedRouteFirst() {
        let (network, _) = makeNetwork()
        network.learnedRoute = .doh
        XCTAssertEqual(network.routes, [.doh, .system])
        network.connectionMode = .system
        XCTAssertEqual(network.routes, [.system])
        network.connectionMode = .doh
        XCTAssertEqual(network.routes, [.doh])
    }

    func testChosenWebDomainWinsOverTheLearnedOne() {
        let (network, defaults) = makeNetwork()
        defaults.set("bangumi.tv", forKey: BangumiNetwork.learnedWebDomainKey)
        XCTAssertEqual(network.webHost, "bangumi.tv")
        network.webDomain = .bgm
        XCTAssertEqual(network.webHost, "bgm.tv")
        // Only the known website domains are ever learned.
        network.webDomain = .automatic
        defaults.set("chii.in", forKey: BangumiNetwork.learnedWebDomainKey)
        XCTAssertEqual(network.webHost, "bgm.tv")
    }

    func testBangumiHostsAreRecognised() {
        for host in ["bgm.tv", "api.bgm.tv", "lain.bgm.tv", "BANGUMI.TV", "chii.in"] {
            XCTAssertTrue(BangumiHosts.isBangumi(host), host)
        }
        for host in ["vndb.org", "notbgm.tv", "bgm.tv.example.com"] {
            XCTAssertFalse(BangumiHosts.isBangumi(host), host)
        }
    }

    func testMovingARequestToTheOtherDomainKeepsEverythingElse() {
        var request = URLRequest(url: URL(string: "https://bgm.tv/signup/captcha?123")!)
        request.httpMethod = "POST"
        request.setValue("https://bgm.tv/login", forHTTPHeaderField: "Referer")
        request.setValue("a=b", forHTTPHeaderField: "Cookie")
        let moved = BangumiNetwork.request(request, host: "bangumi.tv")
        XCTAssertEqual(moved.url?.absoluteString, "https://bangumi.tv/signup/captcha?123")
        XCTAssertEqual(moved.value(forHTTPHeaderField: "Referer"), "https://bangumi.tv/login")
        XCTAssertEqual(moved.value(forHTTPHeaderField: "Cookie"), "a=b")
        XCTAssertEqual(moved.httpMethod, "POST")
    }

    func testOnlyConnectionFailuresMoveToAnotherRoute() {
        XCTAssertTrue(BangumiNetwork.isConnectionFailure(URLError(.timedOut)))
        XCTAssertTrue(BangumiNetwork.isConnectionFailure(URLError(.cannotFindHost)))
        XCTAssertTrue(BangumiNetwork.isConnectionFailure(URLError(.secureConnectionFailed)))
        XCTAssertFalse(BangumiNetwork.isConnectionFailure(URLError(.notConnectedToInternet)))
        XCTAssertFalse(BangumiNetwork.isConnectionFailure(URLError(.cancelled)))
        XCTAssertFalse(BangumiNetwork.isConnectionFailure(MetadataError.unauthorized(.bangumi)))
    }

    // MARK: DNS over HTTPS

    func testJSONAnswersKeepOnlyIPv4Records() {
        let json = """
        {"Status":0,"Answer":[
          {"name":"bgm.tv.","type":5,"TTL":60,"data":"bgm.tv.cdn.cloudflare.net."},
          {"name":"bgm.tv.","type":1,"TTL":60,"data":"104.26.8.23"},
          {"name":"bgm.tv.","type":1,"TTL":60,"data":"not.an.ip.addr"},
          {"name":"bgm.tv.","type":1,"TTL":60,"data":"172.67.73.67"}
        ]}
        """
        XCTAssertEqual(DoHResolver.addresses(in: Data(json.utf8)), ["104.26.8.23", "172.67.73.67"])
        XCTAssertEqual(DoHResolver.addresses(in: Data(#"{"Status":2}"#.utf8)), [])
        XCTAssertEqual(DoHResolver.addresses(in: Data("<html>".utf8)), [])
    }

    func testAddressesMostResolversAgreeOnComeFirst() {
        let ranked = DoHResolver.rank([
            ("AliDNS", ["31.13.94.10"]),
            ("Cloudflare", ["104.26.8.23", "104.26.9.23"]),
            ("Google", ["104.26.9.23", "104.26.8.23"]),
            ("DNSPod", ["104.26.8.23"])
        ], order: DoHResolver.servers.map(\.name))
        XCTAssertEqual(ranked.map(\.address), ["104.26.8.23", "104.26.9.23", "31.13.94.10"])
        XCTAssertEqual(ranked.first?.servers.sorted(), ["Cloudflare", "DNSPod", "Google"])
    }

    // MARK: Direct HTTP

    #if canImport(Network)
    func testRequestBytesCarryTheHostAndBody() {
        var request = URLRequest(url: URL(string: "https://api.bgm.tv/v0/search/subjects?limit=8")!)
        request.httpMethod = "POST"
        request.httpBody = Data("{}".utf8)
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let text = String(decoding: DirectHTTP.requestBytes(request, url: request.url!, host: "api.bgm.tv"), as: UTF8.self)
        XCTAssertTrue(text.hasPrefix("POST /v0/search/subjects?limit=8 HTTP/1.1\r\nHost: api.bgm.tv\r\n"))
        XCTAssertTrue(text.contains("Content-Type: application/json\r\n"))
        XCTAssertTrue(text.contains("Content-Length: 2\r\n"))
        XCTAssertTrue(text.contains("Connection: close\r\n"))
        XCTAssertTrue(text.hasSuffix("\r\n\r\n{}"))
    }

    func testResponsesWithALengthWaitForTheWholeBody() {
        let url = URL(string: "https://bgm.tv/")!
        let partial = Data("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\nhel".utf8)
        guard case .needMore = DirectHTTP.parse(partial, url: url, complete: false) else {
            return XCTFail("a short body should wait")
        }
        guard case .done(let body, let response) = DirectHTTP.parse(partial + Data("lo".utf8), url: url, complete: false) else {
            return XCTFail("the whole body should parse")
        }
        XCTAssertEqual(String(decoding: body, as: UTF8.self), "hello")
        XCTAssertEqual(response.statusCode, 200)
        let cookies = HTTPCookie.cookies(withResponseHeaderFields: response.allHeaderFields as! [String: String], for: url)
        XCTAssertEqual(Set(cookies.map(\.name)), ["a", "b"])
    }

    func testChunkedAndGzippedBodiesAreDecoded() {
        let url = URL(string: "https://api.bgm.tv/")!
        let chunked = Data("HTTP/1.1 302 Found\r\nLocation: /next\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2;x=y\r\nde\r\n0\r\n\r\n".utf8)
        guard case .done(let body, let response) = DirectHTTP.parse(chunked, url: url, complete: false) else {
            return XCTFail("chunked body should parse")
        }
        XCTAssertEqual(String(decoding: body, as: UTF8.self), "abcde")
        XCTAssertEqual(response.value(forHTTPHeaderField: "Location"), "/next")

        let gzipped = Data(base64Encoded: "H4sIAMtp62oC/6tWys9WsiopKk2tBQCQX9SnCwAAAA==")!
        let head = Data("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: \(gzipped.count)\r\n\r\n".utf8)
        guard case .done(let inflated, let plain) = DirectHTTP.parse(head + gzipped, url: url, complete: false) else {
            return XCTFail("gzip body should parse")
        }
        XCTAssertEqual(String(decoding: inflated, as: UTF8.self), #"{"ok":true}"#)
        XCTAssertNil(plain.value(forHTTPHeaderField: "Content-Encoding"))
    }

    func testABodyWithoutLengthEndsWhenTheServerCloses() {
        let url = URL(string: "https://bgm.tv/")!
        let data = Data("HTTP/1.1 200 OK\r\n\r\npage".utf8)
        guard case .needMore = DirectHTTP.parse(data, url: url, complete: false) else {
            return XCTFail("should wait for the close")
        }
        guard case .done(let body, _) = DirectHTTP.parse(data, url: url, complete: true) else {
            return XCTFail("should end at the close")
        }
        XCTAssertEqual(String(decoding: body, as: UTF8.self), "page")
    }
    #endif
}
