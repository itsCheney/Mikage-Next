import SwiftUI
import VNCore

/// The glass notice shown over the library after a status changed by
/// itself, with a way to take the change back.
struct LibraryToast: Identifiable, Equatable {
    let id = UUID()
    let gameID: UUID
    var title: String
    var detail: String
    /// The entry before the change; Undo restores it.
    var previous: ListEntry?
}

extension AppModel {
    private static let accountsKey = "accounts.v1"
    private static let lastSyncedKey = "sync.lastSyncedAt"

    // MARK: Records

    func record(_ id: UUID) -> GameRecord? {
        games.first { $0.id == id } ?? missingGames.first { $0.id == id }
    }

    /// Writes one record under the repository lock and mirrors it in memory.
    @discardableResult
    func updateRecord(_ id: UUID, _ transform: (inout GameRecord) -> Void) -> GameRecord? {
        guard libraryReadable else { return nil }
        do {
            let updated = try repository.update(id, transform)
            if let index = games.firstIndex(where: { $0.id == id }) {
                games[index] = updated
            } else if let index = missingGames.firstIndex(where: { $0.id == id }) {
                missingGames[index] = updated
            }
            return updated
        } catch {
            alert = error.localizedDescription
            return nil
        }
    }

    // MARK: Titles

    /// The title the library shows, following Settings › Accounts & Sync.
    func displayTitle(_ game: GameRecord) -> String {
        guard let metadata = game.link?.metadata else { return game.title }
        let chinese = metadata.chineseTitle
        let original = metadata.originalTitle
        let preferred: String?
        switch settings.titleDisplay {
        case .automatic:
            let chineseUI = Bundle.main.preferredLocalizations.first?.hasPrefix("zh") == true
            preferred = chineseUI ? (chinese ?? original) : (original ?? metadata.romanizedTitle)
        case .chinese:
            preferred = chinese ?? original
        case .original:
            preferred = original ?? chinese
        case .romanized:
            preferred = metadata.romanizedTitle ?? original
        case .folder:
            preferred = nil
        }
        return preferred ?? game.title
    }

    /// Every title search should find a game by.
    func searchableTitles(_ game: GameRecord) -> [String] {
        guard let metadata = game.link?.metadata else { return [] }
        return [metadata.chineseTitle, metadata.originalTitle, metadata.romanizedTitle].compactMap { $0 }
    }

    // MARK: Accounts

    func loadAccounts() {
        if let data = defaults.data(forKey: Self.accountsKey),
           let stored = try? JSONDecoder().decode([ServiceAccount].self, from: data) {
            for account in stored where tokens.token(for: account.service) != nil {
                accounts[account.service] = account
            }
        }
        lastSyncedAt = defaults.object(forKey: Self.lastSyncedKey) as? Date
    }

    private func saveAccounts() {
        if let data = try? JSONEncoder().encode(Array(accounts.values)) {
            defaults.set(data, forKey: Self.accountsKey)
        }
    }

    /// Checks the token with the service, then keeps it in the Keychain.
    func connect(_ service: MetadataService, token rawToken: String) async throws {
        let token = rawToken.trimmingCharacters(in: .whitespacesAndNewlines)
        AppDiagnostics.shared.event("accounts", "connect.begin", ["service": service.rawValue])
        let account: ServiceAccount
        switch service {
        case .bangumi: account = try await BangumiClient(token: token).account()
        case .vndb: account = try await VNDBClient(token: token).account()
        }
        try tokens.setToken(token, for: service)
        accounts[service] = account
        saveAccounts()
        AppDiagnostics.shared.event("accounts", "connect.end", ["service": service.rawValue])
        // Changes made before connecting go out now, without holding up the sheet.
        Task { await syncPending() }
    }

    func disconnect(_ service: MetadataService) {
        AppDiagnostics.shared.event("accounts", "disconnect", ["service": service.rawValue])
        tokens.removeToken(for: service)
        accounts[service] = nil
        saveAccounts()
        if service == .bangumi {
            bangumiSignInExpired = false
            signOutOfBangumiWeb()
        }
    }

