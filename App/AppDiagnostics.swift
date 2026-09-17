import Foundation
import UIKit
import AVFAudio
import KRKRRuntime
import VNCore

final class AppDiagnostics: @unchecked Sendable {
    static let shared = AppDiagnostics()
    static let preferenceKey = "diagnostics.enabled"
    private let log: DiagnosticLog
    private let lock = NSLock()
    private var gameSession = "none"
    private var recording = false
    private var observers: [NSObjectProtocol] = []
    private var suppressScriptDump = false

    var isEnabled: Bool {
        lock.lock(); let active = recording; lock.unlock()
        return active && log.lastError == nil
    }
    var lastError: String? { log.lastError }

    private init() {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        log = DiagnosticLog(directory: support.appendingPathComponent("Mikage/Logs", isDirectory: true))
    }

    @MainActor func configure(enabled: Bool) throws {
        MikageKRKRSetLogCallback(nil)
        try log.setEnabled(enabled)
        lock.lock(); recording = enabled; suppressScriptDump = false; lock.unlock()
        if enabled {
            MikageKRKRSetLogCallback(mikageDiagnosticLogCallback)
            recordEnvironment()
            windows("recording.enabled.windows")
        }
        installObservers()
    }

    @MainActor private func recordEnvironment() {
        event("app", "environment", [
            "version": Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "unknown",
            "build": Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "unknown",
            "sourceRevision": Bundle.main.infoDictionary?["MikageSourceRevision"] as? String ?? "unknown",
            "system": UIDevice.current.systemVersion,
            "device": UIDevice.current.model,
            "screenPoints": NSCoder.string(for: UIScreen.main.bounds),
            "screenScale": String(Double(UIScreen.main.scale)),
            "process": ProcessInfo.processInfo.processName,
            "pid": String(ProcessInfo.processInfo.processIdentifier)
        ])
    }

    func event(_ source: String, _ name: String, _ fields: [String: String] = [:]) {
        lock.lock(); let active = recording; let session = gameSession; lock.unlock()
        guard active, log.lastError == nil else { return }
        var cleaned = fields.mapValues(redact)
        cleaned["gameSession"] = session
        log.record(source, redact(name), fields: cleaned)
    }

    func beginGame(_ fields: [String: String]) {
        lock.lock(); gameSession = UUID().uuidString; suppressScriptDump = false; lock.unlock()
        event("session", "game.begin", fields)
    }

    func endGame() {
        event("session", "game.end")
        log.flush()
        lock.lock(); gameSession = "none"; lock.unlock()
    }

    func runtime(source: String, level: Int32, message: String) {
        // Preserve error headings but omit VM disassembly/register dumps, which
        // can contain game script or saved variable contents.
        lock.lock()
        if source == "KRKR", message.contains("-- Disassembled") || message.contains("-- Register dump") {
            suppressScriptDump = true
        }
        let omit = source == "KRKR" && suppressScriptDump
        if source == "KRKR", message.contains("--------------------") { suppressScriptDump = false }
        lock.unlock()
        guard !omit else { return }
        event(source, "runtime.message", ["level": String(level), "message": message])
    }

    @MainActor func windows(_ name: String) {
        guard isEnabled, Thread.isMainThread else { return }
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        for scene in scenes {
            event("UIKit", name, ["scene": scene.session.persistentIdentifier,
                                  "orientation": String(scene.interfaceOrientation.rawValue),
                                  "activation": String(scene.activationState.rawValue),
                                  "sceneBounds": NSCoder.string(for: scene.coordinateSpace.bounds),
                                  "windowCount": String(scene.windows.count)])
            for (index, window) in scene.windows.enumerated() {
                event("UIKit", name + ".window", [
                    "scene": scene.session.persistentIdentifier, "index": String(index),
                    "identity": String(describing: ObjectIdentifier(window)),
                    "type": String(describing: type(of: window)),
                    "key": String(window.isKeyWindow), "hidden": String(window.isHidden),
                    "alpha": String(Double(window.alpha)), "bounds": NSCoder.string(for: window.bounds),
                    "safeArea": NSCoder.string(for: window.safeAreaInsets),
                    "root": window.rootViewController.map { String(describing: type(of: $0)) } ?? "none",
                    "subviews": window.rootViewController?.viewIfLoaded?.subviews.prefix(8).map {
                        "\(type(of: $0)) frame=\(NSCoder.string(for: $0.frame)) hidden=\($0.isHidden) alpha=\($0.alpha)"
                    }.joined(separator: " | ") ?? "none"
                ])
            }
        }
    }

    func flush() { log.flush() }
    @MainActor func clear() throws {
        try log.clear()
        event("diagnostics", "logs.cleared")
        if isEnabled {
            recordEnvironment()
            windows("logs.cleared.windows")
        }
    }
    func export() throws -> URL {
        event("diagnostics", "export.requested")
        let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let directory = caches.appendingPathComponent("Mikage/LogExports", isDirectory: true)
        // Only remove our previous immutable exports, never game/user files.
        if let old = try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil) {
            for file in old where file.lastPathComponent.hasPrefix("Mikage-diagnostics-") {
                try? FileManager.default.removeItem(at: file)
            }
        }
        return try log.export(to: directory)
    }

    private func redact(_ text: String) -> String {
        var value = text.replacingOccurrences(of: NSHomeDirectory(), with: "<APP>")
        for url in FileManager.default.urls(for: .documentDirectory, in: .userDomainMask) {
            value = value.replacingOccurrences(of: url.path, with: "<Documents>")
        }
        return value
    }

    @MainActor private func installObservers() {
        guard observers.isEmpty else { return }
        let names: [Notification.Name] = [
            UIApplication.willResignActiveNotification, UIApplication.didEnterBackgroundNotification,
            UIApplication.willEnterForegroundNotification, UIApplication.didBecomeActiveNotification,
            UIApplication.willTerminateNotification, UIApplication.didReceiveMemoryWarningNotification,
            UIWindow.didBecomeKeyNotification, UIWindow.didResignKeyNotification,
            UIScene.didActivateNotification, UIScene.willDeactivateNotification,
            UIScene.didEnterBackgroundNotification, UIScene.willEnterForegroundNotification,
            AVAudioSession.interruptionNotification, AVAudioSession.routeChangeNotification,
            ProcessInfo.thermalStateDidChangeNotification
        ]
        for name in names {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] notification in
                guard let self, self.isEnabled else { return }
                let fields = ["thermal": String(ProcessInfo.processInfo.thermalState.rawValue),
                              "audioInterruption": String(describing: notification.userInfo?[AVAudioSessionInterruptionTypeKey]),
                              "audioRouteReason": String(describing: notification.userInfo?[AVAudioSessionRouteChangeReasonKey])]
                self.event("lifecycle", name.rawValue, fields)
                self.flush()
                Task { @MainActor [weak self] in
                    self?.windows("window.inventory")
                }
            })
        }
    }
}

private func mikageDiagnosticLogCallback(
    _ source: UnsafePointer<CChar>?, _ level: Int32, _ message: UnsafePointer<CChar>?
) {
    guard let source, let message else { return }
    AppDiagnostics.shared.runtime(source: String(cString: source), level: level, message: String(cString: message))
}
