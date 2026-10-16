// Exercises the production layerExSave PNG encoder: decoded output matches the
// source pixels, progress is monotonic, and a cancel during row filtering stops
// the encoder before compression starts.

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

typedef std::uint8_t tjs_uint8;
typedef std::uint32_t tjs_uint32;
typedef std::int32_t tjs_int;
typedef std::int64_t tjs_int64;

namespace {

typedef std::vector<tjs_uint8> Bytes;
typedef std::function<bool(int percent)> ProgressFunc;

struct Image
{
    long w = 0, h = 0;
    Bytes px;
    const tjs_uint8* Line(long y) const { return px.data() + (size_t)w * 4 * y; }
};

#include "ProductionPngEncoder.inc"

void Check(bool ok, const std::string& what)
{
    if (!ok)
        throw std::runtime_error(what);
}

Image MakeImage(long w, long h, bool opaque)
{
    Image image;
    image.w = w;
    image.h = h;
    image.px.resize((size_t)w * h * 4);
    std::uint32_t seed = 12345;
    for (long y = 0; y < h; y++)
        for (long x = 0; x < w; x++)
        {
            tjs_uint8* p = image.px.data() + ((size_t)y * w + x) * 4;
            seed = seed * 1103515245u + 12345u;
            p[0] = (tjs_uint8)(x * 3 + y);
            p[1] = (tjs_uint8)(y * 5 + (seed >> 24) % 4);
            p[2] = (tjs_uint8)(x ^ y);
            p[3] = opaque ? 255 : (tjs_uint8)(x + y * 7);
        }
    return image;
}

tjs_uint32 BE32(const Bytes& b, size_t pos)
{
    return (tjs_uint32)b[pos] << 24 | (tjs_uint32)b[pos + 1] << 16 | (tjs_uint32)b[pos + 2] << 8 | b[pos + 3];
}

// Minimal decoder for the encoder's own output: RGB/RGBA, 8-bit, one IDAT.
Bytes DecodeToBgra(const Bytes& png, long& w, long& h)
{
    Check(png.size() > 8 && std::memcmp(png.data(), "\x89PNG\x0D\x0A\x1A\x0A", 8) == 0, "signature");
    size_t pos = 8;
    Bytes idat;
    int colorType = -1;
    bool end = false;
    while (pos + 12 <= png.size() && !end)
    {
        const tjs_uint32 len = BE32(png, pos);
        const std::string type((const char*)png.data() + pos + 4, 4);
        Check(pos + 12 + len <= png.size(), "chunk length");
        const uLong crc = crc32(0L, png.data() + pos + 4, len + 4);
        Check(crc == BE32(png, pos + 8 + len), "chunk crc " + type);
        const tjs_uint8* data = png.data() + pos + 8;
        if (type == "IHDR")
        {
            w = (long)BE32(png, pos + 8);
            h = (long)BE32(png, pos + 12);
            Check(data[8] == 8, "bit depth");
            colorType = data[9];
        }
        else if (type == "IDAT")
            idat.insert(idat.end(), data, data + len);
        else if (type == "IEND")
            end = true;
        pos += 12 + len;
    }
    Check(end && pos == png.size(), "IEND");
    Check(colorType == 2 || colorType == 6, "color type");
    const size_t bpp = colorType == 6 ? 4 : 3;
    const size_t rowBytes = (size_t)w * bpp;
    Bytes raw((rowBytes + 1) * h);
    uLongf rawSize = (uLongf)raw.size();
    Check(uncompress(raw.data(), &rawSize, idat.data(), (uLong)idat.size()) == Z_OK, "inflate");
    Check(rawSize == raw.size(), "inflated size");

    Bytes out((size_t)w * h * 4);
    Bytes prev(rowBytes, 0), cur(rowBytes);
    for (long y = 0; y < h; y++)
    {
        const tjs_uint8* row = raw.data() + (rowBytes + 1) * y;
        const int filter = row[0];
        Check(filter <= 4, "filter type");
        for (size_t x = 0; x < rowBytes; x++)
        {
            const int a = x >= bpp ? cur[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
            int pred = 0;
            switch (filter)
            {
                case 1: pred = a; break;
                case 2: pred = b; break;
                case 3: pred = (a + b) >> 1; break;
                case 4: pred = Paeth(a, b, c); break;
            }
            cur[x] = (tjs_uint8)(row[1 + x] + pred);
        }
        for (long x = 0; x < w; x++)
        {
            tjs_uint8* q = out.data() + ((size_t)y * w + x) * 4;
            const tjs_uint8* p = cur.data() + x * bpp;
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = bpp == 4 ? p[3] : 255;
        }
        prev.swap(cur);
    }
    return out;
}

void TestRoundTrip(bool opaque, int level)
{
    const Image image = MakeImage(97, 61, opaque);
    PngOptions options;
    options.level = level;
    Bytes silent, reported;
    Check(!EncodePng(image, options, silent, ProgressFunc()), "silent encode canceled");
    std::vector<int> seen;
    Check(!EncodePng(image, options, reported, [&](int percent) {
              seen.push_back(percent);
              return false;
          }),
          "reported encode canceled");
    Check(silent == reported, "progress reporting changes the output");
    long w = 0, h = 0;
    Check(DecodeToBgra(silent, w, h) == image.px && w == image.w && h == image.h, "decoded pixels differ");
    Check(!seen.empty() && seen.front() == 0 && seen.back() == 100, "progress range");
    for (size_t i = 1; i < seen.size(); i++)
        Check(seen[i] > seen[i - 1], "progress not increasing");
    Check(std::count_if(seen.begin(), seen.end(), [](int p) { return p > 0 && p < 50; }) > 10,
          "filtering reports no progress");
    Check(std::count_if(seen.begin(), seen.end(), [](int p) { return p >= 50 && p < 100; }) > 0,
          "deflate reports no progress");
}

void TestCancelDuringFiltering(int level)
{
    const Image image = MakeImage(64, 400, false);
    PngOptions options;
    options.level = level;
    Bytes out;
    int last = -1, calls = 0;
    const bool canceled = EncodePng(image, options, out, [&](int percent) {
        last = percent;
        calls++;
        return percent >= 10;
    });
    Check(canceled, "cancel ignored");
    Check(last == 10 && calls == 11, "cancel not prompt");
}

void TestCancelAtStart()
{
    const Image image = MakeImage(8, 8, true);
    PngOptions options;
    Bytes out;
    int calls = 0;
    Check(EncodePng(image, options, out, [&](int) {
              calls++;
              return true;
          }),
          "immediate cancel ignored");
    Check(calls == 1 && out.empty(), "work after immediate cancel");
}

} // namespace

int main()
{
    struct Case
    {
        const char* name;
        std::function<void()> run;
    } cases[] = {
        {"round trip RGBA default level", [] { TestRoundTrip(false, Z_DEFAULT_COMPRESSION); }},
        {"round trip RGB level 9", [] { TestRoundTrip(true, 9); }},
        {"round trip RGBA level 0", [] { TestRoundTrip(false, 0); }},
        {"cancel during adaptive filtering", [] { TestCancelDuringFiltering(Z_DEFAULT_COMPRESSION); }},
        {"cancel during stored rows", [] { TestCancelDuringFiltering(0); }},
        {"cancel at start", TestCancelAtStart},
    };
    int failed = 0;
    for (const Case& c : cases)
    {
        try
        {
            c.run();
            std::cout << "ok   " << c.name << "\n";
        }
        catch (const std::exception& e)
        {
            std::cout << "FAIL " << c.name << ": " << e.what() << "\n";
            failed++;
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