    /// A client with the current Bangumi token, renewed first when it is
    /// about to lapse. Reads fall back to signed-out requests when renewal
    /// fails; writes go through `withBangumi` to see the error.
    func bangumiClient() async -> BangumiClient {
        BangumiClient(token: try? await bangumiAccessToken())
    }

    /// Runs a Bangumi request, renewing the token and retrying once when
    /// Bangumi rejects it, as the community clients do.
    @discardableResult
    func withBangumi<T>(_ operation: (BangumiClient) async throws -> T) async throws -> T {
        do {
            return try await operation(BangumiClient(token: try await bangumiAccessToken()))
        } catch MetadataError.unauthorized(.bangumi) {
            guard let stored = tokens.token(for: .bangumi), let token = BangumiToken(stored: stored) else {
                throw MetadataError.unauthorized(.bangumi)
            }
            let renewed = try await renewBangumiToken(token)
            return try await operation(BangumiClient(token: renewed.accessToken))
        }
    }

    // MARK: Bangumi sign-in

    /// Mikage's Bangumi app from the build settings. Nil until one is
    /// registered, which leaves an access token as the only way in.
    static let bangumiApp: BangumiAppCredentials? = {
        let info = Bundle.main.infoDictionary ?? [:]
        func value(_ key: String) -> String? {
            guard let text = (info[key] as? String)?.trimmingCharacters(in: .whitespaces),
                  !text.isEmpty, !text.hasPrefix("$(") else { return nil }
            return text
        }
        guard let id = value("BangumiAppID"), let secret = value("BangumiAppSecret") else { return nil }
        return BangumiAppCredentials(
            clientID: id,
            clientSecret: secret,
            redirectURI: value("BangumiRedirectURI") ?? "mikage://bangumi/callback"
        )
    }()

    /// Finishes signing in once the website session exists: approves
    /// Mikage with it, then reads the account.
    func connectBangumiWithWebSession() async throws {
        guard let app = Self.bangumiApp else { throw BangumiWebError.authorizationRefused }
        AppDiagnostics.shared.event("accounts", "connect.begin", ["service": "bangumi", "method": "web"])
        let token = try await BangumiWebSession.shared.authorize(app)
        let account = try await BangumiClient(token: token.accessToken).account()
        try tokens.setToken(token.stored, for: .bangumi)
        accounts[.bangumi] = account
        saveAccounts()
        bangumiSignInExpired = false
        bangumiWebSignedIn = true
        AppDiagnostics.shared.event("accounts", "connect.end", ["service": "bangumi", "method": "web"])
        Task { await syncPending() }
    }

    /// Whether the Bangumi account came from signing in, rather than a
    /// pasted access token.
    var bangumiUsesSignIn: Bool {
        tokens.token(for: .bangumi).flatMap { BangumiToken(stored: $0) } != nil
    }

    /// The Bangumi access token, renewed when it is within a day of lapsing.
    /// A pasted access token is used as is.
    func bangumiAccessToken() async throws -> String? {
        guard let stored = tokens.token(for: .bangumi) else { return nil }
        guard let token = BangumiToken(stored: stored) else { return stored }
        if token.needsRefresh() {
            return try await renewBangumiToken(token).accessToken
        }
        return token.accessToken
    }

    /// One renewal at a time; callers arriving meanwhile share its result.
    private func renewBangumiToken(_ old: BangumiToken) async throws -> BangumiToken {
        if let running = bangumiRenewal {
            return try await running.value
        }
        let task = Task { try await self.performBangumiRenewal(old) }
        bangumiRenewal = task
        defer { bangumiRenewal = nil }
        return try await task.value
    }

