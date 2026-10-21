import AVFAudio
import CoreText
import Foundation
import QuartzCore
import SiglusRuntime
import UIKit
import VNCore

enum SiglusSessionError: LocalizedError {
    case runtime(String)
    case stopped(String)

    var errorDescription: String? {
        switch self {
        case .runtime(let message):
            return message.isEmpty ? String(localized: "SiglusEngine failed to start.") : String(localized: "SiglusEngine failed to start: \(message)")
        case .stopped(let message):
            return message.isEmpty ? String(localized: "SiglusEngine stopped.") : String(localized: "SiglusEngine stopped: \(message)")
        }
    }
}

/// Runs SiglusEngine games with siglus_rs (SiglusRuntime). It isn't an SDL
/// program: it renders with wgpu straight into a CAMetalLayer, so the session
/// gives it its own landscape window and steps it from a display link. The
/// orientation, lifecycle and menu overlay are the same as the SDL engines'.
@MainActor
final class NativeSiglusSession: NSObject, KRKRSession {
    private static let rendererLabel = "siglus_rs"

    private(set) var state: KRKRSessionState = .idle {
        didSet {
            AppDiagnostics.shared.event("session", "state.changed", ["from": String(describing: oldValue), "to": String(describing: state)])
        }
    }
    var onFinished: ((Result<Void, Error>) -> Void)?
    var onWarning: ((Error) -> Void)?
    var onReturningToLibrary: (() -> Void)?
    var onMarkFinished: (() -> Void)?

    private let presentation = SDLHostPresentation()
    private let frameDriver = SDLHostFrameDriver()
    private var lifecycleObserver: SDLHostLifecycleObserver?
    private var engineWindow: UIWindow?
    private var controller: SiglusViewController?
    private var handle: UnsafeMutableRawPointer?
    private var metricsTimer: Timer?
    private var overlayCoordinator: KRKROverlayCoordinator?
    private var isForeground = true
    private var requestedForeground = true
    private var elapsedSeconds = 0
    private var performanceEnabled = false
    private var framesThisSecond = 0
    private var lastStepTime: CFTimeInterval?
    private static var fontsRegistered = false

    override init() {
        super.init()
        lifecycleObserver = SDLHostLifecycleObserver { [weak self] active in
            self?.setForeground(active)
        }
    }

