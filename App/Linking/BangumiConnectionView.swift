import SwiftUI
import VNCore

/// Settings › Accounts & Sync › Bangumi Connection: how Mikage reaches
/// Bangumi where its domains are blocked or their DNS is poisoned, and a
/// test of every host over both routes.
struct BangumiConnectionView: View {
    @AppStorage(BangumiNetwork.connectionModeKey) private var mode = BangumiConnectionMode.automatic.rawValue
    @AppStorage(BangumiNetwork.webDomainKey) private var domain = BangumiWebDomain.automatic.rawValue
    @State private var probes: [BangumiNetwork.Probe] = []
    @State private var testing = false
    @State private var inUse = ""

    var body: some View {
        Form {
            Section {
                Picker("DNS", selection: $mode) {
                    ForEach(BangumiConnectionMode.allCases, id: \.self) {
                        Text($0.displayName).tag($0.rawValue)
                    }
                }
                .pickerStyle(.menu)
                Picker("Website", selection: $domain) {
                    ForEach(BangumiWebDomain.allCases, id: \.self) {
                        Text($0.displayName).tag($0.rawValue)
                    }
                }
                .pickerStyle(.menu)
            } footer: {
                Text("Where Bangumi can’t be reached, Mikage can look up its addresses over HTTPS instead of through your network’s DNS, and sign in on bangumi.tv when bgm.tv is blocked. Automatic tries both and keeps what works.")
            }

            Section {
                Button {
                    Task { await test() }
                } label: {
                    HStack {
                        Text("Test Connection")
                        Spacer()
                        if testing { ProgressView() }
                    }
                }
                .disabled(testing)
                ForEach(BangumiHosts.tested, id: \.self) { host in
                    let results = probes.filter { $0.host == host }
                    if !results.isEmpty {
                        hostRow(host, results)
                    }
                }
            } footer: {
                if !inUse.isEmpty {
                    Text(inUse)
                }
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Bangumi Connection")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear(perform: describeRoute)
        .onChange(of: mode) { _ in describeRoute() }
        .onChange(of: domain) { _ in describeRoute() }
    }

    private func hostRow(_ host: String, _ results: [BangumiNetwork.Probe]) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(verbatim: host)
                .font(.body.monospaced())
            ForEach(BangumiRoute.allCases, id: \.self) { route in
                if let probe = results.first(where: { $0.route == route }) {
                    HStack(spacing: 8) {
                        Image(systemName: probe.isUsable ? "checkmark.circle.fill" : "xmark.circle.fill")
                            .foregroundStyle(probe.isUsable ? .green : .red)
                        Text(routeName(route))
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text(outcomeText(probe))
                            .multilineTextAlignment(.trailing)
                    }
                    .font(.footnote)
                }
            }
        }
        .padding(.vertical, 4)
        .accessibilityElement(children: .combine)
    }

    private func routeName(_ route: BangumiRoute) -> String {
        switch route {
        case .system: return String(localized: "System DNS")
        case .doh: return String(localized: "DNS over HTTPS")
        }
    }

    private func outcomeText(_ probe: BangumiNetwork.Probe) -> String {
        switch probe.outcome {
        case .reachable:
            if let address = probe.address {
                return String(localized: "\(probe.milliseconds) ms · \(address)")
            }
            return String(localized: "\(probe.milliseconds) ms")
        case .challenged:
            return String(localized: "Browser check only")
        case .failed(let reason):
            return reason
        }
    }

    private func test() async {
        testing = true
        AppDiagnostics.shared.event("network", "bangumi.connectionTest.begin")
        probes = await BangumiNetwork.shared.runConnectionTest()
        testing = false
        describeRoute()
    }

    private func describeRoute() {
        let network = BangumiNetwork.shared
        let route = network.routes.first == .doh ? String(localized: "DNS over HTTPS") : String(localized: "System DNS")
        inUse = String(localized: "Now using \(route) and \(network.webHost).")
    }
}
