import Foundation
import XCTest
@testable import VNCore

final class DiagnosticLogTests: XCTestCase {
    private var root: URL!

    override func setUpWithError() throws {
        root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try FileManager.default.removeItem(at: root)
    }

    private func records(_ url: URL) throws -> [[String: Any]] {
        try String(contentsOf: url, encoding: .utf8).split(separator: "\n").map {
            try XCTUnwrap(JSONSerialization.jsonObject(with: Data($0.utf8)) as? [String: Any])
        }
    }

    func testDefaultOffAndDisabledWritesPreserveExistingLogs() throws {
        let directory = root.appendingPathComponent("Logs")
        let log = DiagnosticLog(directory: directory)
        log.record("app", "not recorded")
        log.flush()
        XCTAssertFalse(FileManager.default.fileExists(atPath: directory.path))
        XCTAssertThrowsError(try log.export(to: root.appendingPathComponent("Exports")))
        try log.setEnabled(true)
        log.record("app", "before disable")
        try log.setEnabled(false)
        log.record("app", "after disable")
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let events = try records(output).compactMap { $0["event"] as? String }
        XCTAssertTrue(events.contains("before disable"))
        XCTAssertFalse(events.contains("after disable"))
    }

    func testOrderedJSONLinesWithMultilineAndRunMetadata() throws {
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        for index in 0..<100 {
            log.record("SDL", "message", fields: ["index": String(index), "message": "line one\nline two"])
        }
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let all = try records(output)
        let messages = all.filter { ($0["event"] as? String) == "message" }
        XCTAssertEqual(messages.count, 100)
        for (index, row) in messages.enumerated() {
            let fields = try XCTUnwrap(row["fields"] as? [String: String])
            XCTAssertEqual(fields["index"], String(index))
            XCTAssertEqual(fields["message"], "line one\nline two")
            XCTAssertNotNil(row["time"])
            XCTAssertNotNil(row["uptimeSeconds"])
        }
        XCTAssertEqual(Set(all.compactMap { $0["run"] as? String }).count, 1)
        try log.setEnabled(false)
    }