    func start(configuration: KRKRLaunchConfiguration, in viewController: UIViewController) async throws {
        guard state == .idle || isFailed else { throw KRKRSessionError.alreadyRunning }
        requestedForeground = UIApplication.shared.applicationState == .active
        let gameDirectory = configuration.gameDirectory.standardizedFileURL.resolvingSymlinksInPath()
        var isDirectory = ObjCBool(false)
        guard FileManager.default.fileExists(atPath: gameDirectory.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            throw KRKRSessionError.invalidGameDirectory
        }
        guard let window = viewController.viewIfLoaded?.window,
              let scene = window.windowScene else {
            throw KRKRSessionError.missingHostWindow
        }

        AppDiagnostics.shared.beginGame(["engine": EngineID.siglus.rawValue,
                                         "folder": configuration.gameDirectory.lastPathComponent,
                                         "performanceHUD": String(configuration.performance),
                                         "floatingButton": String(configuration.floatingButton),
                                         "threeFingerMenu": String(configuration.threeFingerMenu),
                                         "respectSilentMode": String(configuration.respectSilentMode)])
        state = .preparingOrientation
        presentation.begin(hostWindow: window, scene: scene)
        do {
            try await presentation.prepareLandscape(scene: scene, window: window, controller: viewController)
        } catch {
            state = .failed(error.localizedDescription)
            await presentation.restorePreviousOrientation()
            throw error
        }

        state = .starting
        await Self.registerSystemFonts()
        SiglusMovieDecoder.register()
        configureAudioSession(respectSilentMode: configuration.respectSilentMode)

        // siglus_rs keeps saves in the game's savedata folder, which is
        // Mikage's saves folder too, so Storage can measure and export them.
        try? FileManager.default.createDirectory(
            at: gameDirectory.appendingPathComponent(LibraryRepository.savesFolderName, isDirectory: true),
            withIntermediateDirectories: true
        )

        // A patched executable picked as the startup file (SiglusEngineCHS.exe
        // and the like) loads renamed copies of Scene.pck and Gameexe.dat.
        let names = configuration.targetKind == .archive
            ? GameScanner.siglusResourceNames(of: configuration.entryPoint, in: gameDirectory)
            : SiglusResourceNames()
        AppDiagnostics.shared.event("bridge", "siglus.resources", ["scene": names.scene ?? "Scene.pck", "gameexe": names.gameexe ?? "Gameexe.dat"])
        gameDirectory.path.withCString { root in
            withOptionalCString(names.scene) { scene in
                withOptionalCString(names.gameexe) { gameexe in
                    siglus_mikage_set_resource_names(root, scene, gameexe)
                }
            }
        }

        let controller = SiglusViewController()
        controller.loadViewIfNeeded()
        let engineWindow = UIWindow(windowScene: scene)
        engineWindow.backgroundColor = .black
        engineWindow.rootViewController = controller
        engineWindow.frame = scene.coordinateSpace.bounds
        engineWindow.isHidden = false
        engineWindow.layoutIfNeeded()

        let metalView = controller.metalView
        let scale = scene.screen.scale
        let drawable = controller.drawablePixels(scale: scale)
        let viewPointer = Unmanaged.passUnretained(metalView).toOpaque()
        AppDiagnostics.shared.event("bridge", "siglus.create", ["drawable": "\(drawable.width)x\(drawable.height)", "scale": String(Double(scale))])
        let created = gameDirectory.path.withCString { game in
            siglus_ios_create(viewPointer, drawable.width, drawable.height, Double(scale), game)
        }
        guard let created else {
            let message = String(cString: siglus_mikage_last_error())
            engineWindow.isHidden = true
            state = .failed(message)
            await presentation.restorePreviousOrientation()
            AppDiagnostics.shared.endGame()
            throw SiglusSessionError.runtime(message)
        }
        handle = created
        self.controller = controller
        self.engineWindow = engineWindow

        var logicalWidth: UInt32 = 0
        var logicalHeight: UInt32 = 0
        siglus_ios_logical_size(created, &logicalWidth, &logicalHeight)
        controller.attach(handle: created, logicalSize: CGSize(width: Int(logicalWidth), height: Int(logicalHeight)))
        controller.layoutEngine(scale: scale)
        controller.onMenuGesture = configuration.threeFingerMenu ? { [weak self] in self?.showMenuOverlay() } : nil
        let context = Unmanaged.passUnretained(self).toOpaque()
        siglus_ios_set_native_messagebox_callback(created, siglusMessageBoxCallback, context)
        AppDiagnostics.shared.event("bridge", "siglus.created", ["logical": "\(logicalWidth)x\(logicalHeight)"])

        performanceEnabled = configuration.performance
        elapsedSeconds = 0
        let overlay = KRKROverlayCoordinator(
            gameTitle: configuration.gameTitle,
            engineName: EngineID.siglus.displayName,
            performanceVisible: configuration.performance,
            performanceDetail: configuration.performanceDetail,
            renderer: Self.rendererLabel
        )
        overlay.install(
            in: engineWindow,
            floatingButtonEnabled: configuration.floatingButton,
            floatingButtonOpacity: configuration.idleOpacity,
            onExit: { [weak self] in
                self?.overlayCoordinator?.hideMenu()
                self?.requestStop()
            },
            onBack: { [weak self] in
                // Siglus games open their system menu, close windows and
                // step back on a right click.
                self?.controller?.sendSecondaryClickAtCenter()
                AppDiagnostics.shared.event("session", "back.requested")
            },
            onMarkFinished: { [weak self] in
                AppDiagnostics.shared.event("session", "markFinished.requested")
                self?.onMarkFinished?()
                self?.overlayCoordinator?.hideMenu()
                self?.requestStop()
            },
            onScreenshot: { [weak self] in self?.snapshot() },
            onTogglePerformance: { [weak self] in self?.togglePerformanceHUD() }
        )
        overlayCoordinator = overlay

        lastStepTime = nil
        frameDriver.start(preferredFramesPerSecond: 60) { [weak self] in self?.step() }
        startMetricsTimer()
        state = .running
        isForeground = true
        if !requestedForeground {
            siglus_mikage_set_foreground(created, 0)
            isForeground = false
        }

        presentation.showEngineWindow(engineWindow)
        AppDiagnostics.shared.windows("launch.windowsSwitched")
    }

    /// Siglus has no shutdown of its own: the global save is written, then
    /// the engine is dropped.
    func requestStop() {
        AppDiagnostics.shared.event("session", "stop.requested")
        guard state == .running || state == .starting else { return }
        state = .stopping
        Task { @MainActor in await finish(success: true, message: nil) }
    }

    func setForeground(_ foreground: Bool) {
        AppDiagnostics.shared.event("lifecycle", "foreground.requested", ["active": String(foreground)])
        requestedForeground = foreground
        guard state == .running, let handle else { return }
        guard isForeground != foreground else { return }
        isForeground = foreground
        lastStepTime = nil
        if foreground {
            try? AVAudioSession.sharedInstance().setActive(true)
        }
        siglus_mikage_set_foreground(handle, foreground ? 1 : 0)
    }

    func showMenuOverlay() {
        guard state == .running || state == .stopping else { return }
        overlayCoordinator?.showMenu()
    }

    func hideMenuOverlay() {
        guard state == .running else { return }
        overlayCoordinator?.hideMenu()
    }

