import CoreText
import Foundation

/// System fonts as files the Rust engines (siglus_rs, rfvp) can load. They
/// draw text with their own rasterizers from font files, and the faces
/// games ask for (MS Gothic, MS Mincho) aren't on iOS, so the sessions
/// register system faces in their place.
enum SystemFontExport {
    /// The file an engine can load for the system face with this PostScript
    /// name, with the face to use in it: the system file itself when the app
    /// can read it and its outlines are TrueType or CFF, else a copy rebuilt
    /// through CoreText. PingFang's file lives in a private framework and,
    /// since iOS 18, keeps its outlines in Apple's own hvgl table.
    static func fontFiles(_ name: String) -> [(path: String, face: String)] {
        let font = CTFontCreateWithName(name as CFString, 16, nil)
        guard CTFontCopyPostScriptName(font) as String == name else {
            AppDiagnostics.shared.event("bridge", "font.missing", ["name": name])
            return []
        }
        if let url = CTFontCopyAttribute(font, kCTFontURLAttribute) as? URL {
            let readable = FileManager.default.isReadableFile(atPath: url.path)
            let outlines = hasStandardOutlines(font)
            AppDiagnostics.shared.event("bridge", "font.file", [
                "name": name, "path": url.path, "readable": String(readable), "outlines": String(outlines),
            ])
            if readable && outlines { return [(url.path, name)] }
        }
        return exportedFontFile(name).map { [($0.path, "")] } ?? []
    }

    private static func hasStandardOutlines(_ font: CTFont) -> Bool {
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
    private static func exportedFontFile(_ name: String) -> URL? {
        let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let directory = caches.appendingPathComponent("Mikage/Fonts", isDirectory: true)
        let system = ProcessInfo.processInfo.operatingSystemVersionString
            .filter { $0.isLetter || $0.isNumber || $0 == "." }
        let url = directory.appendingPathComponent("\(name)-v3-\(system).ttf")
        if FileManager.default.fileExists(atPath: url.path) { return url }
        let started = CFAbsoluteTimeGetCurrent()
        guard let exported = trueTypeData(name) else {
            AppDiagnostics.shared.event("bridge", "font.exportFailed", ["name": name])
            return nil
        }
        let (data, glyphs) = exported
        AppDiagnostics.shared.event("bridge", "font.export", [
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
            AppDiagnostics.shared.event("bridge", "font.exportFailed", ["name": name, "error": error.localizedDescription])
            return nil
        }
    }

    /// Builds a TrueType font (cmap, glyf, loca, hmtx and the tables they
    /// need) holding the glyphs of every character the face maps in planes
    /// 0 to 2, at one font unit per point.
    private static func trueTypeData(_ name: String) -> (Data, Int)? {
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
    private static func trueTypeContours(_ path: CGPath) -> [[TrueTypePoint]] {
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
    private static func sfnt(_ tables: [(tag: String, data: Data)]) -> Data {
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

    private static func checksum(_ data: Data) -> UInt32 {
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

    static func prefersTraditionalChinese(_ languageTag: String) -> Bool {
        let parts = languageTag.lowercased().split(separator: "-")
        guard parts.first == "zh" else { return false }
        return parts.contains("hant") || (!parts.contains("hans") && parts.contains { ["tw", "hk", "mo"].contains($0) })
    }
}

private extension Data {
    mutating func appendBE(_ value: UInt16) { Swift.withUnsafeBytes(of: value.bigEndian) { append(contentsOf: $0) } }
    mutating func appendBE(_ value: UInt32) { Swift.withUnsafeBytes(of: value.bigEndian) { append(contentsOf: $0) } }
}
