import SwiftUI
import UIKit

/// A command the game menu or a quick action sends to the engine. The raw
/// values are the engines' own command numbers (MikageONSCommand,
/// MikageRenPyCommand, siglus_mikage_command...).
enum GameCommand: Int32 {
    /// The game's own auto mode.
    case auto = 1
    /// The game's own skip, which follows its read-skip setting.
    case skip = 2
    case quickSave = 3
    case quickLoad = 4
    case backlog = 5
    case save = 6
    case load = 7
}

/// What the engine reports about the game's controls. The bits match the
/// engines' control enums.
struct GameControlState: OptionSet, Equatable {
    let rawValue: UInt32

    static let auto = Self(rawValue: 1 << 0)
    static let skip = Self(rawValue: 1 << 1)
    static let canAuto = Self(rawValue: 1 << 8)
    static let canSkip = Self(rawValue: 1 << 9)
    static let canQuickSave = Self(rawValue: 1 << 10)
    static let canQuickLoad = Self(rawValue: 1 << 11)
    static let canBacklog = Self(rawValue: 1 << 12)
    static let canSave = Self(rawValue: 1 << 13)
    static let canLoad = Self(rawValue: 1 << 14)

    func allows(_ command: GameCommand) -> Bool {
        switch command {
        case .auto: return contains(.canAuto)
        case .skip: return contains(.canSkip)
        case .quickSave: return contains(.canQuickSave)
        case .quickLoad: return contains(.canQuickLoad)
        case .backlog: return contains(.canBacklog)
        case .save: return contains(.canSave)
        case .load: return contains(.canLoad)
        }
    }
}

/// The host's trackpad cursor, as the engines' pointer phases.
enum PointerPhase: Int32 {
    case move = 0
    case down = 1
    case up = 2
    case secondaryDown = 3
    case secondaryUp = 4
}

/// How the overlay reaches one engine. Every closure runs on the main
/// thread between engine steps.
struct GameControls {
    /// The engine's own command; false when the game can't run it now.
    var run: (GameCommand) -> Bool = { _ in false }
    /// Asked again twice a second while the menu shows: some engines
    /// (Ren'Py) answer with the state as of their last step.
    var state: () -> GameControlState = { [] }
    /// Holds or releases Ctrl: a skip through read and unread text alike.
    var setControlSkip: (Bool) -> Bool = { _ in false }
    /// A mouse at a point in the engine window, in points.
    var sendPointer: (PointerPhase, CGPoint) -> Bool = { _, _ in false }
    /// What the engine is waiting on, for the diagnostics log.
    var describe: () -> String = { "" }
}

/// A floating button that runs one action with a tap.
enum QuickActionKind: String, StoredSetting, Identifiable {
    case quickSave = "quick-save"
    case quickLoad = "quick-load"
    case screenshot
    case skip
    case auto
    case backlog
    case mouse
    case stats
    case back

    static let legacyValues: [String: Self] = [:]
    static let storageDefault: Self = .quickSave
    /// Up to this many show at once.
    static let limit = 2
    static let defaults: [Self] = [.quickSave, .skip]

    init(from decoder: Decoder) throws { self = try Self.decodeLenient(from: decoder) }

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .quickSave: return String(localized: "Quick Save")
        case .quickLoad: return String(localized: "Quick Load")
        case .screenshot: return String(localized: "Screenshot")
        case .skip: return String(localized: "Skip")
        case .auto: return String(localized: "Auto")
        case .backlog: return String(localized: "Backlog")
        case .mouse: return String(localized: "Mouse")
        case .stats: return String(localized: "Stats")
        case .back: return String(localized: "Back (Esc)")
        }
    }

    var symbol: String {
        switch self {
        case .quickSave: return "bolt.fill"
        case .quickLoad: return "arrow.counterclockwise"
        case .screenshot: return "camera"
        case .skip: return "forward.fill"
        case .auto: return "play.fill"
        case .backlog: return "text.alignleft"
        case .mouse: return "cursorarrow"
        case .stats: return "gauge.with.dots.needle.33percent"
        case .back: return "arrow.uturn.backward"
        }
    }
}

/// When the last quick save of each game was written, for the menu's
/// Quick Load tile.
enum QuickSaveTimes {
    private static let key = "quickSave.times"

    static func date(for game: String) -> Date? {
        (UserDefaults.standard.dictionary(forKey: key)?[game] as? Double).map(Date.init(timeIntervalSince1970:))
    }

    static func record(_ date: Date, for game: String) {
        var times = UserDefaults.standard.dictionary(forKey: key) ?? [:]
        times[game] = date.timeIntervalSince1970
        UserDefaults.standard.set(times, forKey: key)
    }
}

/// The overlay's settings for one session.
struct GameOverlayOptions {
    var floatingButton = true
    var idleOpacity = 0.38
    var threeFingerMenu = true
    var quickActions: [QuickActionKind] = QuickActionKind.defaults

    init(configuration: KRKRLaunchConfiguration) {
        floatingButton = configuration.floatingButton
        idleOpacity = configuration.idleOpacity
        threeFingerMenu = configuration.threeFingerMenu
        quickActions = configuration.quickActions
    }
}