    /// The refresh token first; when Bangumi refuses it, the website
    /// session approves Mikage again. Only when both are refused does the
    /// account need signing in again. Being offline changes nothing.
    private func performBangumiRenewal(_ old: BangumiToken) async throws -> BangumiToken {
        guard let app = Self.bangumiApp else { throw MetadataError.unauthorized(.bangumi) }
        var renewed: BangumiToken?
        do {
            renewed = try await BangumiOAuth.requestToken(BangumiOAuth.refreshRequest(refreshToken: old.refreshToken, app: app))
        } catch let error as URLError {
            throw error
        } catch {
            AppDiagnostics.shared.event("accounts", "bangumi.refresh.failed", ["error": error.localizedDescription])
        }
        if renewed == nil {
            do {
                renewed = try await BangumiWebSession.shared.authorize(app)
            } catch let error as URLError {
                throw error
            } catch {
                AppDiagnostics.shared.event("accounts", "bangumi.reauthorize.failed", ["error": error.localizedDescription])
            }
        }
        guard let renewed else {
            bangumiSignInExpired = true
            bangumiWebSignedIn = false
            throw BangumiWebError.signInRequired
        }
        try tokens.setToken(renewed.stored, for: .bangumi)
        bangumiSignInExpired = false
        AppDiagnostics.shared.event("accounts", "bangumi.renewed")
        return renewed
    }

    func vndbClient() -> VNDBClient {
        VNDBClient(token: tokens.token(for: .vndb))
    }

    // MARK: Matching

    /// Searches both services at once. Fails only when both fail, so one
    /// service being down still leaves the other's candidates.
    func searchCandidates(_ query: String) async throws -> [MatchCandidate] {
        let vndb = vndbClient()
        let bangumi = await bangumiClient()
        async let vndbResults = Self.attempt { try await vndb.search(query) }
        async let bangumiResults = Self.attempt { try await bangumi.search(query) }
        let (novels, subjects) = await (vndbResults, bangumiResults)
        if case .failure(let error) = novels, case .failure = subjects {
            throw error
        }
        return MatchFinder.candidates(
            query: query,
            vndb: (try? novels.get()) ?? [],
            bangumi: (try? subjects.get()) ?? []
        )
    }

    nonisolated private static func attempt<T>(_ operation: () async throws -> T) async -> Result<T, Error> {
        do {
            return .success(try await operation())
        } catch {
            return .failure(error)
        }
    }

    private func requestMatchIfFound(_ game: GameRecord?) {
        guard let game, game.link == nil else { return }
        matchRequest = game
    }

    /// Offers the match sheet for a folder that was just imported.
    func requestMatch(forImported url: URL, engine: EngineID) {
        let name = GameScanner.normalized(url.lastPathComponent.precomposedStringWithCanonicalMapping)
        requestMatchIfFound(games.first {
            $0.engine == engine && GameScanner.normalized($0.folderName) == name
        })
    }

    /// Links the game to the candidate, filling in the other service when it
    /// has the same work, then downloads artwork and syncs.
    func link(_ game: GameRecord, to candidate: MatchCandidate) async throws {
        AppDiagnostics.shared.event("linking", "link.begin", ["folder": game.folderName, "candidate": candidate.id])
        var novel = candidate.vndb
        var subject = candidate.bangumi
        if novel == nil, let subject {
            novel = await counterpart(ofBangumi: subject)
        }
        if subject == nil, let novel {
            subject = await counterpart(ofVNDB: novel)
        }
        // Search results can omit the infobox; the subject page has it all.
        if let id = subject?.id, let full = try? await bangumiClient().subject(id: id) {
            subject = full
        }
        let metadata = LinkedMetadata.merged(vndb: novel, bangumi: subject)
        let remote = await remoteEntry(vndbID: novel?.id, bangumiID: subject?.id)

        guard let previous = record(game.id) else { throw LibraryError.recordNotFound }
        let replacesLink = previous.link?.vndbID != novel?.id || previous.link?.bangumiID != subject?.id
        if replacesLink {
            repository.removeArtwork(named: previous.link?.posterName)
            repository.removeArtwork(named: previous.link?.bannerName)
        }
        let updated = updateRecord(game.id) { record in
            var link = replacesLink ? GameLink() : (record.link ?? GameLink())
            link.vndbID = novel?.id
            link.bangumiID = subject?.id
            link.metadata = metadata
            record.link = link
            if record.listEntry.status == nil && record.listEntry.rating == nil, let remote {
                // The player's list already has this work: adopt it.
                record.listEntry.status = remote.status
                record.listEntry.rating = remote.rating
                record.listEntry.comment = remote.comment
                record.listEntry.startedAt = record.listEntry.startedAt ?? remote.startedAt
                record.listEntry.finishedAt = record.listEntry.finishedAt ?? remote.finishedAt
            } else if !record.listEntry.isEmpty {
                record.listEntry.markPending(link.services)
            }
        }
        guard updated != nil else { throw LibraryError.recordNotFound }
        AppDiagnostics.shared.event("linking", "link.end", ["vndb": novel?.id ?? "", "bangumi": subject.map { String($0.id) } ?? ""])
        await refreshArtwork(for: game.id)
        await sync(game.id)
    }