    func snapshot() -> UIImage? {
        guard let window = engineWindow, !window.bounds.isEmpty else { return nil }
        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
        return renderer.image { _ in
            UIColor.black.setFill()
            UIRectFill(window.bounds)
            controller?.metalView.drawHierarchy(in: window.bounds, afterScreenUpdates: false)
            overlayCoordinator?.drawScreenshotOverlay(in: window)
        }
    }

    fileprivate func presentMessageBox(requestID: UInt64, kind: Int32, title: String, message: String) {
        guard let controller, let handle else { return }
        let alert = UIAlertController(title: title.isEmpty ? "SiglusEngine" : title, message: message, preferredStyle: .alert)
        func add(_ title: String, _ style: UIAlertAction.Style, _ value: Int64) {
            alert.addAction(UIAlertAction(title: title, style: style) { _ in
                siglus_ios_submit_messagebox_result(handle, requestID, value)
            })
        }
        // The same kinds and results as the Win32 MessageBox calls they stand for.
        switch kind {
        case 1:
            add(String(localized: "OK"), .default, 0)
            add(String(localized: "Cancel"), .cancel, 1)
        case 2:
            add(String(localized: "Yes"), .default, 0)
            add(String(localized: "No"), .cancel, 1)
        case 3:
            add(String(localized: "Yes"), .default, 0)
            add(String(localized: "No"), .default, 1)
            add(String(localized: "Cancel"), .cancel, 2)
        default:
            add(String(localized: "OK"), .default, 0)
        }
        controller.present(alert, animated: true)
    }

    private func finish(success: Bool, message: String?) async {
        AppDiagnostics.shared.event("bridge", "siglus.completion", ["success": String(success), "message": message ?? ""])
        guard state == .running || state == .stopping else { return }
        frameDriver.stop()
        metricsTimer?.invalidate()
        metricsTimer = nil
        if let handle {
            siglus_mikage_set_foreground(handle, 0)
            siglus_ios_set_native_messagebox_callback(handle, nil, nil)
            controller?.detach()
            siglus_ios_destroy(handle)
        }
        handle = nil
        overlayCoordinator?.remove()
        overlayCoordinator = nil

        state = .restoringOrientation
        onReturningToLibrary?()
        presentation.showHostWindow()
        engineWindow?.isHidden = true
        engineWindow = nil
        controller = nil
        await presentation.restorePreviousOrientation()
        AppDiagnostics.shared.windows("return.windowsRestored")
        AppDiagnostics.shared.endGame()

        let completion = onFinished
        onFinished = nil
        onWarning = nil
        onReturningToLibrary = nil
        onMarkFinished = nil
        if success {
            state = .idle
            completion?(.success(()))
        } else {
            let detail = message ?? ""
            state = .failed(detail)
            completion?(.failure(SiglusSessionError.stopped(detail)))
        }
    }

    private var isFailed: Bool {
        if case .failed = state { return true }
        return false
    }

    private func step() {
        guard state == .running, isForeground, let handle else { return }
        let now = CACurrentMediaTime()
        let elapsed = lastStepTime.map { min(max(now - $0, 0), 0.2) } ?? (1.0 / 60.0)
        lastStepTime = now
        let status = siglus_mikage_step(handle, UInt32((elapsed * 1000).rounded()))
        framesThisSecond += 1
        switch status {
        case 0:
            break
        case 1:
            state = .stopping
            Task { @MainActor in await finish(success: true, message: nil) }
        default:
            let message = String(cString: siglus_mikage_last_error())
            state = .stopping
            Task { @MainActor in await finish(success: false, message: message) }
        }
    }

    private func togglePerformanceHUD() {
        performanceEnabled.toggle()
        overlayCoordinator?.setPerformanceVisible(performanceEnabled)
    }

