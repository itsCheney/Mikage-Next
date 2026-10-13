import Foundation
import VNCore

/// What Mikage keeps on the device, measured for the storage page.
struct StorageReport: Equatable, Sendable {
    /// Size of each library game's `savedata` folder, by record id.
    var saves: [UUID: Int64] = [:]
    /// Saves kept from games removed with "keep saves", by record id.
    var keptSaves: [UUID: Int64] = [:]
    /// Covers, Bangumi and VNDB artwork, and the library file.
    var metadata: Int64 = 0
    var logs: Int64 = 0
    var deviceTotal: Int64?
    var deviceAvailable: Int64?

    var totalSaves: Int64 {
        saves.values.reduce(0, +) + keptSaves.values.reduce(0, +)
    }
}

extension AppModel {
    /// Walks each game's saves and Mikage's own folders off the main thread.
    /// Game folders themselves are not walked again: their sizes come from
    /// the library scan.
    func measureStorage() async -> StorageReport {
        let repository = self.repository
        let games = self.games
        let missing = self.missingGames
        let logDirectory = AppDiagnostics.shared.logDirectory
        return await Task.detached(priority: .utility) {
            var report = StorageReport()
            for game in games {
                guard let saves = try? repository.savesURL(for: game) else { continue }
                report.saves[game.id] = GameScanner.directorySize(saves)
            }
            for game in missing where repository.hasKeptSaves(game) {
                report.keptSaves[game.id] = GameScanner.directorySize(repository.keptSavesURL(for: game.id))
            }
            let libraryFile = (try? repository.libraryURL.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
            report.metadata = GameScanner.directorySize(repository.coversURL) + Int64(libraryFile)
            report.logs = GameScanner.directorySize(logDirectory)
            if let values = try? repository.documentsRoot.resourceValues(forKeys: [
                .volumeTotalCapacityKey, .volumeAvailableCapacityForImportantUsageKey
            ]) {
                report.deviceTotal = values.volumeTotalCapacity.map(Int64.init)
                report.deviceAvailable = values.volumeAvailableCapacityForImportantUsage
            }
            return report
        }.value
    }

    /// Finished games whose folders are still here, largest first: the
    /// ones worth removing while keeping their saves.
    var finishedGames: [GameRecord] {
        games
            .filter { $0.listEntry.status == .finished }
            .sorted { $0.byteCount > $1.byteCount }
    }

    /// Deletes the game folder and keeps its saves, record and cover.
    @discardableResult
    func offload(_ game: GameRecord) async -> Bool {
        AppDiagnostics.shared.event("storage", "offload.requested", ["folder": game.folderName])
        guard libraryReadable else { return false }
        do {
            let repository = self.repository
            try await Task.detached(priority: .userInitiated) {
                try repository.offload(game)
            }.value
            await refreshLibrary()
            return true
        } catch {
            alert = String(localized: "Couldn’t remove the game: \(error.localizedDescription)")
            await refreshLibrary(showErrors: false)
            return false
        }
    }

    /// Removes several games one after another, keeping their saves.
    func offload(_ list: [GameRecord]) async {
        for game in list {
            guard await offload(game) else { return }
        }
    }

    /// Deletes the game folder, its saves and its record.
    @discardableResult
    func deleteGame(_ game: GameRecord) async -> Bool {
        AppDiagnostics.shared.event("storage", "delete.requested", ["folder": game.folderName])
        guard libraryReadable else { return false }
        do {
            let repository = self.repository
            try await Task.detached(priority: .userInitiated) {
                try repository.delete(game)
            }.value
            await refreshLibrary()
            return true
        } catch {
            alert = String(localized: "Couldn’t delete the game: \(error.localizedDescription)")
            await refreshLibrary(showErrors: false)
            return false
        }
    }

    /// Zips the game's `savedata` folder for the share sheet.
    func exportSaves(_ game: GameRecord) async throws -> URL {
        let saves = try repository.savesURL(for: game)
        let name = displayTitle(game)
        return try await Task.detached(priority: .userInitiated) {
            try Self.zip(saves, named: String(localized: "\(name) Saves"))
        }.value
    }

    /// Files' own path for the game folder, which opens it in the Files app.
    func filesAppURL(for game: GameRecord) -> URL? {
        guard let directory = try? repository.directory(for: game),
              let path = directory.path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) else {
            return nil
        }
        return URL(string: "shareddocuments://\(path)")
    }

    /// The coordinator's "for uploading" read hands back a zip of a folder.
    private nonisolated static func zip(_ folder: URL, named name: String) throws -> URL {
        let fm = FileManager.default
        let caches = fm.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let exports = caches.appendingPathComponent("Mikage/SaveExports", isDirectory: true)
        // Only our own earlier exports live here.
        try? fm.removeItem(at: exports)
        try fm.createDirectory(at: exports, withIntermediateDirectories: true)
        let safeName = name.components(separatedBy: CharacterSet(charactersIn: "/:\\")).joined(separator: "-")
        let destination = exports.appendingPathComponent(safeName).appendingPathExtension("zip")

        var coordinationError: NSError?
        var copyError: Error?
        NSFileCoordinator().coordinate(readingItemAt: folder, options: .forUploading, error: &coordinationError) { zipped in
            do { try fm.copyItem(at: zipped, to: destination) } catch { copyError = error }
        }
        if let error = coordinationError ?? copyError { throw error }
        return destination
    }
}
