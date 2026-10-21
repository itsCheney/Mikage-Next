import Accelerate
import AVFoundation
import Foundation
import SiglusRuntime

/// Decodes the movies siglus_rs can't, with AVFoundation. Translation repacks
/// sometimes replace a game's MPEG movie with an MP4 (H.264 and AAC) and keep
/// the .mpg name: Rewrite's Chinese release does this for its OP. siglus_rs
/// sends files that start with an ISO "ftyp" box here and calls these on its
/// movie worker threads.
enum SiglusMovieDecoder {
    static func register() {
        siglus_mikage_set_movie_decoder(
            { path, wantAudio, info in
                guard let path, let info else { return nil }
                let url = URL(fileURLWithPath: String(cString: path))
                let reader = wantAudio != 0 ? SiglusMovieReader(audioOf: url) : SiglusMovieReader(videoOf: url)
                guard let reader else { return nil }
                info.pointee = reader.info
                return Unmanaged.passRetained(reader).toOpaque()
            },
            { movie, rgba, pts in
                guard let movie, let rgba, let pts else { return -1 }
                return Unmanaged<SiglusMovieReader>.fromOpaque(movie).takeUnretainedValue().readVideo(into: rgba, pts: pts)
            },
            { movie, samples, maxFrames in
                guard let movie, let samples else { return -1 }
                return Unmanaged<SiglusMovieReader>.fromOpaque(movie).takeUnretainedValue().readAudio(into: samples, maxFrames: Int(maxFrames))
            },
            { movie in
                guard let movie else { return }
                Unmanaged<SiglusMovieReader>.fromOpaque(movie).release()
            }
        )
    }
}

/// One track of a movie, read in order.
private final class SiglusMovieReader {
    private let reader: AVAssetReader
    private let output: AVAssetReaderTrackOutput
    private(set) var info = siglus_mikage_movie_info()
    /// Audio decoded but not handed out yet, interleaved.
    private var pendingSamples: [Int16] = []
    private var pendingOffset = 0

    init?(videoOf url: URL) {
        let asset = AVURLAsset(url: url)
        guard let track = Self.wait({ try await asset.loadTracks(withMediaType: .video) })?.first,
              let size = Self.wait({ try await track.load(.naturalSize) }),
              size.width >= 1, size.height >= 1,
              let reader = try? AVAssetReader(asset: asset) else {
            return nil
        }
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        ])
        output.alwaysCopiesSampleData = false
        guard reader.canAdd(output) else { return nil }
        reader.add(output)
        guard reader.startReading() else { return nil }
        self.reader = reader
        self.output = output
        info.width = UInt32(size.width.rounded())
        info.height = UInt32(size.height.rounded())
        info.duration_ms = Self.durationMs(of: asset)
    }

    init?(audioOf url: URL) {
        let asset = AVURLAsset(url: url)
        guard let track = Self.wait({ try await asset.loadTracks(withMediaType: .audio) })?.first,
              let formats = Self.wait({ try await track.load(.formatDescriptions) }),
              let format = formats.first,
              let source = CMAudioFormatDescriptionGetStreamBasicDescription(format)?.pointee,
              source.mSampleRate > 0, source.mChannelsPerFrame > 0,
              let reader = try? AVAssetReader(asset: asset) else {
            return nil
        }
        let channels = min(Int(source.mChannelsPerFrame), 2)
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
            AVNumberOfChannelsKey: channels,
            AVSampleRateKey: source.mSampleRate,
        ])
        guard reader.canAdd(output) else { return nil }
        reader.add(output)
        guard reader.startReading() else { return nil }
        self.reader = reader
        self.output = output
        info.channels = UInt32(channels)
        info.sample_rate = UInt32(source.mSampleRate.rounded())
        info.duration_ms = Self.durationMs(of: asset)
    }

    deinit {
        reader.cancelReading()
    }

    /// 1 with a frame written, 0 at the end, -1 on failure.
    func readVideo(into rgba: UnsafeMutablePointer<UInt8>, pts: UnsafeMutablePointer<UInt64>) -> Int32 {
        guard let sample = output.copyNextSampleBuffer() else {
            return reader.status == .failed ? -1 : 0
        }
        guard let image = CMSampleBufferGetImageBuffer(sample) else { return -1 }
        let time = CMSampleBufferGetPresentationTimeStamp(sample)
        pts.pointee = time.isNumeric ? UInt64(max(0, time.seconds * 1000).rounded()) : 0

        let width = Int(info.width)
        let height = Int(info.height)
        memset(rgba, 0, width * height * 4)
        CVPixelBufferLockBaseAddress(image, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(image, .readOnly) }
        guard let base = CVPixelBufferGetBaseAddress(image) else { return -1 }
        var source = vImage_Buffer(data: base,
                                   height: vImagePixelCount(min(height, CVPixelBufferGetHeight(image))),
                                   width: vImagePixelCount(min(width, CVPixelBufferGetWidth(image))),
                                   rowBytes: CVPixelBufferGetBytesPerRow(image))
        var target = vImage_Buffer(data: rgba, height: source.height, width: source.width, rowBytes: width * 4)
        let bgraToRgba: [UInt8] = [2, 1, 0, 3]
        return vImagePermuteChannels_ARGB8888(&source, &target, bgraToRgba, vImage_Flags(kvImageNoFlags)) == kvImageNoError ? 1 : -1
    }

    /// The number of frames written, 0 at the end, -1 on failure.
    func readAudio(into samples: UnsafeMutablePointer<Int16>, maxFrames: Int) -> Int64 {
        let channels = Int(info.channels)
        while pendingOffset >= pendingSamples.count {
            guard let sample = output.copyNextSampleBuffer() else {
                return reader.status == .failed ? -1 : 0
            }
            guard let block = CMSampleBufferGetDataBuffer(sample) else { continue }
            let length = CMBlockBufferGetDataLength(block)
            var decoded = [Int16](repeating: 0, count: length / 2)
            let status = decoded.withUnsafeMutableBytes {
                CMBlockBufferCopyDataBytes(block, atOffset: 0, dataLength: $0.count, destination: $0.baseAddress!)
            }
            guard status == kCMBlockBufferNoErr else { return -1 }
            pendingSamples = decoded
            pendingOffset = 0
        }
        let count = min(maxFrames * channels, (pendingSamples.count - pendingOffset) / channels * channels)
        pendingSamples.withUnsafeBufferPointer {
            samples.update(from: $0.baseAddress! + pendingOffset, count: count)
        }
        pendingOffset += count
        return Int64(count / channels)
    }

    private static func durationMs(of asset: AVURLAsset) -> UInt64 {
        guard let duration = wait({ try await asset.load(.duration) }), duration.isNumeric else { return 0 }
        return UInt64(max(0, duration.seconds * 1000).rounded())
    }

    /// Runs an AVFoundation load from a movie worker thread, which may block.
    private static func wait<T>(_ operation: @escaping @Sendable () async throws -> T) -> T? {
        let box = SiglusLoadResult<T>()
        let done = DispatchSemaphore(value: 0)
        Task.detached {
            box.value = try? await operation()
            done.signal()
        }
        done.wait()
        return box.value
    }
}

private final class SiglusLoadResult<T>: @unchecked Sendable {
    var value: T?
}