    private func startMetricsTimer() {
        metricsTimer?.invalidate()
        framesThisSecond = 0
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.updateMetrics() }
        }
        RunLoop.main.add(timer, forMode: .common)
        metricsTimer = timer
    }

    private func updateMetrics() {
        guard state == .running else { return }
        if isForeground {
            elapsedSeconds += 1
        }
        let frames = framesThisSecond
        framesThisSecond = 0
        let resident = processResidentMemoryBytes()
        if AppDiagnostics.shared.isEnabled {
            AppDiagnostics.shared.event("session", "heartbeat", [
                "foreground": String(isForeground),
                "steps": String(frames),
                "residentBytes": String(resident),
                "thermalState": String(ProcessInfo.processInfo.thermalState.rawValue)
            ])
        }
        guard performanceEnabled else {
            overlayCoordinator?.update(
                KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds)
            )
            return
        }
        let drawable = controller?.lastDrawablePixels ?? (0, 0)
        var snapshot = KRKRPerformanceSnapshot(renderer: Self.rendererLabel, elapsedSeconds: elapsedSeconds)
        snapshot.framesPerSecond = Double(frames)
        snapshot.frameTimeMilliseconds = frames > 0 ? 1000.0 / Double(frames) : 0
        snapshot.drawableWidth = Int32(drawable.0)
        snapshot.drawableHeight = Int32(drawable.1)
        snapshot.residentMemoryBytes = resident
        snapshot.showsLayerCompositing = false
        snapshot.gpuMemoryBytes = metalAllocatedBytes()
        overlayCoordinator?.update(snapshot)
    }

    private func configureAudioSession(respectSilentMode: Bool) {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(respectSilentMode ? .ambient : .playback, mode: .default)
            try session.setActive(true)
        } catch {
            onWarning?(KRKRSessionError.audioSession(error.localizedDescription))
        }
    }

    /// Games ask for Windows faces (MS Gothic, MS Mincho); PingFang stands in
    /// for both (Mashiro's choice: one face for Chinese translations and
    /// Japanese originals alike), with a per-character fallback chain behind it.
    private static func registerSystemFonts() async {
        guard !fontsRegistered else { return }
        fontsRegistered = true
        let main = Locale.preferredLanguages.first.map(Self.prefersTraditionalChinese) == true
            ? "PingFangTC-Regular" : "PingFangSC-Regular"
        // Characters the main face lacks (Hangul...) come from these, in order.
        let fallbacks = ["PingFangSC-Regular", "PingFangTC-Regular", "HiraginoSans-W3", "AppleSDGothicNeo-Regular"]
            .filter { $0 != main }
        // Rebuilding a face takes a few seconds the first time.
        let files = await Task.detached(priority: .userInitiated) {
            (main: Self.fontFiles(main), fallbacks: fallbacks.map { ($0, Self.fontFiles($0)) })
        }.value
        for (path, face) in files.main {
            for family: Int32 in [0, 1] {
                AppDiagnostics.shared.event("bridge", "siglus.font", ["name": main, "family": String(family), "path": path])
                path.withCString { path in face.withCString { siglus_mikage_register_font_face(path, family, $0) } }
            }
        }
        for (name, faces) in files.fallbacks {
            for (path, face) in faces {
                AppDiagnostics.shared.event("bridge", "siglus.fallbackFont", ["name": name, "path": path])
                path.withCString { path in face.withCString { siglus_mikage_register_fallback_font(path, $0) } }
            }
        }
    }

    /// The file siglus_rs uses for the system face with this PostScript
    /// name, with the face to use in it: the system file itself when the app
    /// can read it and its outlines are TrueType or CFF, else a copy rebuilt
    /// through CoreText. PingFang's file lives in a private framework and,
    /// since iOS 18, keeps its outlines in Apple's own hvgl table.
    nonisolated private static func fontFiles(_ name: String) -> [(path: String, face: String)] {
        let font = CTFontCreateWithName(name as CFString, 16, nil)
        guard CTFontCopyPostScriptName(font) as String == name else {
            AppDiagnostics.shared.event("bridge", "siglus.fontMissing", ["name": name])
            return []
        }
        if let url = CTFontCopyAttribute(font, kCTFontURLAttribute) as? URL {
            let readable = FileManager.default.isReadableFile(atPath: url.path)
            let outlines = hasStandardOutlines(font)
            AppDiagnostics.shared.event("bridge", "siglus.fontFile", [
                "name": name, "path": url.path, "readable": String(readable), "outlines": String(outlines),
            ])
            if readable && outlines { return [(url.path, name)] }
        }
        return exportedFontFile(name).map { [($0.path, "")] } ?? []
    }

    nonisolated private static func hasStandardOutlines(_ font: CTFont) -> Bool {
        guard let available = CTFontCopyAvailableTables(font, []) else { return false }
        // The array holds the tags themselves, not CFNumbers.
        let tags = (0..<CFArrayGetCount(available)).map {
            UInt32(truncatingIfNeeded: UInt(bitPattern: CFArrayGetValueAtIndex(available, $0)))
        }
        let glyf: UInt32 = 0x676C_7966, cff: UInt32 = 0x4346_4620, cff2: UInt32 = 0x4346_4632
        return tags.contains { $0 == glyf || $0 == cff || $0 == cff2 }
    }

    /// A TrueType copy of a system face, its outlines, advances and
    /// character map taken from CoreText. Kept in Caches per system version.
    nonisolated private static func exportedFontFile(_ name: String) -> URL? {
        let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let directory = caches.appendingPathComponent("Mikage/Fonts", isDirectory: true)
        let system = ProcessInfo.processInfo.operatingSystemVersionString
            .filter { $0.isLetter || $0.isNumber || $0 == "." }
        let url = directory.appendingPathComponent("\(name)-v3-\(system).ttf")
        if FileManager.default.fileExists(atPath: url.path) { return url }
        let started = CFAbsoluteTimeGetCurrent()
        guard let exported = trueTypeData(name) else {
            AppDiagnostics.shared.event("bridge", "siglus.fontExportFailed", ["name": name])
            return nil
        }
        let (data, glyphs) = exported
        AppDiagnostics.shared.event("bridge", "siglus.fontExport", [
            "name": name, "bytes": String(data.count), "glyphs": String(glyphs),
            "seconds": String(format: "%.2f", CFAbsoluteTimeGetCurrent() - started),
        ])
        do {
            // Copies made for an older system or build are stale.
            if let old = try? FileManager.default.contentsOfDirectory(atPath: directory.path) {
                for file in old where file.hasPrefix(name + "-") {
                    try? FileManager.default.removeItem(at: directory.appendingPathComponent(file))
                }
            }
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try data.write(to: url, options: .atomic)
            return url
        } catch {
            AppDiagnostics.shared.event("bridge", "siglus.fontExportFailed", ["name": name, "error": error.localizedDescription])
            return nil
        }
    }

    /// Builds a TrueType font (cmap, glyf, loca, hmtx and the tables they
    /// need) holding the glyphs of every character the face maps in planes
    /// 0 to 2, at one font unit per point.
    nonisolated private static func trueTypeData(_ name: String) -> (Data, Int)? {
        let probe = CTFontCreateWithName(name as CFString, 16, nil)
        let unitsPerEm = CTFontGetUnitsPerEm(probe)
        guard unitsPerEm > 0 else { return nil }
        let font = CTFontCreateWithName(name as CFString, CGFloat(unitsPerEm), nil)

        // Character map, in code point order.
        var mapped: [(scalar: UInt32, glyph: CGGlyph)] = []
        var scalar: UInt32 = 0
        while scalar < 0x30000 {
            var scalars: [UInt32] = []
            var units: [UniChar] = []
            while scalars.count < 4096 && scalar < 0x30000 {
                if !(0xD800...0xDFFF).contains(scalar) {
                    scalars.append(scalar)
                    if scalar < 0x10000 {
                        units.append(UniChar(scalar))
                    } else {
                        let value = scalar - 0x10000
                        units.append(UniChar(0xD800 + (value >> 10)))
                        units.append(UniChar(0xDC00 + (value & 0x3FF)))
                    }
                }
                scalar += 1
            }
            var glyphs = [CGGlyph](repeating: 0, count: units.count)
            _ = CTFontGetGlyphsForCharacters(font, units, &glyphs, units.count)
            var index = 0
            for value in scalars {
                if glyphs[index] != 0 { mapped.append((value, glyphs[index])) }
                index += value < 0x10000 ? 1 : 2
            }
        }
        guard !mapped.isEmpty else { return nil }

        // Glyphs renumbered in first-use order, .notdef first.
        var order: [CGGlyph] = [0]
        var newID: [CGGlyph: UInt16] = [0: 0]
        for entry in mapped where newID[entry.glyph] == nil {
            guard order.count < 0xFFFF else { break }
            newID[entry.glyph] = UInt16(order.count)
            order.append(entry.glyph)
        }
        var advances = [CGSize](repeating: .zero, count: order.count)
        CTFontGetAdvancesForGlyphs(font, .horizontal, order, &advances, order.count)

        var glyf = Data()
        var loca = Data()
        var hmtx = Data()
        var maxPoints = 0, maxContours = 0
        var fontBox: [Int16] = [.max, .max, .min, .min]
        var maxAdvance: UInt16 = 0
        for (index, glyph) in order.enumerated() {
            loca.appendBE(UInt32(glyf.count))
            let contours = CTFontCreatePathForGlyph(font, glyph, nil).map(trueTypeContours) ?? []
            let points = Array(contours.joined())
            let advance = UInt16(clamping: Int(advances[index].width.rounded()))
            maxAdvance = max(maxAdvance, advance)
            guard let first = points.first else {
                hmtx.appendBE(advance)
                hmtx.appendBE(UInt16(0))
                continue
            }
            var box = [first.x, first.y, first.x, first.y]
            for point in points {
                box = [min(box[0], point.x), min(box[1], point.y), max(box[2], point.x), max(box[3], point.y)]
            }
            fontBox = [min(fontBox[0], box[0]), min(fontBox[1], box[1]), max(fontBox[2], box[2]), max(fontBox[3], box[3])]
            maxPoints = max(maxPoints, points.count)
            maxContours = max(maxContours, contours.count)
            hmtx.appendBE(advance)
            hmtx.appendBE(UInt16(bitPattern: box[0]))

            glyf.appendBE(UInt16(contours.count))
            for value in box { glyf.appendBE(UInt16(bitPattern: value)) }
            var end = -1
            for contour in contours {
                end += contour.count
                glyf.appendBE(UInt16(end))
            }
            glyf.appendBE(UInt16(0))
            for point in points { glyf.append(point.onCurve ? 1 : 0) }
            var previous: Int16 = 0
            for point in points {
                glyf.appendBE(UInt16(bitPattern: Int16(clamping: Int(point.x) - Int(previous))))
                previous = point.x
            }
            previous = 0
            for point in points {
                glyf.appendBE(UInt16(bitPattern: Int16(clamping: Int(point.y) - Int(previous))))
                previous = point.y
            }
            glyf.append(Data(count: (4 - glyf.count % 4) % 4))
        }
        loca.appendBE(UInt32(glyf.count))
        let box = fontBox[0] <= fontBox[2] ? fontBox : [0, 0, 0, 0]

        var head = Data()
        head.appendBE(UInt32(0x0001_0000))
        head.appendBE(UInt32(0x0001_0000))
        head.appendBE(UInt32(0))
        head.appendBE(UInt32(0x5F0F_3CF5))
        head.appendBE(UInt16(0x000B))
        head.appendBE(UInt16(unitsPerEm))
        head.append(Data(count: 16))
        for value in box { head.appendBE(UInt16(bitPattern: value)) }
        head.appendBE(UInt16(0))
        head.appendBE(UInt16(8))
        head.appendBE(UInt16(2))
        head.appendBE(UInt16(1))
        head.appendBE(UInt16(0))

        var hhea = Data()
        hhea.appendBE(UInt32(0x0001_0000))
        hhea.appendBE(UInt16(bitPattern: Int16(clamping: Int(CTFontGetAscent(font).rounded()))))
        hhea.appendBE(UInt16(bitPattern: Int16(clamping: -Int(CTFontGetDescent(font).rounded()))))
        hhea.appendBE(UInt16(bitPattern: Int16(clamping: Int(CTFontGetLeading(font).rounded()))))
        hhea.appendBE(maxAdvance)
        hhea.appendBE(UInt16(bitPattern: box[0]))
        hhea.appendBE(UInt16(0))
        hhea.appendBE(UInt16(bitPattern: box[2]))
        hhea.appendBE(UInt16(1))
        hhea.append(Data(count: 2 + 2 + 8 + 2))
        hhea.appendBE(UInt16(order.count))

        var maxp = Data()
        maxp.appendBE(UInt32(0x0001_0000))
        maxp.appendBE(UInt16(order.count))
        maxp.appendBE(UInt16(clamping: maxPoints))
        maxp.appendBE(UInt16(clamping: maxContours))
        maxp.appendBE(UInt16(0))
        maxp.appendBE(UInt16(0))
        maxp.appendBE(UInt16(2))
        maxp.append(Data(count: 16))

        // Format 12 groups of consecutive characters on consecutive glyphs.
        var groups: [(start: UInt32, end: UInt32, glyph: UInt32)] = []
        for entry in mapped {
            guard let id = newID[entry.glyph].map(UInt32.init) else { continue }
            if let last = groups.last, last.end + 1 == entry.scalar, last.glyph + (last.end - last.start) + 1 == id {
                groups[groups.count - 1].end = entry.scalar
            } else {
                groups.append((entry.scalar, entry.scalar, id))
            }
        }
        var cmap = Data()
        cmap.appendBE(UInt16(0))
        cmap.appendBE(UInt16(1))
        cmap.appendBE(UInt16(3))
        cmap.appendBE(UInt16(10))
        cmap.appendBE(UInt32(12))
        cmap.appendBE(UInt16(12))
        cmap.appendBE(UInt16(0))
        cmap.appendBE(UInt32(16 + 12 * groups.count))
        cmap.appendBE(UInt32(0))
        cmap.appendBE(UInt32(groups.count))
        for group in groups {
            cmap.appendBE(group.start)
            cmap.appendBE(group.end)
            cmap.appendBE(group.glyph)
        }

        var post = Data()
        post.appendBE(UInt32(0x0003_0000))
        post.append(Data(count: 28))

        let tables: [(tag: String, data: Data)] = [
            ("cmap", cmap), ("glyf", glyf), ("head", head), ("hhea", hhea),
            ("hmtx", hmtx), ("loca", loca), ("maxp", maxp), ("post", post),
        ]
        return (sfnt(tables), order.count)
    }

    private struct TrueTypePoint {
        var x: Int16
        var y: Int16
        var onCurve: Bool
    }

    /// A glyph path as TrueType contours: lines and quadratic curves as they
    /// are, each cubic curve as two quadratics (one per half).
    nonisolated private static func trueTypeContours(_ path: CGPath) -> [[TrueTypePoint]] {
        var contours: [[TrueTypePoint]] = []
        var current: [TrueTypePoint] = []
        var last = CGPoint.zero
        var start = CGPoint.zero
        func point(_ p: CGPoint, _ onCurve: Bool) -> TrueTypePoint {
            TrueTypePoint(x: Int16(clamping: Int(p.x.rounded())), y: Int16(clamping: Int(p.y.rounded())), onCurve: onCurve)
        }
        func close() {
            // A contour closes by itself: drop an end point that repeats the start.
            if current.count > 1, let first = current.first, let end = current.last,
               end.onCurve, end.x == first.x, end.y == first.y {
                current.removeLast()
            }
            if current.count > 2 { contours.append(current) }
            current = []
        }
        func mid(_ a: CGPoint, _ b: CGPoint) -> CGPoint { CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2) }
        func control(_ a: CGPoint, _ b: CGPoint, _ c: CGPoint, _ d: CGPoint) -> CGPoint {
            CGPoint(x: (3 * (b.x + c.x) - a.x - d.x) / 4, y: (3 * (b.y + c.y) - a.y - d.y) / 4)
        }
        path.applyWithBlock { element in
            let points = element.pointee.points
            switch element.pointee.type {
            case .moveToPoint:
                close()
                current.append(point(points[0], true))
                last = points[0]
                start = points[0]
            case .addLineToPoint:
                current.append(point(points[0], true))
                last = points[0]
            case .addQuadCurveToPoint:
                current.append(point(points[0], false))
                current.append(point(points[1], true))
                last = points[1]
            case .addCurveToPoint:
                let (p0, c1, c2, p3) = (last, points[0], points[1], points[2])
                let ab = mid(p0, c1), bc = mid(c1, c2), cd = mid(c2, p3)
                let abc = mid(ab, bc), bcd = mid(bc, cd), middle = mid(abc, bcd)
                current.append(point(control(p0, ab, abc, middle), false))
                current.append(point(middle, true))
                current.append(point(control(middle, bcd, cd, p3), false))
                current.append(point(p3, true))
                last = p3
            case .closeSubpath:
                close()
                last = start
            @unknown default:
                break
            }
        }
        close()
        return contours
    }

    /// Tables packed into one TrueType file.
    nonisolated private static func sfnt(_ tables: [(tag: String, data: Data)]) -> Data {
        let tables = tables.sorted { $0.tag < $1.tag }
        var file = Data()
        let count = UInt16(tables.count)
        var power = 1
        while power * 2 <= Int(count) { power *= 2 }
        let searchRange = UInt16(power * 16)
        file.appendBE(UInt32(0x0001_0000))
        file.appendBE(count)
        file.appendBE(searchRange)
        file.appendBE(UInt16(power.trailingZeroBitCount))
        file.appendBE(count * 16 - searchRange)
        var offset = 12 + tables.count * 16
        for table in tables {
            file.append(contentsOf: Array(table.tag.utf8))
            file.appendBE(checksum(table.data))
            file.appendBE(UInt32(offset))
            file.appendBE(UInt32(table.data.count))
            offset += (table.data.count + 3) & ~3
        }
        for table in tables {
            file.append(table.data)
            file.append(Data(count: (4 - table.data.count % 4) % 4))
        }
        return file
    }

    nonisolated private static func checksum(_ data: Data) -> UInt32 {
        var sum: UInt32 = 0
        data.withUnsafeBytes { bytes in
            var index = 0
            while index < bytes.count {
                var word: UInt32 = 0
                for byte in 0..<4 {
                    word <<= 8
                    if index + byte < bytes.count { word |= UInt32(bytes[index + byte]) }
                }
                sum &+= word
                index += 4
            }
        }
        return sum
    }

    private static func prefersTraditionalChinese(_ languageTag: String) -> Bool {
        let parts = languageTag.lowercased().split(separator: "-")
        guard parts.first == "zh" else { return false }
        return parts.contains("hant") || (!parts.contains("hans") && parts.contains { ["tw", "hk", "mo"].contains($0) })
    }
}

