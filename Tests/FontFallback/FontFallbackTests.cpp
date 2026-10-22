// Production font enumeration (TVPFont.cpp), FreeType faces (FreeTypeImp.cpp)
// and FreeTypeFontRasterizer.cpp against real system fonts. Storage and
// platform services are stubbed; the default font is Liberation Sans, then the
// WenQuanYi Zen Hei collection (3 faces) and IPAGothic are registered.
#include "tjsCommHead.h"
#include "TVPFont.h"
#include "TVPStorage.h"
#include "TVPMsg.h"
#include "PlatformFile.h"
#include "FreeTypeImp.h"
#include "FreeTypeFontRasterizer.h"
#include "CharacterData.h"
#include "ComplexRect.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>

static const char* kLatin = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";
static const char* kZenHei = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc";
static const char* kIPA = "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf";

//--------------------------------------------------------------------------- stubs
ttstr TVPProjectDir;
static std::map<std::string, int> StreamOpens;

class tFileStream : public tTJSBinaryStream
{
    FILE* File;
    tjs_uint64 Size;

public:
    explicit tFileStream(FILE* f) : File(f)
    {
        fseek(File, 0, SEEK_END);
        Size = ftell(File);
        fseek(File, 0, SEEK_SET);
    }
    ~tFileStream() { fclose(File); }
    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence)
    {
        fseek(File, (long)offset, whence == TJS_BS_SEEK_SET   ? SEEK_SET
                                  : whence == TJS_BS_SEEK_CUR ? SEEK_CUR
                                                              : SEEK_END);
        return ftell(File);
    }
    tjs_uint Read(void* buffer, tjs_uint size) { return (tjs_uint)fread(buffer, 1, size, File); }
    tjs_uint Write(const void*, tjs_uint) { return 0; }
    bool Flush() { return true; }
    tjs_uint64 GetSize() { return Size; }
};

static tTJSBinaryStream* OpenFile(const ttstr& name)
{
    std::string path = name.AsStdString();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return nullptr;
    StreamOpens[path]++;
    return new tFileStream(f);
}
bool TVPIsExistentStorageNoSearch(const ttstr& name)
{
    return access(name.AsStdString().c_str(), R_OK) == 0;
}
tTJSBinaryStream* TVPCreateStream(const ttstr& name, tjs_uint32) { return OpenFile(name); }
tTJSBinaryStream* TVPCreateBinaryStreamForRead(const ttstr& name, const ttstr&)
{
    return OpenFile(name);
}
tTVPMemoryStream* GetResourceStream(const ttstr&) { return nullptr; }
void TVPGetLocalFileListAt(const ttstr&, const std::function<void(const ttstr&, tTVPLocalFileInfo*)>&) {}
ttstr TVPGetLocallyAccessibleName(const ttstr& name) { return name; }
void TVPAddLog(const ttstr& line) { std::fprintf(stderr, "log: %s\n", line.AsStdString().c_str()); }
void TVPConsoleLog(const tjs_char*, ...) {}
tjs_uint64 TVPGetRoughTickCount() { return 0; }
int TVPShowSimpleMessageBox(const ttstr&, const ttstr&) { return 0; }
void TVPThrowExceptionMessage(const tjs_char* message) { throw std::runtime_error(message); }
void TVPThrowExceptionMessage(const tjs_char* message, const ttstr&) { throw std::runtime_error(message); }
tTJSMessageHolder TVPCannotOpenFontFile(TJS_N("TVPCannotOpenFontFile"), TJS_N("cannot open font"));
tTJSMessageHolder TVPFontCannotBeUsed(TJS_N("TVPFontCannotBeUsed"), TJS_N("font cannot be used"));
tTJSMessageHolder TVPFontRasterizeError(TJS_N("TVPFontRasterizeError"), TJS_N("rasterize error"));
// Blur / 1bpp expansion are not exercised here.
#include "tvpgl.h"
extern "C" {
decltype(TVPChBlurCopy) TVPChBlurCopy = nullptr;
decltype(TVPChBlurCopy65) TVPChBlurCopy65 = nullptr;
decltype(TVPChBlurMulCopy) TVPChBlurMulCopy = nullptr;
decltype(TVPChBlurMulCopy65) TVPChBlurMulCopy65 = nullptr;
decltype(TVPBLExpand1BitTo8BitPal) TVPBLExpand1BitTo8BitPal = nullptr;
}