    func unlink(_ game: GameRecord) {
        AppDiagnostics.shared.event("linking", "unlink", ["folder": game.folderName])
        guard let current = record(game.id) else { return }
        repository.removeArtwork(named: current.link?.posterName)
        repository.removeArtwork(named: current.link?.bannerName)
        updateRecord(game.id) {
            $0.link = nil
            $0.listEntry.pendingSync = []
        }
    }

    /// Fetches the linked entries again, keeping the link itself.
    func refreshMetadata(_ game: GameRecord, showErrors: Bool = true) async {
        guard let link = record(game.id)?.link else { return }
        let vndb = vndbClient()
        let bangumi = await bangumiClient()
        do {
            async let novel = link.vndbID.asyncFlatMap { try await vndb.visualNovel(id: $0) }
            async let subject = link.bangumiID.asyncFlatMap { try await bangumi.subject(id: $0) }
            let metadata = LinkedMetadata.merged(vndb: try await novel, bangumi: try await subject)
            updateRecord(game.id) { $0.link?.metadata = metadata }
            await refreshArtwork(for: game.id)
        } catch {
            AppDiagnostics.shared.event("linking", "metadata.failed", ["error": error.localizedDescription])
            if showErrors { alert = error.localizedDescription }
        }
    }

    // MARK: Keeping linked games current

    /// Brings every linked game up to date: details fetched in an older
    /// format, artwork that never downloaded, and statuses changed on the
    /// services. Runs when the app comes to the front, at most every few
    /// minutes.
    func refreshLinkedGames() async {
        if let last = lastLinkedRefresh, Date().timeIntervalSince(last) < 5 * 60 { return }
        lastLinkedRefresh = Date()
        for game in games + missingGames where game.link != nil {
            await refreshLinked(game.id)
        }
    }

    /// The same for one game, when its details page opens.
    func refreshLinked(_ id: UUID) async {
        guard let game = record(id), let link = game.link else { return }
        if link.vndbID == nil, link.bangumiID != nil, await findVNDBCounterpart(id) {
            // Fresh details from both services, artwork included.
        } else if link.metadata.isStale {
            await refreshMetadata(game, showErrors: false)
        } else if needsArtwork(link) {
            await refreshArtwork(for: id)
        }
        await pullListEntry(id)
    }

    /// A game linked on Bangumi alone gets its VNDB entry when one is
    /// found, once per launch. Returns whether the link gained one.
    private func findVNDBCounterpart(_ id: UUID) async -> Bool {
        guard !vndbLookups.contains(id), let link = record(id)?.link, link.vndbID == nil,
              let bangumiID = link.bangumiID else { return false }
        vndbLookups.insert(id)
        guard let subject = try? await bangumiClient().subject(id: bangumiID),
              let novel = await counterpart(ofBangumi: subject) else { return false }
        AppDiagnostics.shared.event("linking", "counterpart.added", ["vndb": novel.id])
        updateRecord(id) { record in
            guard record.link?.bangumiID == bangumiID, record.link?.vndbID == nil else { return }
            record.link?.vndbID = novel.id
            record.link?.metadata = LinkedMetadata.merged(vndb: novel, bangumi: subject)
        }
        await refreshArtwork(for: id)
        return true
    }

    /// True when the art the settings call for is not on disk, such as
    /// after a download failed.
    private func needsArtwork(_ link: GameLink) -> Bool {
        let hide = settings.hideSensitiveImages
        let wantsPoster = link.posterChoice ?? link.metadata.poster(hidingSensitive: hide)?.url
        let wantsBanner = link.bannerChoice ?? link.metadata.banner(hidingSensitive: hide)?.url
        return (wantsPoster != nil && repository.artworkURL(named: link.posterName) == nil)
            || (wantsBanner != nil && repository.artworkURL(named: link.bannerName) == nil)
    }

