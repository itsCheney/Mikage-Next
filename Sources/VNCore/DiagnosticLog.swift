import Foundation

/// Bounded, ordered JSON-lines diagnostics. No game files are read by this logger.
public final class DiagnosticLog: @unchecked Sendable {
    private let queue = DispatchQueue(label: "moe.cheney233.mikage.diagnostics", qos: .utility)
    private let lock = NSLock()
    private let directory: URL
    private let maxBytes: Int
    private let fileCount: Int
    private var enabled = false
    private var pending = 0
    private var dropped = 0
    private var handle: FileHandle?
    private var size = 0
    private var lastSynchronizedAt = 0.0
    private var sequence = 0
    private let dateFormatter: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()
    private var failure: String?
    private let runID = UUID().uuidString
    private let startedAt = ProcessInfo.processInfo.systemUptime
    /// Maximum fields retained per record. Generous enough for the diagnostic
    /// heartbeats (dozens of counters); per-record size is bounded separately.
    static let fieldLimit = 128

    public init(directory: URL, maxBytes: Int = 2 * 1024 * 1024, fileCount: Int = 4) {
        self.directory = directory
        self.maxBytes = max(1024, maxBytes)
        self.fileCount = max(1, fileCount)
    }

    public var lastError: String? {
        lock.lock(); defer { lock.unlock() }
        return failure
    }

    public func setEnabled(_ value: Bool) throws {
        if !value { record("diagnostics", "recording.disabled") }
        lock.lock()
        enabled = false
        lock.unlock()
        try queue.sync {
            do {
                try handle?.synchronize()
                try handle?.close()
                handle = nil
                if value { try open() }
            } catch {
                report(error)
                throw error
            }
        }
        lock.lock()
        enabled = value
        failure = nil
        lock.unlock()
        if value { record("diagnostics", "recording.enabled") }
    }

    public func record(_ source: String, _ event: String, fields: [String: String] = [:]) {
        lock.lock()
        guard enabled else { lock.unlock(); return }
        guard pending < 128 else { dropped += 1; lock.unlock(); return }
        pending += 1
        let lost = dropped
        dropped = 0
        let timestamp = Date()
        let uptime = ProcessInfo.processInfo.systemUptime - startedAt
        let thread = Thread.isMainThread ? "main" : "background"
        let threadDetail = Self.clipped(String(describing: Thread.current), to: 256)
        let event = Self.clipped(event, to: 4096)
        let source = Self.clipped(source, to: 128)
        // Dictionary iteration order is unspecified, so truncating fields without
        // sorting first drops a different, arbitrary subset on every call. Callers
        // that emit more than the limit (heartbeats carry dozens of counters) then
        // produce records whose fields cannot be compared across time. Sort by key
        // so any truncation is at least deterministic, and say what was dropped
        // instead of losing it silently. Oversized records stay bounded by the
        // maxBytes checks below, not by this limit.
        var fieldSnapshot: [String: String] = [:]
        let ordered = fields.sorted { $0.key < $1.key }
        for (key, value) in ordered.prefix(Self.fieldLimit) {
            fieldSnapshot[Self.clipped(key, to: 128)] = Self.clipped(value, to: 1024)
        }
        if ordered.count > Self.fieldLimit {
            let omitted = ordered.dropFirst(Self.fieldLimit).map(\.key)
            fieldSnapshot["fieldsOmitted"] = String(omitted.count)
            fieldSnapshot["fieldsOmittedKeys"] = Self.clipped(omitted.joined(separator: ","), to: 1024)
        }
        let fieldsToWrite = fieldSnapshot
        queue.async {
            defer {
                self.lock.lock(); self.pending -= 1; self.lock.unlock()
            }
            guard self.handle != nil else { return }
            do {
                self.sequence += 1
                var values: [String: Any] = [
                    "time": self.dateFormatter.string(from: timestamp),
                    "unixTime": timestamp.timeIntervalSince1970,
                    "uptimeSeconds": uptime, "run": self.runID, "sequence": self.sequence,
                    "threadDetail": threadDetail,
                    "thread": thread, "source": source,
                    "event": event,
                    "fields": fieldsToWrite
                ]
                if lost > 0 { values["droppedRecords"] = lost }
                var data = try JSONSerialization.data(withJSONObject: values, options: [.sortedKeys])
                data.append(10)
                if data.count > self.maxBytes {
                    // Bound a single pathological message too, not only the file count.
                    values["source"] = Self.clipped(source, to: 32)
                    values["event"] = Self.clipped(event, to: 64)
                    values["fields"] = ["truncated": "record exceeded segment limit"]
                    data = try JSONSerialization.data(withJSONObject: values, options: [.sortedKeys])
                    data.append(10)
                }
                if data.count > self.maxBytes {
                    // JSON escaping can expand control characters by 6x.
                    data = try JSONSerialization.data(withJSONObject: [
                        "run": self.runID, "sequence": self.sequence,
                        "unixTime": timestamp.timeIntervalSince1970,
                        "source": "diagnostics", "event": "record.truncated"
                    ], options: [.sortedKeys])
                    data.append(10)
                }
                if self.size + data.count > self.maxBytes { try self.rotate() }
                try self.handle?.write(contentsOf: data)
                self.size += data.count
                if uptime - self.lastSynchronizedAt >= 1 {
                    try self.handle?.synchronize()
                    self.lastSynchronizedAt = uptime
                }
            } catch { self.report(error) }
        }
        // Enqueue while holding the ingress lock: disabling drains every
        // accepted callback, even if another thread changes the switch.
        lock.unlock()
    }

