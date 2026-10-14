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
    /// Widens games narrower than 16:9 (such as 4:3) to 16:9. Off by
    /// default: a 4:3 game stays 4:3. Either way the game is letterboxed.
    var stretchToScreen = false
    /// When enabled, game audio uses iOS ambient semantics and is silenced by
    /// the device Ring/Silent state. Enabled by default for native iOS behavior.
    var respectSilentMode = true
    var performance = false
    var listLayout = false
    var sort = LibrarySort.storageDefault
    /// Skips the built-in list of localization-patch videos. Off by default:
    /// those credits are the patch authors' own work, so not showing them is
    /// the user's choice to make.
    var skipPatchVideos = false
    /// Captured at game launch; existing Emote players keep their current mode.
    var experimentalEmote = false
    /// Sets Playing after a session of a game that had no status or was
    /// on the wishlist or on hold.
    var autoStatus = true
    /// Sends ratings and comments along with the status.
    var syncRatings = true
    /// Records start and finish dates, which go to the VNDB list.
    var syncDates = true
    var titleDisplay = TitleDisplay.storageDefault
    var bannerArt = BannerArt.storageDefault
    var hideSensitiveImages = true

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
        stretchToScreen = value(.stretchToScreen, fallback.stretchToScreen)
        respectSilentMode = value(.respectSilentMode, fallback.respectSilentMode)
        performance = value(.performance, fallback.performance)
        listLayout = value(.listLayout, fallback.listLayout)
        sort = value(.sort, fallback.sort)
        skipPatchVideos = value(.skipPatchVideos, fallback.skipPatchVideos)
        experimentalEmote = value(.experimentalEmote, fallback.experimentalEmote)
        autoStatus = value(.autoStatus, fallback.autoStatus)
        syncRatings = value(.syncRatings, fallback.syncRatings)
        syncDates = value(.syncDates, fallback.syncDates)
        titleDisplay = value(.titleDisplay, fallback.titleDisplay)
        bannerArt = value(.bannerArt, fallback.bannerArt)
        hideSensitiveImages = value(.hideSensitiveImages, fallback.hideSensitiveImages)
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
        case .system: return String(localized: "Automatic")
        case .light: return String(localized: "Light")
        case .dark: return String(localized: "Dark")
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
        case .softwareMetal: return String(localized: "Software Compositing · Metal")
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
        case .cover: return String(localized: "Game Cover")
        case .black: return String(localized: "Black")
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
        case .recentlyPlayed: return String(localized: "Recently Played")
        case .dateAdded: return String(localized: "Date Added")
        case .title: return String(localized: "Name")
        case .size: return String(localized: "Size")
        }
    }
}

/// Which title a linked game shows in the library.
enum TitleDisplay: String, StoredSetting {
    /// Bangumi's Chinese title when the app runs in Chinese, else the original.
    case automatic
    case chinese
    case original
    case romanized
    case folder

    static let legacyValues: [String: Self] = [:]
    static let storageDefault: Self = .automatic

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var displayName: String {
        switch self {
        case .automatic: return String(localized: "Automatic")
        case .chinese: return String(localized: "Bangumi Chinese")
        case .original: return String(localized: "Original Title")
        case .romanized: return String(localized: "Romanized")
        case .folder: return String(localized: "Folder Name")
        }
    }
}

/// The art library cards and the continue hero use for a linked game.
enum BannerArt: String, StoredSetting {
    case screenshot
    case cover

    static let legacyValues: [String: Self] = [:]
    static let storageDefault: Self = .screenshot

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var displayName: String {
        switch self {
        case .screenshot: return String(localized: "VNDB Screenshot")
        case .cover: return String(localized: "Cover")
        }
    }
}