//--------------------------------------------------------------------------- private access
// Explicit instantiation may name private members; this reads the fallback
// state without changing the production class.
template<auto M>
struct tStealFallbacks
{
    friend auto& Fallbacks(FreeTypeFontRasterizer& r) { return r.*M; }
};
template<auto M>
struct tStealCharCache
{
    friend auto& CharCache(FreeTypeFontRasterizer& r) { return r.*M; }
};
auto& Fallbacks(FreeTypeFontRasterizer& r);
auto& CharCache(FreeTypeFontRasterizer& r);
template struct tStealFallbacks<&FreeTypeFontRasterizer::Fallbacks>;
template struct tStealCharCache<&FreeTypeFontRasterizer::FallbackForChar>;

//--------------------------------------------------------------------------- checks
static int Failures = 0;
#define CHECK(cond)                                                                   \
    do                                                                                \
    {                                                                                 \
        if (!(cond))                                                                  \
        {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++Failures;                                                               \
        }                                                                             \
    } while (0)

static int TotalOpens()
{
    int n = 0;
    for (auto& entry : StreamOpens)
        n += entry.second;
    return n;
}

static tTVPFontAndCharacterData Char(const tTVPFont& font, tjs_wchar ch)
{
    tTVPFontAndCharacterData data;
    data.Font = font;
    data.FontHash = 0;
    data.Character = ch;
    data.BlurLevel = data.BlurWidth = 0;
    data.Antialiased = true;
    data.Blured = false;
    data.Hinting = true;
    return data;
}

// Absolute rows (OriginY based) that are fully covered: the drawn line.
static std::set<tjs_int> LineRows(tTVPCharacterData* data)
{
    std::set<tjs_int> rows;
    for (tjs_uint y = 0; y < data->BlackBoxY; y++)
    {
        const tjs_uint8* p = data->GetData() + y * data->Pitch;
        bool full = data->BlackBoxX > 0;
        for (tjs_uint x = 0; x < data->BlackBoxX; x++)
            full = full && p[x] == 255;
        if (full)
            rows.insert(data->OriginY + (tjs_int)y);
    }
    return rows;
}

static tTVPFont MakeFont(const char* face, tjs_uint32 flags)
{
    tTVPFont font;
    font.Height = -32;
    font.Flags = flags;
    font.Angle = 0;
    font.Face = face;
    return font;
}

