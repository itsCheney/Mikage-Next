import UIKit
import ImageIO

/// Decodes cover art at display size and keeps the results in memory.
///
/// Covers are user-supplied files of unbounded size, so the library grid must
/// not decode them at full resolution: a handful of phone-camera images held as
/// full-size `UIImage` is enough to get the app jettisoned. `ImageIO` produces a
/// thumbnail at the requested pixel size without ever materializing the full
/// bitmap, and `NSCache` evicts under memory pressure on its own.
final class CoverCache {
    static let shared = CoverCache()

    private let cache: NSCache<NSString, UIImage> = {
        let cache = NSCache<NSString, UIImage>()
        // Cost is measured in bytes below, not entry count.
        cache.totalCostLimit = 48 * 1024 * 1024
        return cache
    }()

    /// Serializes decoding so a fast scroll cannot start dozens of concurrent
    /// file reads for the same images.
    private let queue: DispatchQueue = {
        DispatchQueue(label: "moe.cheney233.mikage.covers", qos: .userInitiated)
    }()

    private init() {}

    /// Rounded up so that minor layout differences share one cache entry and
    /// one decode, instead of thrashing on every fractional width change.
    private func bucket(_ size: CGSize, scale: CGFloat) -> Int {
        let longest = max(size.width, size.height) * scale
        guard longest.isFinite, longest > 0 else { return 256 }
        return max(128, Int((longest / 64).rounded(.up)) * 64)
    }

    private func key(_ url: URL, pixels: Int) -> NSString {
        "\(url.path)|\(pixels)" as NSString
    }

    /// Returns an already-decoded cover, if one is in memory.
    func cached(_ url: URL, size: CGSize, scale: CGFloat) -> UIImage? {
        cache.object(forKey: key(url, pixels: bucket(size, scale: scale)))
    }

    func image(for url: URL, size: CGSize, scale: CGFloat) async -> UIImage? {
        let pixels = bucket(size, scale: scale)
        let cacheKey = key(url, pixels: pixels)
        if let hit = cache.object(forKey: cacheKey) { return hit }

        let decoded = await withCheckedContinuation { continuation in
            queue.async {
                continuation.resume(returning: Self.thumbnail(at: url, maxPixels: pixels))
            }
        }
        guard let decoded else { return nil }
        cache.setObject(decoded, forKey: cacheKey, cost: Self.byteCost(of: decoded))
        return decoded
    }

    /// Drops all decoded covers.
    ///
    /// A replaced custom cover reuses the same per-game filename, so its cached
    /// entries would otherwise keep showing the old art. `NSCache` cannot
    /// enumerate keys to evict one game's sizes, so this clears everything;
    /// cover replacement is rare and the grid re-decodes lazily.
    func invalidateAll() {
        cache.removeAllObjects()
    }

    private static func byteCost(of image: UIImage) -> Int {
        guard let cgImage = image.cgImage else { return 1 }
        return max(1, cgImage.bytesPerRow * cgImage.height)
    }

    private static func thumbnail(at url: URL, maxPixels: Int) -> UIImage? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, [
            kCGImageSourceShouldCache: false
        ] as CFDictionary) else { return nil }
        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            // Honor EXIF orientation; the grid draws the result directly.
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPixels
        ]
        guard let thumbnail = CGImageSourceCreateThumbnailAtIndex(
            source, 0, options as CFDictionary
        ) else { return nil }
        return UIImage(cgImage: thumbnail)
    }
}
