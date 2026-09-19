import SwiftUI

/// Persisted preferences. Raw values are stable ASCII identifiers: they are
/// written to UserDefaults, passed to the KRKR runtime and recorded in
/// diagnostics, so display text must never be used as the stored value.
struct PlayerSettings: Codable {
    var appearance = Appearance.storageDefault
    var renderer = RendererPreference.storageDefault
    var background = PlayerBackground.storageDefault
    var floatingButton = true
    var idleOpacity = 0.38
    var threeFingerMenu = true
    var performance = false
    var listLayout = false
    var sort = LibrarySort.storageDefault

    init() {}

    /// Hand-written because the synthesized decoder ignores property defaults
    /// and fails on any missing key. Each field falls back independently, so a
    /// preference added in a later version — or one stored value we no longer
    /// recognize — cannot discard the rest of the file.
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let fallback = PlayerSettings()
        func value<T: Decodable>(_ key: CodingKeys, _ default: T) -> T {
            (try? container.decodeIfPresent(T.self, forKey: key)).flatMap { $0 } ?? `default`
        }
        appearance = value(.appearance, fallback.appearance)
        renderer = value(.renderer, fallback.renderer)
        background = value(.background, fallback.background)
        floatingButton = value(.floatingButton, fallback.floatingButton)
        idleOpacity = value(.idleOpacity, fallback.idleOpacity)
        threeFingerMenu = value(.threeFingerMenu, fallback.threeFingerMenu)
        performance = value(.performance, fallback.performance)
        listLayout = value(.listLayout, fallback.listLayout)
        sort = value(.sort, fallback.sort)
    }
}

/// A preference stored by raw value and shown by display name.
protocol StoredSetting: RawRepresentable, Codable, CaseIterable, Hashable
where RawValue == String {
    /// Display text persisted by earlier versions, mapped to the current case.
    static var legacyValues: [String: Self] { get }
    static var storageDefault: Self { get }
    var displayName: String { get }
}

extension StoredSetting {
    /// Accepts current raw values and previously stored display text. An
    /// unrecognized value falls back to the default instead of throwing, so one
    /// stale field cannot discard every other preference in the file.
    static func decodeLenient(from decoder: Decoder) throws -> Self {
        let stored = try decoder.singleValueContainer().decode(String.self)
        return Self(rawValue: stored) ?? legacyValues[stored] ?? storageDefault
    }
}

enum Appearance: String, StoredSetting {
    case system
    case light
    case dark

    static let legacyValues: [String: Self] = [
        "跟随系统": .system, "浅色": .light, "深色": .dark
    ]
    static let storageDefault: Self = .dark

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var displayName: String {
        switch self {
        case .system: return "跟随系统"
        case .light: return "浅色"
        case .dark: return "深色"
        }
    }

    /// `nil` follows the system setting.
    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light: return .light
        case .dark: return .dark
        }
    }
}

/// The raw value is the `-render=` argument handed to the KRKR runtime. The
/// runtime still has an OpenGL ES backend and may report it after a fallback,
/// but it is no longer offered as a preference: a stored `opengl` value is
/// unrecognized and therefore migrates to the default.
enum RendererPreference: String, StoredSetting {
    case metal = "metal"
    case softwareMetal = "software-metal"

    static let legacyValues: [String: Self] = [
        "Metal": .metal,
        // 0.1 development builds labelled the native backend "Metal 原生".
        "Metal 原生": .metal,
        "软件合成 · Metal": .softwareMetal
    ]
    static let storageDefault: Self = .metal

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var displayName: String {
        switch self {
        case .metal: return "Metal"
        case .softwareMetal: return "软件合成 · Metal"
        }
    }
}

enum PlayerBackground: String, StoredSetting {
    case cover
    case black

    static let legacyValues: [String: Self] = ["游戏封面": .cover, "纯黑": .black]
    static let storageDefault: Self = .cover

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var displayName: String {
        switch self {
        case .cover: return "游戏封面"
        case .black: return "纯黑"
        }
    }
}

enum LibrarySort: String, StoredSetting {
    case recentlyPlayed = "recently-played"
    case dateAdded = "date-added"
    case title
    case size

    static let legacyValues: [String: Self] = [
        "最近游玩": .recentlyPlayed, "添加时间": .dateAdded,
        "名称": .title, "大小": .size
    ]
    static let storageDefault: Self = .recentlyPlayed

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var displayName: String {
        switch self {
        case .recentlyPlayed: return "最近游玩"
        case .dateAdded: return "添加时间"
        case .title: return "名称"
        case .size: return "大小"
        }
    }
}