    func testRotationAndOversizedMessageAreBounded() throws {
        let directory = root.appendingPathComponent("Logs")
        let log = DiagnosticLog(directory: directory, maxBytes: 1024, fileCount: 3)
        try log.setEnabled(true)
        for index in 0..<30 {
            log.record("app", "event-\(index)", fields: ["payload": String(repeating: "😀", count: 4096)])
        }
        log.flush()
        let files = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.fileSizeKey])
        XCTAssertLessThanOrEqual(files.count, 3)
        for file in files {
            XCTAssertLessThanOrEqual(try XCTUnwrap(file.resourceValues(forKeys: [.fileSizeKey]).fileSize), 1024)
        }
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let events = try records(output).compactMap { $0["event"] as? String }
        XCTAssertEqual(events.last, "event-29")
        try log.setEnabled(false)
    }

    func testClearReopensWhenRecordingAndDeletesOnlyLogSegments() throws {
        let directory = root.appendingPathComponent("Logs")
        let log = DiagnosticLog(directory: directory)
        try log.setEnabled(true)
        log.record("app", "old")
        log.flush()
        let unrelated = directory.appendingPathComponent("keep.txt")
        try Data("keep".utf8).write(to: unrelated)
        try log.clear()
        log.record("app", "new")
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let events = try records(output).compactMap { $0["event"] as? String }
        XCTAssertEqual(events, ["new"])
        XCTAssertTrue(FileManager.default.fileExists(atPath: unrelated.path))
        try log.setEnabled(false)
    }

    func testEscapedControlCharactersCannotExceedSegmentLimit() throws {
        let directory = root.appendingPathComponent("Logs")
        let log = DiagnosticLog(directory: directory, maxBytes: 1024, fileCount: 1)
        try log.setEnabled(true)
        log.record(String(repeating: "\u{0}", count: 1024), String(repeating: "\u{0}", count: 16384))
        log.flush()
        let files = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.fileSizeKey])
        XCTAssertEqual(files.count, 1)
        XCTAssertLessThanOrEqual(try XCTUnwrap(files[0].resourceValues(forKeys: [.fileSizeKey]).fileSize), 1024)
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        XCTAssertFalse(try records(output).isEmpty)
        try log.setEnabled(false)
    }

    func testHeartbeatSizedRecordKeepsEveryField() throws {
        // Heartbeats carry dozens of counters. Dropping an arbitrary subset makes
        // a counter appear in some records and not others, so the same metric
        // cannot be compared over time.
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        var fields: [String: String] = [:]
        for index in 0..<43 { fields["counter\(index)"] = String(index) }
        for _ in 0..<20 { log.record("session", "heartbeat", fields: fields) }
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let beats = try records(output).filter { ($0["event"] as? String) == "heartbeat" }
        XCTAssertEqual(beats.count, 20)
        for row in beats {
            let written = try XCTUnwrap(row["fields"] as? [String: String])
            XCTAssertNil(written["fieldsOmitted"])
            for index in 0..<43 {
                XCTAssertEqual(written["counter\(index)"], String(index),
                               "counter\(index) missing from a heartbeat")
            }
        }
        try log.setEnabled(false)
    }

    func testFieldTruncationIsDeterministicAndReported() throws {
        // Past the limit, truncation must be stable across records and must say
        // what it dropped rather than losing fields silently.
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        let total = DiagnosticLog.fieldLimit + 15
        var fields: [String: String] = [:]
        for index in 0..<total { fields[String(format: "k%04d", index)] = String(index) }
        for _ in 0..<8 { log.record("session", "wide", fields: fields) }
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let rows = try records(output).filter { ($0["event"] as? String) == "wide" }
        XCTAssertEqual(rows.count, 8)
        var seen: Set<String>?
        for row in rows {
            let written = try XCTUnwrap(row["fields"] as? [String: String])
            // Two bookkeeping fields accompany the retained ones.
            XCTAssertEqual(written["fieldsOmitted"], "15")
            XCTAssertNotNil(written["fieldsOmittedKeys"])
            let kept = Set(written.keys.filter { $0.hasPrefix("k") })
            XCTAssertEqual(kept.count, DiagnosticLog.fieldLimit)
            // Sorted order means the lowest keys survive, every time.
            XCTAssertTrue(kept.contains("k0000"))
            XCTAssertFalse(kept.contains(String(format: "k%04d", total - 1)))
            if let first = seen { XCTAssertEqual(kept, first, "truncation differed between records") }
            seen = kept
        }
        try log.setEnabled(false)
    }

    func testLayerWorkProfilePreservesBoundedStructuredValuesThroughExport() throws {
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        let json = "[\"" + String(repeating: "汉字😀\\\"", count: 120) + "\"]"
        let fields = [
            "stages": String(repeating: "s", count: 1023),
            "transfers": String(repeating: "t", count: 2047),
            "transferOrigins": String(repeating: "o", count: 16383),
            "originOverflow": String(repeating: "v", count: 511),
            "frameSamplesNS": Array(repeating: "18446744073709551615/18446744073709551615", count: 2048).joined(separator: ","),
            "transitionProfiles": json,
            "transitionOverflow": String(repeating: "c", count: 1023),
            "shrinkProfiles": json,
            "shrinkOverflow": String(repeating: "b", count: 1023),
            "shrinkReadWaitSamplesNS": Array(repeating: "18446744073709551615", count: 2048).joined(separator: ",")
        ]
        for key in ["transfers", "transferOrigins", "frameSamplesNS", "transitionProfiles", "shrinkProfiles", "shrinkReadWaitSamplesNS"] {
            XCTAssertGreaterThan(try XCTUnwrap(fields[key]).utf8.count, 1024)
        }
        log.record("session", "layerWorkProfile", fields: fields)
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let sample = try XCTUnwrap(try records(output).first { ($0["event"] as? String) == "layerWorkProfile" })
        let written = try XCTUnwrap(sample["fields"] as? [String: String])
        XCTAssertEqual(written["structuredFieldLimitsVersion"], "1")
        XCTAssertNil(written["fieldsValueOmitted"])
        for (key, value) in fields { XCTAssertEqual(written[key], value, "Structured field \(key) was cut") }
        for key in ["transitionProfiles", "shrinkProfiles"] {
            let data = Data(try XCTUnwrap(written[key]).utf8)
            XCTAssertEqual((try JSONSerialization.jsonObject(with: data) as? [String])?.count, 1)
        }
        try log.setEnabled(false)
    }

    func testStructuredUTF8CapsOmitWholeValueAndReportKeys() throws {
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        // Exactly 16383 bytes, with a multibyte final scalar near the boundary.
        let boundary = String(repeating: "😀", count: 4095) + "abc"
        XCTAssertEqual(boundary.utf8.count, 16383)
        log.record("session", "layerWorkProfile", fields: ["transferOrigins": boundary])
        let oversized = DiagnosticLog.layerWorkFieldByteLimits.mapValues { String(repeating: "x", count: $0 + 1) }
        log.record("session", "layerWorkProfile", fields: oversized.merging(["ordinary": String(repeating: "q", count: 3000)]) { _, new in new })
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let samples = try records(output).filter { ($0["event"] as? String) == "layerWorkProfile" }
        XCTAssertEqual(samples.count, 2)
        let first = try XCTUnwrap(samples[0]["fields"] as? [String: String])
        XCTAssertEqual(first["transferOrigins"], boundary)
        let last = try XCTUnwrap(samples[1]["fields"] as? [String: String])
        XCTAssertEqual(last["fieldsValueOmitted"], String(oversized.count))
        XCTAssertEqual(last["fieldsValueOmittedKeys"], oversized.keys.sorted().joined(separator: ","))
        for key in oversized.keys { XCTAssertNil(last[key]) }
        XCTAssertEqual(last["ordinary"], String(repeating: "q", count: 1024))
        try log.setEnabled(false)
    }

    func testStructuredExceptionDoesNotExpandOrdinaryEventsAndRecordGuardStillApplies() throws {
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"), maxBytes: 1024)
        try log.setEnabled(true)
        log.record("session", "layerWorkProfile", fields: ["shrinkProfiles": "[\"" + String(repeating: "x", count: 3000) + "\"]"])
        log.record("session", "ordinary", fields: ["shrinkProfiles": String(repeating: "x", count: 3000)])
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let all = try records(output)
        let sample = try XCTUnwrap(all.first { ($0["event"] as? String) == "layerWorkProfile" })
        XCTAssertEqual((sample["fields"] as? [String: String])?["truncated"], "record exceeded segment limit")
        for file in try FileManager.default.contentsOfDirectory(at: root.appendingPathComponent("Logs"), includingPropertiesForKeys: [.fileSizeKey]) {
            XCTAssertLessThanOrEqual(try XCTUnwrap(file.resourceValues(forKeys: [.fileSizeKey]).fileSize), 1024)
        }
        try log.setEnabled(false)
        let ordinary = DiagnosticLog(directory: root.appendingPathComponent("OrdinaryLogs"))
        try ordinary.setEnabled(true)
        ordinary.record("session", "ordinary", fields: ["shrinkProfiles": String(repeating: "x", count: 3000)])
        let ordinaryOutput = try ordinary.export(to: root.appendingPathComponent("OrdinaryExports"))
        let ordinaryRow = try XCTUnwrap(try records(ordinaryOutput).first { ($0["event"] as? String) == "ordinary" })
        let ordinaryFields = try XCTUnwrap(ordinaryRow["fields"] as? [String: String])
        XCTAssertEqual(ordinaryFields["shrinkProfiles"], String(repeating: "x", count: 1024))
        XCTAssertNil(ordinaryFields["structuredFieldLimitsVersion"])
        try ordinary.setEnabled(false)
    }

    func testStructuredEscapedJSONRemainsValidWithoutSplittingScalars() throws {
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        let value = String(repeating: "\u{0}\"\\汉😀", count: 1200)
        let payload = try JSONSerialization.data(withJSONObject: [value])
        let encoded = try XCTUnwrap(String(data: payload, encoding: .utf8))
        XCTAssertGreaterThan(encoded.utf8.count, 1024)
        XCTAssertLessThan(encoded.utf8.count, DiagnosticLog.layerWorkFieldByteLimits["shrinkProfiles"]!)
        log.record("session", "layerWorkProfile", fields: ["shrinkProfiles": encoded])
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let sample = try XCTUnwrap(try records(output).first { ($0["event"] as? String) == "layerWorkProfile" })
        let copied = try XCTUnwrap((sample["fields"] as? [String: String])?["shrinkProfiles"])
        XCTAssertEqual(copied, encoded)
        XCTAssertEqual(try JSONSerialization.jsonObject(with: Data(copied.utf8)) as? [String], [value])
        try log.setEnabled(false)
    }

    func testEnableReportsUnwritableDirectory() throws {
        let file = root.appendingPathComponent("not-a-directory")
        try Data("file".utf8).write(to: file)
        let log = DiagnosticLog(directory: file.appendingPathComponent("Logs"))
        XCTAssertThrowsError(try log.setEnabled(true))
        XCTAssertNotNil(log.lastError)
        log.record("app", "must not crash")
        log.flush()
    }

    func testConcurrentCallbacksProduceValidIndependentRecords() throws {
        let log = DiagnosticLog(directory: root.appendingPathComponent("Logs"))
        try log.setEnabled(true)
        DispatchQueue.concurrentPerform(iterations: 64) { index in
            log.record("worker", "callback", fields: ["index": String(index), "message": "汉字\n😀"])
        }
        let output = try log.export(to: root.appendingPathComponent("Exports"))
        let rows = try records(output).filter { ($0["event"] as? String) == "callback" }
        XCTAssertEqual(rows.count, 64)
        let indices = rows.compactMap { ($0["fields"] as? [String: String])?["index"] }
        XCTAssertEqual(Set(indices).count, 64)
        try log.setEnabled(false)
    }
}
