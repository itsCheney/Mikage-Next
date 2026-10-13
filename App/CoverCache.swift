import UIKit
import ImageIO
import CoreImage

/// Decodes cover art at display size and keeps the results in memory.
///
/// Covers are user-supplied files of unbounded size, so the library grid must
/// not decode them at full resolution: a handful of phone-camera images held as
/// full-size `UIImage` is enough to get the app jettisoned. `ImageIO` produces a
/// thumbnail at the requested pixel size without ever materializing the full
/// bitmap, and `NSCache` evicts under memory pressure on its own.
final class CoverCache: ObservableObject {
    static let shared = CoverCache()

    /// Bumped whenever covers are dropped. A replaced custom cover keeps its
    /// file name, so views key their loads on this as well as the path, or
    /// they would keep showing the image they already decoded.
    @Published private(set) var generation = 0

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

    /// Processed continue-card artwork, keyed like `cache`.
    private let heroCache: NSCache<NSString, HeroArtwork> = {
        let cache = NSCache<NSString, HeroArtwork>()
        cache.countLimit = 4
        return cache
    }()

    /// Shared Core Image context; creating one per render is expensive.
    private let ciContext = CIContext(options: [.cacheIntermediates: false])

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
        heroCache.removeAllObjects()
        if Thread.isMainThread {
            generation += 1
        } else {
            DispatchQueue.main.async { self.generation += 1 }
        }
    }

    /// The continue card's cover with its derived backdrop: a small,
    /// pre-blurred copy and the color of the cover's top edge. Computed once
    /// per cover and size off the main thread, so the library body only draws
    /// finished bitmaps.
    func heroArtwork(for url: URL, size: CGSize, scale: CGFloat) async -> HeroArtwork? {
        let cacheKey = key(url, pixels: bucket(size, scale: scale))
        if let hit = heroCache.object(forKey: cacheKey) { return hit }
        guard let cover = await image(for: url, size: size, scale: scale) else { return nil }

        let artwork = await withCheckedContinuation { continuation in
            queue.async {
                continuation.resume(returning: self.processHero(cover))
            }
        }
        if let artwork { heroCache.setObject(artwork, forKey: cacheKey) }
        return artwork
    }

    /// Returns already-processed continue-card artwork, if any.
    func cachedHeroArtwork(_ url: URL, size: CGSize, scale: CGFloat) -> HeroArtwork? {
        heroCache.object(forKey: key(url, pixels: bucket(size, scale: scale)))
    }

    private func processHero(_ cover: UIImage) -> HeroArtwork? {
        guard let cgImage = cover.cgImage else { return nil }
        let input = CIImage(cgImage: cgImage)
        let extent = input.extent

        // Blur a small copy: the backdrop is drawn far larger than this, so
        // the result looks the same as blurring the full image and costs a
        // fraction of it. 160 px across a ~390 pt hero puts the 12 px radius
        // at roughly 30 pt on screen.
        let shrink = min(1, 160 / max(extent.width, 1))
        let small = input.transformed(by: CGAffineTransform(scaleX: shrink, y: shrink))
        let blurred = small
            .clampedToExtent()
            .applyingGaussianBlur(sigma: 12)
            .cropped(to: small.extent)
        guard let blurredImage = ciContext.createCGImage(blurred, from: small.extent) else { return nil }

        // Core Image's origin is bottom-left, so the top 20% of the cover is
        // the band at the highest y values.
        let topBand = CGRect(
            x: extent.minX,
            y: extent.maxY - extent.height * 0.2,
            width: extent.width,
            height: extent.height * 0.2
        )
        let topColor = averageColor(of: input, in: topBand) ?? UIColor(white: 0.2, alpha: 1)

        return HeroArtwork(
            cover: cover,
            blurred: UIImage(cgImage: blurredImage),
            topColor: topColor
        )
    }

    private func averageColor(of image: CIImage, in rect: CGRect) -> UIColor? {
        guard let filter = CIFilter(name: "CIAreaAverage", parameters: [
            kCIInputImageKey: image,
            kCIInputExtentKey: CIVector(cgRect: rect)
        ]), let output = filter.outputImage else { return nil }
        var pixel = [UInt8](repeating: 0, count: 4)
        ciContext.render(
            output,
            toBitmap: &pixel,
            rowBytes: 4,
            bounds: CGRect(x: 0, y: 0, width: 1, height: 1),
            format: .RGBA8,
            colorSpace: CGColorSpaceCreateDeviceRGB()
        )
        return UIColor(
            red: CGFloat(pixel[0]) / 255,
            green: CGFloat(pixel[1]) / 255,
            blue: CGFloat(pixel[2]) / 255,
            alpha: 1
        )
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

/// Continue-card artwork prepared by `CoverCache.heroArtwork(for:size:scale:)`.
final class HeroArtwork {
    let cover: UIImage
    let blurred: UIImage
    /// Average color of the cover's top fifth, extended up to the status bar.
    let topColor: UIColor

    init(cover: UIImage, blurred: UIImage, topColor: UIColor) {
        self.cover = cover
        self.blurred = blurred
        self.topColor = topColor
    }

    var aspectRatio: CGFloat {
        guard cover.size.height > 0 else { return 16 / 9 }
        return cover.size.width / cover.size.height
    }
}