    /// Adopts the status, rating and comment the player's Bangumi or VNDB
    /// list holds, so a game played elsewhere shows its state here. Changes
    /// made in Mikage and not yet sent win; a work missing from the lists
    /// leaves the local state alone.
    func pullListEntry(_ id: UUID) async {
        guard let game = record(id), let link = game.link, game.listEntry.pendingSync.isEmpty,
              let remote = await remoteEntry(vndbID: link.vndbID, bangumiID: link.bangumiID),
              remote.status != nil else { return }
        // The record may have changed while the lists answered.
        guard let current = record(id), current.listEntry.pendingSync.isEmpty else { return }
        let entry = current.listEntry
        guard entry.status != remote.status || entry.rating != remote.rating
            || (remote.comment != nil && entry.comment != remote.comment) else { return }
        AppDiagnostics.shared.event("linking", "entry.pulled", ["status": remote.status?.rawValue ?? ""])
        updateRecord(id) { record in
            record.listEntry.status = remote.status
            record.listEntry.rating = remote.rating
            record.listEntry.comment = remote.comment ?? record.listEntry.comment
            record.listEntry.startedAt = remote.startedAt ?? record.listEntry.startedAt
            record.listEntry.finishedAt = remote.finishedAt ?? record.listEntry.finishedAt
            record.listEntry.lastSyncedAt = Date()
        }
    }

    /// The same work on Bangumi, found by the VN's titles.
    private func counterpart(ofVNDB novel: VNDBVisualNovel) async -> BangumiSubject? {
        guard let results = try? await bangumiClient().search(novel.originalTitle, limit: 5) else { return nil }
        return MatchFinder.candidates(query: novel.originalTitle, vndb: [novel], bangumi: results)
            .first { $0.vndb != nil }?.bangumi
    }

    /// The same work on VNDB, found by the subject's titles.
    /// The VNDB link on the subject's Bangumi page wins; otherwise each of
    /// its titles is searched in turn.
    private func counterpart(ofBangumi subject: BangumiSubject) async -> VNDBVisualNovel? {
        var subject = subject
        // Search results can omit the infobox, which holds links and aliases.
        if subject.infobox.isEmpty, let full = try? await bangumiClient().subject(id: subject.id) {
            subject = full
        }
        let vndb = vndbClient()
        if let id = subject.vndbID, let novel = try? await vndb.visualNovel(id: id) {
            AppDiagnostics.shared.event("linking", "counterpart.vndbLink", ["vndb": id])
            return novel
        }
        for title in subject.searchTitles.prefix(4) {
            guard let results = try? await vndb.search(title, limit: 5), !results.isEmpty else { continue }
            if let novel = MatchFinder.candidates(query: title, vndb: results, bangumi: [subject])
                .first(where: { $0.bangumi != nil })?.vndb {
                return novel
            }
        }
        return nil
    }

    /// What the player's own lists already hold for the work, Bangumi first.
    private func remoteEntry(vndbID: String?, bangumiID: Int?) async -> ListEntry? {
        if let bangumiID, let username = accounts[.bangumi]?.username,
           let entry = try? await withBangumi({ try await $0.collection(username: username, subjectID: bangumiID) }) {
            return entry
        }
        if let vndbID, let userID = accounts[.vndb]?.userID,
           let entry = try? await vndbClient().listEntry(userID: userID, vnID: vndbID) {
            return entry
        }
        return nil
    }

    // MARK: Artwork