private func siglusMessageBoxCallback(
    _ context: UnsafeMutableRawPointer?,
    _ requestID: UInt64,
    _ kind: Int32,
    _ title: UnsafePointer<CChar>?,
    _ message: UnsafePointer<CChar>?
) {
    guard let context else { return }
    let session = Unmanaged<NativeSiglusSession>.fromOpaque(context).takeUnretainedValue()
    let titleText = title.map { String(cString: $0) } ?? ""
    let messageText = message.map { String(cString: $0) } ?? ""
    Task { @MainActor in
        session.presentMessageBox(requestID: requestID, kind: kind, title: titleText, message: messageText)
    }
}

/// The CAMetalLayer view wgpu renders into, and touch input mapped from the
/// letterboxed viewport to the game's own coordinates.
final class SiglusMetalView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }
}

@MainActor
final class SiglusViewController: UIViewController {
    let metalView = SiglusMetalView(frame: .zero)
    var onMenuGesture: (() -> Void)?
    private(set) var lastDrawablePixels: (UInt32, UInt32) = (0, 0)
    private var handle: UnsafeMutableRawPointer?
    private var logicalSize = CGSize(width: 1280, height: 720)
    private var viewportPoints = CGRect(x: 0, y: 0, width: 1, height: 1)
    private var activeTouch: UITouch?

