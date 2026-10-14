import Darwin
import Foundation
import Metal

/// CPU used by every thread of the process since the previous sample.
struct ThreadCPUSample {
    struct Group {
        let name: String
        let threads: Int
        let percent: Double
    }

    /// Sum over all threads, as a percentage of one core (can exceed 100).
    let processPercent: Double
    let threadCount: Int
    /// Threads grouped by name, busiest first.
    let groups: [Group]

    /// "name×count:percent" entries, busiest first.
    func summary(limit: Int) -> String {
        groups.prefix(limit).map { group in
            let count = group.threads > 1 ? "×\(group.threads)" : ""
            return "\(group.name)\(count):\(String(format: "%.1f", group.percent))"
        }.joined(separator: ",")
    }
}

/// Samples per-thread CPU time from Mach. The engine's step profile only sees
/// the main thread, so worker threads (audio decode, movie demux, SDL audio)
/// were invisible; this is where idle busy-spins went unnoticed.
@MainActor
final class ThreadCPUSampler {
    private var previousCPU: [UInt64: UInt64] = [:]
    private var previousUptimeNS: UInt64 = 0
    private let mainThreadID: UInt64 = {
        var id: UInt64 = 0
        pthread_threadid_np(nil, &id)
        return id
    }()

    /// Returns nil for the first call, which only establishes the baseline.
    func sample() -> ThreadCPUSample? {
        var list: thread_act_array_t?
        var count: mach_msg_type_number_t = 0
        guard task_threads(mach_task_self_, &list, &count) == KERN_SUCCESS, let list else { return nil }
        defer {
            for index in 0..<Int(count) {
                mach_port_deallocate(mach_task_self_, list[index])
            }
            vm_deallocate(mach_task_self_, vm_address_t(UInt(bitPattern: list)),
                          vm_size_t(Int(count) * MemoryLayout<thread_act_t>.stride))
        }

        let now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW)
        var current: [UInt64: UInt64] = [:]
        var deltas: [String: (threads: Int, ns: UInt64)] = [:]
        var totalNS: UInt64 = 0
        for index in 0..<Int(count) {
            let thread = list[index]
            var identifier = thread_identifier_info_data_t()
            var identifierCount = mach_msg_type_number_t(
                MemoryLayout<thread_identifier_info_data_t>.size / MemoryLayout<natural_t>.size)
            let identifierResult = withUnsafeMutablePointer(to: &identifier) {
                $0.withMemoryRebound(to: integer_t.self, capacity: Int(identifierCount)) {
                    thread_info(thread, thread_flavor_t(THREAD_IDENTIFIER_INFO), $0, &identifierCount)
                }
            }
            var info = thread_extended_info_data_t()
            var infoCount = mach_msg_type_number_t(
                MemoryLayout<thread_extended_info_data_t>.size / MemoryLayout<natural_t>.size)
            let infoResult = withUnsafeMutablePointer(to: &info) {
                $0.withMemoryRebound(to: integer_t.self, capacity: Int(infoCount)) {
                    thread_info(thread, thread_flavor_t(THREAD_EXTENDED_INFO), $0, &infoCount)
                }
            }
            guard identifierResult == KERN_SUCCESS, infoResult == KERN_SUCCESS else { continue }

            let id = identifier.thread_id
            // Both fields are nanoseconds.
            let cpu = info.pth_user_time &+ info.pth_system_time
            current[id] = cpu
            // A thread first seen in this window has no baseline; count it from
            // the next sample rather than charging its whole lifetime here.
            guard let before = previousCPU[id], cpu >= before else { continue }
            let delta = cpu - before
            totalNS += delta
            let name = threadName(info, id: id)
            let entry = deltas[name] ?? (threads: 0, ns: 0)
            deltas[name] = (threads: entry.threads + 1, ns: entry.ns + delta)
        }

        let elapsed = previousUptimeNS > 0 && now > previousUptimeNS ? now - previousUptimeNS : 0
        previousCPU = current
        previousUptimeNS = now
        guard elapsed > 0 else { return nil }

        let scale = 100.0 / Double(elapsed)
        let groups = deltas.map { entry in
            ThreadCPUSample.Group(name: entry.key, threads: entry.value.threads,
                                  percent: Double(entry.value.ns) * scale)
        }.sorted { $0.percent > $1.percent }
        return ThreadCPUSample(processPercent: Double(totalNS) * scale,
                               threadCount: current.count, groups: groups)
    }

    private func threadName(_ info: thread_extended_info_data_t, id: UInt64) -> String {
        if id == mainThreadID { return "main" }
        let name = withUnsafeBytes(of: info.pth_name) { bytes in
            String(decoding: bytes.prefix { $0 != 0 }, as: UTF8.self)
        }
        return name.isEmpty ? "unnamed" : name
    }
}

/// Physical memory the app uses, in bytes; 0 if Mach doesn't say.
func processResidentMemoryBytes() -> UInt64 {
    var info = mach_task_basic_info()
    var count = mach_msg_type_number_t(
        MemoryLayout<mach_task_basic_info>.size / MemoryLayout<natural_t>.size
    )
    let result = withUnsafeMutablePointer(to: &info) { pointer in
        pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
            task_info(
                mach_task_self_,
                task_flavor_t(MACH_TASK_BASIC_INFO),
                $0,
                &count
            )
        }
    }
    return result == KERN_SUCCESS ? UInt64(info.resident_size) : 0
}

private let metalDevice = MTLCreateSystemDefaultDevice()

/// GPU memory the app's Metal resources hold, in bytes. Every engine and
/// SDL render on the system default device, so this covers all of them.
func metalAllocatedBytes() -> UInt64 {
    UInt64(metalDevice?.currentAllocatedSize ?? 0)
}