    /// Downloads the poster and banner the current settings call for.
    /// Each file gets a fresh name, so cached decodes of the old one never
    /// show for the new art.
    func refreshArtwork(for id: UUID) async {
        guard let link = record(id)?.link else { return }
        let hide = settings.hideSensitiveImages
        let poster = link.posterChoice ?? link.metadata.poster(hidingSensitive: hide)?.url
        let banner = link.bannerChoice ?? link.metadata.banner(hidingSensitive: hide)?.url

        var posterName = link.posterName
        var bannerName = link.bannerName
        var posterSource = link.posterSource
        var bannerSource = link.bannerSource
        // A file that never arrived, or was lost, is fetched again.
        if poster != link.posterSource || repository.artworkURL(named: link.posterName) == nil {
            posterName = await download(poster, for: id, kind: "poster")
            // A failed download keeps nothing, so the next refresh retries.
            posterSource = posterName == nil ? nil : poster
        }
        if banner != link.bannerSource || repository.artworkURL(named: link.bannerName) == nil {
            bannerName = await download(banner, for: id, kind: "banner")
            bannerSource = bannerName == nil ? nil : banner
        }
        let oldPoster = link.posterName
        let oldBanner = link.bannerName
        var applied = false
        updateRecord(id) { record in
            // The link may have changed while the files downloaded.
            guard record.link?.vndbID == link.vndbID, record.link?.bangumiID == link.bangumiID else { return }
            record.link?.posterName = posterName
            record.link?.posterSource = posterSource
            record.link?.bannerName = bannerName
            record.link?.bannerSource = bannerSource
            applied = true
        }
        if applied {
            if oldPoster != posterName { repository.removeArtwork(named: oldPoster) }
            if oldBanner != bannerName { repository.removeArtwork(named: oldBanner) }
        } else {
            if posterName != oldPoster { repository.removeArtwork(named: posterName) }
            if bannerName != oldBanner { repository.removeArtwork(named: bannerName) }
        }
    }

    func refreshAllArtwork() async {
        for game in games + missingGames where game.link != nil {
            await refreshArtwork(for: game.id)
        }
    }

    private func download(_ source: String?, for id: UUID, kind: String) async -> String? {
        guard let source, let url = URL(string: source) else { return nil }
        do {
            var request = URLRequest(url: url, timeoutInterval: 30)
            // Image hosts answer browsers most reliably.
            request.setValue(BangumiWebSession.userAgent, forHTTPHeaderField: "User-Agent")
            request.setValue("image/webp,image/avif,image/jpeg,image/png,image/*;q=0.8", forHTTPHeaderField: "Accept")
            let (data, response) = try await URLSession.shared.data(for: request)
            let status = (response as? HTTPURLResponse)?.statusCode ?? 200
            guard (200..<300).contains(status), UIImage(data: data) != nil else {
                AppDiagnostics.shared.event("linking", "artwork.rejected", ["kind": kind, "status": String(status), "bytes": String(data.count)])
                return nil
            }
            let ext = ["jpg", "jpeg", "png", "webp"].contains(url.pathExtension.lowercased())
                ? url.pathExtension.lowercased() : "jpg"
            let name = "\(id.uuidString)-\(kind)-\(UUID().uuidString.prefix(8)).\(ext)"
            try repository.writeArtwork(data, named: name)
            return name
        } catch {
            AppDiagnostics.shared.event("linking", "artwork.failed", ["kind": kind, "error": error.localizedDescription])
            return nil
        }
    }

    // MARK: Status, rating and sync

    /// Changes the entry, marks it for every linked service and syncs.
    func updateEntry(_ id: UUID, _ change: (inout ListEntry) -> Void) {
        let updated = updateRecord(id) { record in
            change(&record.listEntry)
            record.listEntry.markPending(record.link?.services ?? [])
        }
        if updated?.link != nil {
            Task { await sync(id) }
        }
    }

    func setStatus(_ status: PlayStatus?, for game: GameRecord) {
        AppDiagnostics.shared.event("linking", "status.set", ["status": status?.rawValue ?? "none"])
        let recordDates = settings.syncDates
        updateEntry(game.id) { entry in
            entry.status = status
            if recordDates, status == .playing, entry.startedAt == nil { entry.startedAt = Date() }
            if recordDates, status == .finished, entry.finishedAt == nil { entry.finishedAt = Date() }
        }
    }

    /// Saves a rating and comment; `finished` also marks the game finished.
    func saveRating(_ rating: Int?, comment: String, finished: Bool, for game: GameRecord) {
        let recordDates = settings.syncDates
        updateEntry(game.id) { entry in
            entry.rating = rating.map(ListEntry.clampRating)
            entry.comment = comment.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? nil : comment
            if finished {
                entry.status = .finished
                if recordDates, entry.finishedAt == nil { entry.finishedAt = Date() }
            }
        }
    }

