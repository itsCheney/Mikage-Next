import Foundation
import Security
import VNCore

/// Where Bangumi and VNDB tokens live.
protocol ServiceTokenStore: AnyObject {
    func token(for service: MetadataService) -> String?
    func setToken(_ token: String, for service: MetadataService) throws
    func removeToken(for service: MetadataService)
}

enum TokenStoreError: LocalizedError {
    case keychain(OSStatus)

    var errorDescription: String? {
        switch self {
        case .keychain(let status):
            return String(localized: "Couldn’t save the token to the Keychain (error \(Int(status))).")
        }
    }
}

/// Tokens are generic passwords in this device's Keychain. They are not
/// synced through iCloud and are readable once the device has been unlocked
/// after a restart, so a background sync can use them.
final class KeychainTokenStore: ServiceTokenStore {
    private let keychainService = "moe.cheney233.mikage.accounts"

    private func query(for service: MetadataService) -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: keychainService,
            kSecAttrAccount as String: service.rawValue
        ]
    }

    func token(for service: MetadataService) -> String? {
        var lookup = query(for: service)
        lookup[kSecReturnData as String] = true
        lookup[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        guard SecItemCopyMatching(lookup as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    func setToken(_ token: String, for service: MetadataService) throws {
        let data = Data(token.utf8)
        let status = SecItemUpdate(
            query(for: service) as CFDictionary,
            [kSecValueData as String: data] as CFDictionary
        )
        if status == errSecItemNotFound {
            var item = query(for: service)
            item[kSecValueData as String] = data
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            let added = SecItemAdd(item as CFDictionary, nil)
            guard added == errSecSuccess else { throw TokenStoreError.keychain(added) }
        } else if status != errSecSuccess {
            throw TokenStoreError.keychain(status)
        }
    }

    func removeToken(for service: MetadataService) {
        SecItemDelete(query(for: service) as CFDictionary)
    }
}

/// Keeps tokens in memory only; used by tests.
final class MemoryTokenStore: ServiceTokenStore {
    private var tokens: [MetadataService: String] = [:]

    init(_ tokens: [MetadataService: String] = [:]) {
        self.tokens = tokens
    }

    func token(for service: MetadataService) -> String? { tokens[service] }
    func setToken(_ token: String, for service: MetadataService) throws { tokens[service] = token }
    func removeToken(for service: MetadataService) { tokens[service] = nil }
}