int main()
{
    for (const char* path : {kLatin, kZenHei, kIPA})
    {
        if (access(path, R_OK) != 0)
        {
            std::fprintf(stderr, "skipping: %s not installed\n", path);
            return 77;
        }
    }
    char dir[] = "/tmp/font-fallback-XXXXXX";
    if (!mkdtemp(dir))
        return 1;
    std::string project = std::string(dir) + "/";
    symlink(kLatin, (project + "default.ttf").c_str());
    TVPProjectDir = project;

    TVPFindFont(TJS_N("x")); // registers default.ttf
    CHECK(TVPGetDefaultFontName() == TJS_N("Liberation Sans"));
    CHECK(TVPEnumFontsProc(kZenHei) == 3);
    CHECK(TVPEnumFontsProc(kIPA) == 1);

    // 1. Every name of a collection records its own face; CJK names decode.
    struct
    {
        const char* name;
        int index;
    } names[] = {{"WenQuanYi Zen Hei", 0},      {"文泉驿正黑", 0}, {"文泉驛正黑", 0},
                 {"WenQuanYi Zen Hei Mono", 1}, {"文泉驿等宽正黑", 1},
                 {"WenQuanYi Zen Hei Sharp", 2}, {"文泉驿点阵正黑", 2}, {"IPAゴシック", 0}};
    for (auto& entry : names)
    {
        TVPFontNamePathInfo* info = TVPFindFont(entry.name);
        CHECK(info != nullptr);
        if (info)
            CHECK(info->Index == entry.index);
    }
    {
        // The face opened for a name is the registered one.
        tFreeTypeFace mono(TJS_N("文泉驿等宽正黑"), 0);
        CHECK(std::string(mono.GetBaseFace()->GetFTFace()->family_name) ==
              "WenQuanYi Zen Hei Mono");
        tFreeTypeFace sharp(TJS_N("WenQuanYi Zen Hei Sharp"), 0);
        CHECK(std::string(sharp.GetBaseFace()->GetFTFace()->family_name) ==
              "WenQuanYi Zen Hei Sharp");
    }
    {
        // The oldest registration (the default font) is listed too.
        std::vector<ttstr> list;
        TVPGetAllFontList(list);
        bool hasDefault = false;
        for (auto& name : list)
            hasDefault = hasDefault || name == TJS_N("Liberation Sans");
        CHECK(hasDefault);
    }

    const std::string latin = project + "default.ttf";
    const tjs_wchar kHan = u'漢';    // WenQuanYi and IPA, not Liberation
    const tjs_wchar kMissing = 0x0F40; // Tibetan KA: in none of them

    // 2. Fallback chain: one entry per file face, default first, then
    // registration order; repeat lookups are served from the cache.
    {
        FreeTypeFontRasterizer* r = new FreeTypeFontRasterizer();
        r->ApplyFont(MakeFont("Liberation Sans", 0));
        int before = TotalOpens();
        tjs_int w = 0, h = 0;
        r->GetTextExtent(kMissing, w, h);
        auto& fallbacks = Fallbacks(*r);
        CHECK(fallbacks.size() == 5);
        if (fallbacks.size() == 5)
        {
            CHECK(fallbacks[0].Path == latin);
            CHECK(fallbacks[1].Path == kZenHei && fallbacks[1].Index == 0);
            CHECK(fallbacks[2].Path == kZenHei && fallbacks[2].Index == 1);
            CHECK(fallbacks[3].Path == kZenHei && fallbacks[3].Index == 2);
            CHECK(fallbacks[4].Path == kIPA);
            // The primary face is not opened again as its own fallback.
            CHECK(!fallbacks[0].Face);
        }
        // Liberation is the primary: only the four other faces get opened.
        CHECK(TotalOpens() - before == 4);
        CHECK(CharCache(*r).count(kMissing) && CharCache(*r)[kMissing] == -1);

        before = TotalOpens();
        r->GetTextExtent(kMissing, w, h);
        tTVPRect rt;
        r->GetGlyphDrawRect({kMissing, kHan, kMissing}, rt);
        CHECK(TotalOpens() == before);
        CHECK(CharCache(*r).count(kHan) && CharCache(*r)[kHan] == 1);
        r->Release();
    }
    {
        // The primary face resolved through another name is skipped too.
        FreeTypeFontRasterizer* r = new FreeTypeFontRasterizer();
        r->ApplyFont(MakeFont("文泉驿正黑", 0));
        int before = StreamOpens[kZenHei];
        tjs_int w = 0, h = 0;
        r->GetTextExtent(kMissing, w, h);
        auto& fallbacks = Fallbacks(*r);
        CHECK(fallbacks.size() == 5);
        if (fallbacks.size() == 5)
            CHECK(!fallbacks[1].Face && fallbacks[0].Face);
        CHECK(StreamOpens[kZenHei] - before == 2); // Mono and Sharp only
        r->Release();
    }

    // 3. Fallback glyphs carry the primary face's underline / strikeout.
    for (tjs_uint32 flag : {(tjs_uint32)TVP_TF_UNDERLINE, (tjs_uint32)TVP_TF_STRIKEOUT})
    {
        FreeTypeFontRasterizer* r = new FreeTypeFontRasterizer();
        tTVPFont font = MakeFont("Liberation Sans", flag);
        r->ApplyFont(font);
        tTVPCharacterData* primary = r->GetBitmap(Char(font, u'A'), 0, 0);
        tTVPCharacterData* fallback = r->GetBitmap(Char(font, kHan), 0, 0);
        std::set<tjs_int> primaryRows = LineRows(primary);
        std::set<tjs_int> fallbackRows = LineRows(fallback);
        CHECK(!primaryRows.empty());
        // The strikeout crosses the ideograph's own strokes, so its full rows
        // may include stroke rows; the primary line rows must all be there,
        // and nothing of a second line at the fallback's own position.
        for (tjs_int row : primaryRows)
            CHECK(fallbackRows.count(row));
        if (flag == TVP_TF_UNDERLINE)
            CHECK(fallbackRows == primaryRows);
        primary->Release();
        fallback->Release();

        tTVPRect a, b;
        r->GetGlyphDrawRect({u'A'}, a);
        r->GetGlyphDrawRect({kHan}, b);
        if (flag == TVP_TF_UNDERLINE)
            CHECK(b.bottom >= a.bottom);
        r->Release();
    }

    unlink((project + "default.ttf").c_str());
    rmdir(dir);
    if (Failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", Failures);
        return 1;
    }
    std::printf("font fallback tests passed\n");
    return 0;
}