    /// Called when a game returns to the library.
    func finishedPlaying(_ game: GameRecord, duration: TimeInterval, markFinished: Bool) {
        recordPlayback(of: game, duration: duration)
        if markFinished {
            // Wait for the player to finish dismissing before the sheet.
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: 700_000_000)
                finishRequest = record(game.id)
            }
        } else {
            applyAutomaticStatus(to: game.id, sessionDuration: duration)
        }
    }

    /// Moves a game to Playing after a real session, when it had no status or
    /// sat on the wishlist or on hold, and offers Undo. Finished and dropped
    /// games stay as they are: replaying one is not a change of mind.
    func applyAutomaticStatus(to id: UUID, sessionDuration: TimeInterval) {
        guard settings.autoStatus, sessionDuration >= 60, let game = record(id) else { return }
        let previous = game.listEntry
        guard previous.status == nil || previous.status == .wish || previous.status == .onHold else { return }
        setStatus(.playing, for: game)

        let services = game.link?.services.filter { accounts[$0] != nil } ?? []
        let detail = [displayTitle(game), PlayStatus.playing.displayName,
                      String(localized: "\(sessionDuration.playTimeSummary) this session")]
            .joined(separator: " · ")
        toast = LibraryToast(
            gameID: id,
            title: services.isEmpty ? String(localized: "Marked as Playing") : String(localized: "Syncing…"),
            detail: detail,
            previous: previous
        )
    }

    func undoToast() {
        guard let toast, let previous = toast.previous else { return }
        AppDiagnostics.shared.event("linking", "toast.undo")
        self.toast = nil
        updateEntry(toast.gameID) { entry in
            entry.status = previous.status
            entry.startedAt = previous.startedAt
            entry.finishedAt = previous.finishedAt
        }
    }

    /// Linked services with unsent changes and a connected account.
    func pendingServices(_ game: GameRecord) -> [MetadataService] {
        guard let link = game.link else { return [] }
        return game.listEntry.pendingSync.filter { link.services.contains($0) && accounts[$0] != nil }
    }

    var pendingSyncCount: Int {
        (games + missingGames).filter { !pendingServices($0).isEmpty }.count
    }

    /// Sends one game's entry to its services until nothing is left to send
    /// or a request fails. A change made while a request is in flight stays
    /// pending and goes out on the next pass.
    @discardableResult
    func sync(_ id: UUID) async -> [MetadataService] {
        guard !syncingGames.contains(id) else { return [] }
        syncingGames.insert(id)
        defer { syncingGames.remove(id) }

        var synced: [MetadataService] = []
        var failed = false
        for _ in 0..<3 {
            guard let game = record(id), let link = game.link else { break }
            let services = pendingServices(game)
            if services.isEmpty { break }
            let entry = game.listEntry
            var done: [MetadataService] = []
            for service in services {
                do {
                    try await push(entry, link: link, to: service)
                    done.append(service)
                } catch {
                    failed = true
                    lastSyncError = error.localizedDescription
                    AppDiagnostics.shared.event("linking", "sync.failed", ["service": service.rawValue, "error": error.localizedDescription])
                    reportRemovalFailure(error, of: game, service: service, entry: entry)
                }
            }
            synced += done
            if !done.isEmpty {
                updateRecord(id) { record in
                    var sent = entry
                    sent.pendingSync = record.listEntry.pendingSync
                    sent.lastSyncedAt = record.listEntry.lastSyncedAt
                    // Only clear what was actually sent.
                    guard record.listEntry == sent else { return }
                    record.listEntry.pendingSync.removeAll { done.contains($0) }
                    record.listEntry.lastSyncedAt = Date()
                }
            }
            if failed { break }
        }
        if !synced.isEmpty && !failed {
            lastSyncError = nil
            lastSyncedAt = Date()
            defaults.set(lastSyncedAt, forKey: Self.lastSyncedKey)
        }
        updateToast(for: id, synced: synced, failed: failed)
        return synced
    }

    /// Sends every pending change, such as after connecting an account or
    /// returning online.
    func syncPending() async {
        guard !syncing, libraryReadable else { return }
        syncing = true
        defer { syncing = false }
        for game in games + missingGames where !pendingServices(game).isEmpty {
            await sync(game.id)
        }
        if pendingSyncCount == 0 {
            lastSyncedAt = Date()
            defaults.set(lastSyncedAt, forKey: Self.lastSyncedKey)
        }
    }

    private func push(_ entry: ListEntry, link: GameLink, to service: MetadataService) async throws {
        switch service {
        case .bangumi:
            guard let id = link.bangumiID else { return }
            if entry.status == nil {
                try await removeBangumiCollection(subjectID: id)
                return
            }
            let includeRating = settings.syncRatings
            try await withBangumi {
                try await $0.updateCollection(subjectID: id, entry: entry, includeRating: includeRating)
            }
        case .vndb:
            guard let id = link.vndbID else { return }
            try await vndbClient().updateList(
                vnID: id,
                entry: entry,
                includeRating: settings.syncRatings,
                includeDates: settings.syncDates
            )
        }
    }

    /// Removes the subject through Bangumi's website, which API v0 cannot
    /// do, and confirms through the API that it is gone. Nothing is sent
    /// when the collection already lacks the subject.
    private func removeBangumiCollection(subjectID: Int) async throws {
        guard let username = accounts[.bangumi]?.username else { return }
        guard try await withBangumi({ try await $0.collection(username: username, subjectID: subjectID) }) != nil else { return }
        try await BangumiWebSession.shared.removeCollection(subjectID: subjectID)
        if try await withBangumi({ try await $0.collection(username: username, subjectID: subjectID) }) != nil {
            throw BangumiWebError.notRemoved
        }
    }

    /// A status removed here but still on Bangumi is said out loud, once per
    /// game per launch, rather than left looking cleared. The change stays
    /// pending, so Sync Now retries it after signing in.
    private func reportRemovalFailure(_ error: Error, of game: GameRecord, service: MetadataService, entry: ListEntry) {
        guard service == .bangumi, entry.status == nil,
              !reportedRemovalFailures.contains(game.id) else { return }
        reportedRemovalFailures.insert(game.id)
        alert = String(localized: "“\(displayTitle(game))” is still in your Bangumi collection. \(error.localizedDescription)")
    }

    func refreshBangumiWebSession() async {
        bangumiWebSignedIn = (try? await BangumiWebSession.shared.isSignedIn()) == true
    }

    func signOutOfBangumiWeb() {
        BangumiWebSession.shared.signOut()
        bangumiWebSignedIn = false
    }

    /// Uses one of the services' images as the game's card art, replacing
    /// a cover the player set by hand.
    func chooseCardArt(_ candidate: ArtworkCandidate, for game: GameRecord) async {
        guard let current = record(game.id), current.link != nil else { return }
        let custom = current.customCoverName
        // A cover picked from the services' covers is the details page's
        // poster too; a screenshot only changes the card art.
        let isCover = current.link?.metadata.posters.contains { $0.url == candidate.url } == true
        updateRecord(game.id) { record in
            record.link?.bannerChoice = candidate.url
            record.link?.posterChoice = isCover ? candidate.url : nil
            record.customCoverName = nil
        }
        repository.removeArtwork(named: custom)
        await refreshArtwork(for: game.id)
    }

    private func updateToast(for id: UUID, synced: [MetadataService], failed: Bool) {
        guard var current = toast, current.gameID == id else { return }
        if failed {
            current.title = String(localized: "Saved. Will sync when possible")
        } else if synced.count >= 2 {
            current.title = String(localized: "Synced to Bangumi and VNDB")
        } else if let service = synced.first {
            current.title = String(localized: "Synced to \(service.displayName)")
        } else {
            return
        }
        toast = current
    }
}

extension Optional {
    /// `flatMap` for an async transform.
    func asyncFlatMap<T>(_ transform: (Wrapped) async throws -> T?) async rethrows -> T? {
        guard let value = self else { return nil }
        return try await transform(value)
    }
}
