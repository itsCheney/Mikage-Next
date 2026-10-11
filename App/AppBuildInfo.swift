import Foundation

/// Bundle metadata shared by the settings footer and diagnostic environment.
/// This value never queries Git, the network, or the current filesystem.
struct AppBuildInfo: Equatable {
    let version: String
    let build: String
    let sourceRevision: String

    init(infoDictionary: [String: Any]?) {
        let info = infoDictionary ?? [:]
        version = Self.metadataString(info["CFBundleShortVersionString"])
        build = Self.metadataString(info["CFBundleVersion"])
        sourceRevision = Self.normalizedRevision(info["MikageSourceRevision"] as? String)
    }

    init(bundle: Bundle = .main) {
        self.init(infoDictionary: bundle.infoDictionary)
    }

    var versionLine: String { "MIKAGE NEXT · \(version)" }
    var headLine: String { sourceRevision == "unknown" ? "HEAD 未知" : "HEAD \(sourceRevision)" }

    private static func metadataString(_ value: Any?) -> String {
        guard let value = value as? String else { return "unknown" }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "unknown" : trimmed
    }

    static func normalizedRevision(_ value: String?) -> String {
        guard let value else { return "unknown" }
        let revision = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard [12, 40, 64].contains(revision.count),
              !revision.allSatisfy({ $0 == "0" }),
              revision.utf8.allSatisfy({ (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0) }) else {
            return "unknown"
        }
        return String(revision.prefix(12)).lowercased()
    }
}
