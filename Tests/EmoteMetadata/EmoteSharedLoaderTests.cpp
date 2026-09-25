#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "psbfile/PSBData.h"
#include "emoteplayer/emoteresourcecache.h"
#include <zlib.h>
#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <utility>

using namespace TJS;
void TVPConsoleLog(const tjs_char*, ...) {}
tjs_uint64 TVPGetRoughTickCount()
{
    return static_cast<tjs_uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
#include "LoaderTestSupport.hpp"
#include "ProductionDecode.inc"

namespace emoteplayer {
struct EmoteDecodedResource;
class emoteobject;
class emotesource;
class emoteicon;
class emotemotion;
struct emotemetadata {
    int variable = 0;
    emotemetadata() { ++loaderTreesCreated; ++loaderLiveTrees; }
    ~emotemetadata() { --loaderLiveTrees; }
};
// The production declaration is kept exact; exposing private fields here lets
// the test assert cursor/table independence without a test API in the engine.
#define private public
#include "ProductionFileClass.inc"
#undef private
#include "ProductionSharedResource.inc"
bool loaderResetOnTree = false;
bool emotefile::GenerateAniTree()
{
    if (loaderResetOnTree) { loaderResetOnTree = false; ClearSharedEmoteResourceCache(); }
    if (loaderFailTree) return false;
    _metadata = new emotemetadata();
    return true;
}
bool emotefile::ClearAniTree()
{
    delete _metadata;
    _metadata = nullptr;
    return true;
}
#include "ProductionLoad.inc"
#include "ProductionReader.inc"
}

namespace {
#include "PSBFixture.hpp"
Node sampleRoot()
{
    return Node::object({{"metadata", Node::object({{"variableList", Node::list({
        Node::object({{"label", Node::text(u8"身体😀")}, {"frameList", Node::list({Node::text("mark-A"), Node::number(7)})}}),
    })}})}, {"source", Node::object({})}});
}
Bytes lz4Container(const Bytes& bytes)
{
    // Valid independent LZ4 frame containing an uncompressed block. The
    // production LZ4 container reader runs; checksum validation is not enabled.
    Bytes wrapped{0x04, 0x22, 0x4d, 0x18, 0x60, 0x40, 0};
    integer(wrapped, 0x80000000u | static_cast<uint32_t>(bytes.size()), 4);
    append(wrapped, bytes); integer(wrapped, 0, 4);
    return wrapped;
}
Bytes mdfContainer(const Bytes& bytes)
{
    uLongf compressedSize = compressBound(static_cast<uLong>(bytes.size()));
    Bytes compressed(compressedSize);
    require(compress2(compressed.data(), &compressedSize, bytes.data(),
                      static_cast<uLong>(bytes.size()), Z_BEST_SPEED) == Z_OK, "fixture MDF compression failed");
    compressed.resize(compressedSize);
    Bytes wrapped{'m', 'd', 'f', 0};
    integer(wrapped, bytes.size(), 4); append(wrapped, compressed);
    return wrapped;
}
std::string marker(emoteplayer::emotefile& file)
{
    auto frames = file.readVariableFrameList(ttstr(u8"身体😀"));
    require(frames.Type() == tvtObject, "loaded metadata is not an array");
    auto* array = frames.AsObjectNoAddRef();
    tTJSVariant first;
    require(TJS_SUCCEEDED(array->PropGetByNum(TJS_MEMBERMUSTEXIST, 0, &first, array)), "frame marker missing");
    return ttstr(first).AsStdString();
}
void resetFixture()
{
    emoteplayer::ClearSharedEmoteResourceCache();
    loaderArchiveFilters = false;
    loaderFiles.clear(); loaderAliases.clear(); loaderOpens.clear();
    require(loaderLiveTrees == 0 && loaderLiveStreams == 0, "previous loader test leaked state");
}
void coldWarmAndLifetime(unsigned container, unsigned version = 3)
{
    resetFixture();
    Fixture fixture;
    const auto raw = fixture.buildFullFile(sampleRoot(), version);
    loaderFiles["canonical/game.xp3>model.psb"] = container == 1 ? lz4Container(raw) : container == 2 ? mdfContainer(raw) : raw;
    loaderAliases["alias/model.psb"] = "canonical/game.xp3>model.psb";
    std::weak_ptr<const emoteplayer::EmoteDecodedResource> weak;
    {
        emoteplayer::emotefile first;
        require(first.load(TJS_N("canonical/game.xp3>model.psb")), "cold resource load failed");
        require(!first.WasSharedCacheHit(), "first load was marked a shared hit");
        require(marker(first) == "mark-A", "production load decoded wrong metadata");
        first._metadata->variable = 19;
        const auto treesBefore = loaderTreesCreated;
        emoteplayer::emotefile second;
        require(second.load(TJS_N("alias/model.psb")), "warm resource load failed");
        require(second.WasSharedCacheHit(), "alias/canonical shared lookup missed");
        require(loaderOpens["canonical/game.xp3>model.psb"] == 1, "warm load reopened the resource");
        require(loaderTreesCreated == treesBefore + 1, "warm load reused a runtime animation tree");
        require(first._metadata != second._metadata && second._metadata->variable == 0,
                "independent players shared mutable runtime variables");
        second._metadata->variable = 31;
        require(first._metadata->variable == 19, "second player mutation reached first player");
        require(first.namesCache == second.namesCache && first.stringsOffset == second.stringsOffset,
                "cache failed to preserve decoded tables");
        first.filePtr->SetPosition(11); second.filePtr->SetPosition(17);
        require(first.filePtr->GetPosition() == 11 && second.filePtr->GetPosition() == 17,
                "cache shared a mutable stream cursor");
        bool writeRejected = false;
        const uint8_t value = 0;
        try { second.filePtr->Write(&value, 1); } catch (...) { writeRejected = true; }
        require(writeRejected, "shared decoded stream accepted a write");
        bool resizeRejected = false;
        try { second.filePtr->SetEndOfStorage(); } catch (...) { resizeRejected = true; }
        require(resizeRejected, "shared decoded stream accepted truncation");
        require(second.filePtr->Seek(-1, TJS_BS_SEEK_SET) == 17 &&
                second.filePtr->Seek(std::numeric_limits<tjs_int64>::max(), TJS_BS_SEEK_CUR) == 17,
                "invalid shared stream seek changed cursor");
        second.filePtr->Seek(-1, TJS_BS_SEEK_END);
        uint8_t endBytes[8];
        require(second.filePtr->Read(endBytes, sizeof endBytes) == 1 && second.filePtr->Read(endBytes, 1) == 0,
                "shared stream EOF/partial-read behavior changed");
        auto handle = emoteplayer::SharedEmoteCache().Find({"canonical/game.xp3>model.psb", 0});
        require(bool(handle), "decoded resource not retained");
        weak = handle;
        handle.reset();
        emoteplayer::ClearSharedEmoteResourceCache();
        require(!weak.expired(), "clearing cache invalidated a live player resource");
        require(marker(first) == "mark-A" && marker(second) == "mark-A", "live player invalid after cache clear");
    }
    require(weak.expired(), "decoded resource outlived cache and final player");
    require(loaderLiveTrees == 0 && loaderLiveStreams == 0, "resource load leaked streams or runtime trees");
    emoteplayer::emotefile afterReset;
    require(afterReset.load(TJS_N("canonical/game.xp3>model.psb")) && !afterReset.WasSharedCacheHit(),
            "session reset left a stale shared cache entry");
    require(loaderOpens["canonical/game.xp3>model.psb"] == 2, "reset did not force storage reload");
}
class DecryptProbe : public tTJSDispatch {
public:
    unsigned calls = 0;
    tjs_error FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant*,
                      tjs_int count, tTJSVariant** args, iTJSDispatch2*) override {
        require(count == 2, "custom decrypt callback arguments changed");
        auto* bytes = dynamic_cast<CBinaryAccessor*>(args[0]->AsObjectNoAddRef());
        require(bytes && bytes->length == args[1]->AsInteger(), "custom decrypt byte accessor missing");
        for (unsigned i = 0; i + 6 <= bytes->length; ++i)
            if (!std::memcmp(bytes->data + i, "mark-A", 6)) bytes->data[i + 5] = 'B';
        ++calls;
        return TJS_S_OK;
    }
};
void seedAndCustomDecryptIsolation()
{
    resetFixture();
    Fixture fixture;
    loaderFiles["game.xp3>model.psb"] = fixture.buildFullFile(sampleRoot());
    {
        emoteplayer::emotefile seedA, seedB, seedAagain;
        seedA.setSeed(1); seedB.setSeed(2); seedAagain.setSeed(1);
        require(seedA.load(TJS_N("game.xp3>model.psb")) && seedB.load(TJS_N("game.xp3>model.psb")) && seedAagain.load(TJS_N("game.xp3>model.psb")),
                "seed-separated load failed");
        require(!seedA.WasSharedCacheHit() && !seedB.WasSharedCacheHit() && seedAagain.WasSharedCacheHit(),
                "shared cache key omitted decrypt seed");
        require(loaderOpens["game.xp3>model.psb"] == 2, "seed cache opened wrong number of resources");
        DecryptProbe probe;
        for (unsigned i = 0; i < 2; ++i) {
            emoteplayer::emotefile custom;
            custom.setSeed(1); custom.setFun(tTJSVariantClosure(&probe, nullptr));
            require(custom.load(TJS_N("game.xp3>model.psb")), "custom decrypt load failed");
            require(!custom.WasSharedCacheHit() && marker(custom) == "mark-B", "custom decrypt cache bypass failed");
        }
        require(probe.calls == 2 && loaderOpens["game.xp3>model.psb"] == 4, "custom callback was skipped by cache");
        emoteplayer::emotefile original;
        original.setSeed(1);
        require(original.load(TJS_N("game.xp3>model.psb")) && original.WasSharedCacheHit() && marker(original) == "mark-A",
                "custom decrypt polluted shared decoded bytes");
        emoteplayer::InvalidateSharedEmoteResource("game.xp3>model.psb");
        emoteplayer::emotefile invalidated;
        invalidated.setSeed(2);
        require(invalidated.load(TJS_N("game.xp3>model.psb")) && !invalidated.WasSharedCacheHit(),
                "explicit path invalidation did not invalidate all seed variants");
    }
}
void failedDecodeAndGenerationReset()
{
    resetFixture();
    Fixture fixture;
    loaderFiles["game.xp3>model.psb"] = fixture.buildFullFile(sampleRoot());
    loaderFiles["game.xp3>broken.mdf"] = Bytes{'m', 'd', 'f', 0, 32, 0, 0, 0, 0xff, 0xff};
    {
        emoteplayer::emotefile corrupt;
        require(!corrupt.load(TJS_N("game.xp3>broken.mdf")), "invalid compressed data was accepted");
    }
    require(emoteplayer::GetSharedEmoteResourceCacheStats().entries == 0, "failed decompression published a resource");
    {
        emoteplayer::emotefile failed;
        loaderFailTree = true;
        require(!failed.load(TJS_N("game.xp3>model.psb")), "tree failure was not propagated");
        loaderFailTree = false;
    }
    require(emoteplayer::GetSharedEmoteResourceCacheStats().entries == 0, "failed load published a resource");
    {
        emoteplayer::loaderResetOnTree = true;
        emoteplayer::emotefile resetDuringLoad;
        require(resetDuringLoad.load(TJS_N("game.xp3>model.psb")), "load spanning reset failed");
        require(emoteplayer::GetSharedEmoteResourceCacheStats().entries == 0, "in-flight load repopulated cache after reset");
    }
    {
        emoteplayer::emotefile next;
        require(next.load(TJS_N("game.xp3>model.psb")) && !next.WasSharedCacheHit(), "cold retry hit failed/stale entry");
        require(marker(next) == "mark-A", "cold retry metadata corrupt");
    }
    require(loaderOpens["game.xp3>model.psb"] == 3, "failed/reset loads were unexpectedly reused");
}
void encryptedSeedAndReload()
{
    resetFixture();
    Fixture fixture;
    auto bytes = fixture.buildFullFile(sampleRoot(), 2);
    PSB::PSBHeader header{};
    std::memcpy(&header, bytes.data(), sizeof header);
    tjs_uint32 key[4]{0x075BCD15, 0x159A55E5, 0x1F123BB5, 12345};
    EMoteCTX context{};
    init_emote_ctx(&context, key);
    emote_decrypt(&context, bytes.data() + header.offsetEncrypt,
                  header.offsetChunkOffsets - header.offsetEncrypt);
    loaderFiles["game.xp3>encrypted.psb"] = std::move(bytes);
    emoteplayer::emotefile first, second;
    first.setSeed(12345); second.setSeed(12345);
    require(first.load(TJS_N("game.xp3>encrypted.psb")) && second.load(TJS_N("game.xp3>encrypted.psb")), "seed-encrypted PSB load failed");
    require(!first.WasSharedCacheHit() && second.WasSharedCacheHit() && loaderOpens["game.xp3>encrypted.psb"] == 1,
            "decrypted PSB was not shared");
    require(marker(first) == "mark-A" && marker(second) == "mark-A", "seed decoder/table snapshot corrupted metadata");
    const auto treesBefore = loaderTreesCreated;
    first._metadata->variable = 88;
    first.isMotion = true; first.isMirror = true; first._screenSize.width = 321;
    require(first.load(TJS_N("game.xp3>encrypted.psb")) && first.WasSharedCacheHit(), "same-file reload missed cache");
    require(loaderTreesCreated == treesBefore + 1 && first._metadata->variable == 0 &&
            !first.isMotion && !first.isMirror && first._screenSize.width == 0,
            "reload retained prior mutable state");
    require(marker(first) == "mark-A" && first.namesCache == second.namesCache,
            "reload appended/corrupted metadata tables");
}
void looseFileChangesAreVisible()
{
    resetFixture();
    Fixture fixture;
    loaderFiles["loose.psb"] = fixture.buildFullFile(sampleRoot());
    emoteplayer::emotefile first, second;
    require(first.load(TJS_N("loose.psb")) && !first.WasSharedCacheHit(), "loose initial load failed");
    auto& bytes = loaderFiles["loose.psb"];
    for (size_t i = 0; i + 6 <= bytes.size(); ++i)
        if (!std::memcmp(bytes.data() + i, "mark-A", 6)) bytes[i + 5] = 'B';
    require(second.load(TJS_N("loose.psb")) && !second.WasSharedCacheHit() && marker(second) == "mark-B",
            "loose file read reused stale cached contents");
    require(marker(first) == "mark-A" && loaderOpens["loose.psb"] == 2,
            "loose file reload mutated an active player or skipped storage");
    require(emoteplayer::GetSharedEmoteResourceCacheStats().entries == 0, "loose file entered shared cache");
}
void archiveInvalidation()
{
    resetFixture();
    Fixture fixture;
    const auto bytes = fixture.buildFullFile(sampleRoot());
    for (const auto* name : {"game.xp3>first.psb", "game.xp3>second.psb", "other.xp3>first.psb"})
        loaderFiles[name] = bytes;
    emoteplayer::emotefile first, second, other;
    require(first.load(TJS_N("game.xp3>first.psb")) && second.load(TJS_N("game.xp3>second.psb")) &&
            other.load(TJS_N("other.xp3>first.psb")), "archive test setup failed");
    emoteplayer::InvalidateSharedEmoteResource("game.xp3");
    require(emoteplayer::GetSharedEmoteResourceCacheStats().entries == 1,
            "archive invalidation did not preserve unrelated container");
    require(marker(first) == "mark-A" && marker(second) == "mark-A", "archive invalidation broke active files");
    emoteplayer::emotefile fresh, otherAgain;
    require(fresh.load(TJS_N("game.xp3>first.psb")) && !fresh.WasSharedCacheHit(), "archive member remained cached");
    require(otherAgain.load(TJS_N("other.xp3>first.psb")) && otherAgain.WasSharedCacheHit(),
            "unrelated archive was unnecessarily reloaded");
}
void dynamicArchiveFilterBypass()
{
    resetFixture();
    Fixture fixture;
    loaderFiles["filtered.xp3>model.psb"] = fixture.buildFullFile(sampleRoot());
    emoteplayer::emotefile cached;
    require(cached.load(TJS_N("filtered.xp3>model.psb")), "filter setup load failed");
    loaderArchiveFilters = true;
    auto& bytes = loaderFiles["filtered.xp3>model.psb"];
    for (size_t i = 0; i + 6 <= bytes.size(); ++i)
        if (!std::memcmp(bytes.data() + i, "mark-A", 6)) bytes[i + 5] = 'B';
    for (int i = 0; i < 2; ++i) {
        emoteplayer::emotefile filtered;
        require(filtered.load(TJS_N("filtered.xp3>model.psb")) &&
                !filtered.WasSharedCacheHit() && filtered.BypassedSharedCacheForArchiveFilter(),
                "archive callbacks were bypassed by a shared cache hit");
        require(marker(filtered) == "mark-B", "archive filter output was stale");
    }
    require(loaderOpens["filtered.xp3>model.psb"] == 3,
            "archive filters require storage to reopen on every file load");
    require(marker(cached) == "mark-A", "filtered file modified an active snapshot");
}
}

int main()
{
    try {
        tTJS vm;
        for (unsigned version : {2u, 3u, 4u}) coldWarmAndLifetime(0, version);
        coldWarmAndLifetime(1);
        coldWarmAndLifetime(2);
        seedAndCustomDecryptIsolation();
        failedDecodeAndGenerationReset();
        encryptedSeedAndReload();
        looseFileChangesAreVisible();
        archiveInvalidation();
        dynamicArchiveFilterBypass();
        resetFixture();
        std::cout << "PASS: production Emote load/cache integration, full PSB tables, raw/LZ4/MDF cold-warm loads, "
                     "independent trees/cursors, read-only data, seed/custom-decrypt isolation, reset and lifetime\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    } catch (...) {
        std::cerr << "FAIL: unexpected loader/TJS exception\n"; return 1;
    }
}