    public func flush() {
        queue.sync {
            do { try handle?.synchronize() } catch { report(error) }
        }
    }

    /// Exports a stable copy; never shares the actively written files.
    public func export(to destination: URL) throws -> URL {
        try queue.sync {
            try handle?.synchronize()
            let files = (0..<fileCount).reversed().map(fileURL).filter {
                FileManager.default.fileExists(atPath: $0.path)
            }
            guard !files.isEmpty else {
                throw NSError(domain: "MikageDiagnostics", code: 1,
                              userInfo: [NSLocalizedDescriptionKey: "暂无诊断日志，请开启记录并复现问题。"])
            }
            try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
            let output = destination.appendingPathComponent("Mikage-diagnostics-\(UUID().uuidString).jsonl")
            FileManager.default.createFile(atPath: output.path, contents: nil)
            let target = try FileHandle(forWritingTo: output)
            do {
                for file in files {
                    let input = try FileHandle(forReadingFrom: file)
                    defer { try? input.close() }
                    while let chunk = try input.read(upToCount: 128 * 1024), !chunk.isEmpty {
                        try target.write(contentsOf: chunk)
                    }
                }
                try target.synchronize()
                try target.close()
                return output
            } catch {
                try? target.close()
                try? FileManager.default.removeItem(at: output)
                throw error
            }
        }
    }

    public func clear() throws {
        try queue.sync {
            do {
                let reopen = handle != nil
                try handle?.close()
                handle = nil
                for index in 0..<fileCount {
                    let file = fileURL(index)
                    if FileManager.default.fileExists(atPath: file.path) {
                        try FileManager.default.removeItem(at: file)
                    }
                }
                if reopen { try open() }
            } catch {
                report(error)
                throw error
            }
        }
    }

    private func fileURL(_ index: Int) -> URL {
        directory.appendingPathComponent("diagnostics-\(index).jsonl")
    }

    private static func clipped(_ value: String, to bytes: Int) -> String {
        String(decoding: value.utf8.prefix(bytes), as: UTF8.self)
    }

    private func open() throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let file = fileURL(0)
        if !FileManager.default.fileExists(atPath: file.path) {
            FileManager.default.createFile(atPath: file.path, contents: nil)
        }
        handle = try FileHandle(forWritingTo: file)
        size = Int(try handle!.seekToEnd())
    }

    private func rotate() throws {
        try handle?.synchronize()
        try handle?.close()
        handle = nil
        let manager = FileManager.default
        if manager.fileExists(atPath: fileURL(fileCount - 1).path) {
            try manager.removeItem(at: fileURL(fileCount - 1))
        }
        if fileCount > 1 {
            for index in stride(from: fileCount - 2, through: 0, by: -1) {
                if manager.fileExists(atPath: fileURL(index).path) {
                    try manager.moveItem(at: fileURL(index), to: fileURL(index + 1))
                }
            }
        }
        try open()
    }

    private func report(_ error: Error) {
        lock.lock(); failure = error.localizedDescription; enabled = false; lock.unlock()
        try? handle?.close()
        handle = nil
    }
}