    override func loadView() {
        metalView.backgroundColor = .black
        metalView.isOpaque = true
        metalView.isMultipleTouchEnabled = true
        view = metalView
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        let menu = UITapGestureRecognizer(target: self, action: #selector(menuGesture))
        menu.numberOfTouchesRequired = 3
        menu.cancelsTouchesInView = false
        view.addGestureRecognizer(menu)
        // Touch is a left click; a long press stands for the right click
        // games use for their menus.
        let longPress = UILongPressGestureRecognizer(target: self, action: #selector(longPress(_:)))
        longPress.minimumPressDuration = 0.5
        longPress.cancelsTouchesInView = true
        view.addGestureRecognizer(longPress)
    }

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }

    func drawablePixels(scale: CGFloat) -> (width: UInt32, height: UInt32) {
        let size = view.bounds.size
        return (UInt32(max(1, (size.width * scale).rounded())), UInt32(max(1, (size.height * scale).rounded())))
    }

    func attach(handle: UnsafeMutableRawPointer, logicalSize: CGSize) {
        self.handle = handle
        if logicalSize.width > 0 && logicalSize.height > 0 {
            self.logicalSize = logicalSize
        }
    }

    func detach() {
        handle = nil
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        layoutEngine(scale: view.window?.screen.scale ?? UIScreen.main.scale)
    }

    func layoutEngine(scale: CGFloat) {
        guard let handle, view.bounds.width > 0, view.bounds.height > 0 else { return }
        metalView.contentScaleFactor = scale
        let pixels = drawablePixels(scale: scale)
        (metalView.layer as? CAMetalLayer)?.drawableSize = CGSize(width: Int(pixels.width), height: Int(pixels.height))
        let logicalWidth = Double(logicalSize.width)
        let logicalHeight = Double(logicalSize.height)
        let fit = min(Double(pixels.width) / logicalWidth, Double(pixels.height) / logicalHeight)
        let width = UInt32(max(1, min(Double(pixels.width), (logicalWidth * fit).rounded())))
        let height = UInt32(max(1, min(Double(pixels.height), (logicalHeight * fit).rounded())))
        let x = (pixels.width - width) / 2
        let y = (pixels.height - height) / 2
        viewportPoints = CGRect(x: CGFloat(x) / scale, y: CGFloat(y) / scale,
                                width: CGFloat(width) / scale, height: CGFloat(height) / scale)
        guard pixels.width != lastDrawablePixels.0 || pixels.height != lastDrawablePixels.1 else { return }
        lastDrawablePixels = (pixels.width, pixels.height)
        siglus_ios_resize_viewport(handle, pixels.width, pixels.height, x, y, width, height)
    }

    func sendSecondaryClickAtCenter() {
        guard let handle else { return }
        siglus_mikage_secondary_click(handle, Double(logicalSize.width) / 2, Double(logicalSize.height) / 2)
    }

    private func gamePoint(_ touch: UITouch) -> CGPoint {
        let point = touch.location(in: view)
        return CGPoint(
            x: (point.x - viewportPoints.minX) * logicalSize.width / max(viewportPoints.width, 1),
            y: (point.y - viewportPoints.minY) * logicalSize.height / max(viewportPoints.height, 1)
        )
    }

    private func send(_ phase: Int32, _ touch: UITouch) {
        guard let handle else { return }
        let point = gamePoint(touch)
        siglus_ios_touch(handle, phase, Double(point.x), Double(point.y))
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        // One finger plays; more fingers are the menu gesture.
        guard activeTouch == nil, (event?.allTouches?.count ?? 1) == 1, let touch = touches.first else { return }
        activeTouch = touch
        send(0, touch)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        send(1, touch)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        send(2, touch)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = activeTouch, touches.contains(touch) else { return }
        activeTouch = nil
        send(3, touch)
    }

    @objc private func menuGesture() {
        onMenuGesture?()
    }

    @objc private func longPress(_ recognizer: UILongPressGestureRecognizer) {
        guard recognizer.state == .began, let handle else { return }
        let point = recognizer.location(in: view)
        let game = CGPoint(
            x: (point.x - viewportPoints.minX) * logicalSize.width / max(viewportPoints.width, 1),
            y: (point.y - viewportPoints.minY) * logicalSize.height / max(viewportPoints.height, 1)
        )
        siglus_mikage_secondary_click(handle, Double(game.x), Double(game.y))
    }
}

private func withOptionalCString<Result>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> Result) -> Result {
    guard let string else { return body(nil) }
    return string.withCString { body($0) }
}

private extension Data {
    mutating func appendBE(_ value: UInt16) { Swift.withUnsafeBytes(of: value.bigEndian) { append(contentsOf: $0) } }
    mutating func appendBE(_ value: UInt32) { Swift.withUnsafeBytes(of: value.bigEndian) { append(contentsOf: $0) } }
}
