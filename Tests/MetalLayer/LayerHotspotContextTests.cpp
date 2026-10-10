#include "../../Engine/KRKRRuntime/Source/cpp/core/render/LayerHotspotContext.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace krkrsdl3::layer_hotspot;
int main() {
    static_assert(std::is_trivially_copyable<Identity>::value,"backend metadata must be copied by value");
    assert(Current().currentLayer==0);
    const auto first=NextLayerID(),second=NextLayerID();
    assert(first && second>first);
    Identity source;
    {
        Scope outer(first,"image");
        source=CreatedIdentity(7,42);
        assert(source.session==7 && source.texture==42 && source.version==0);
        assert(source.creatorLayer==first && std::string(source.role)=="image");
        try {
            Scope inner(second,"transition.output");
            assert(Current().currentLayer==second);
            throw std::runtime_error("unwind");
        } catch(const std::runtime_error&) {}
        assert(Current().currentLayer==first && std::string(Current().role)=="image");
    }
    assert(Current().currentLayer==0 && !Current().role[0]);
    SetAsset(source,"archive\\bg\\street.png");
    Identity normalized;
    SetAsset(normalized,"archive/bg/street.png");
    assert(std::string(source.asset)=="archive/bg/street.png");
    assert(source.assetHash==normalized.assetHash && !source.assetTruncated);
    const auto originalHash=source.assetHash;
    const std::string longPath(260,'a');
    SetAsset(normalized,longPath.c_str());
    assert(normalized.assetTruncated && std::strlen(normalized.asset)==191);
    const auto longHash=normalized.assetHash;
    SetAsset(normalized,(longPath+"b").c_str());
    assert(normalized.assetHash!=longHash); // Full-path hash includes omitted suffix.
    Identity utf8;
    SetAsset(utf8,(std::string(190,'a')+"\xe8\xa1\x97").c_str());
    assert(utf8.assetTruncated && std::strlen(utf8.asset)==190);
    SetAsset(utf8,(std::string(260,'a')+"/archive.xp3>bg/street.png").c_str());
    assert(std::string(utf8.asset)=="bg/street.png" && utf8.displayShortened && !utf8.assetTruncated);
    {
        Scope bitmap(0,"bitmap",second,ReceiverKind::Bitmap);
        assert(Current().currentReceiver==second && Current().receiverKind==ReceiverKind::Bitmap);
        Scope layer(first,"draw",first,ReceiverKind::Layer);
        assert(Current().currentLayer==first && Current().currentReceiver==first);
    }
    assert(Current().currentReceiver==0);
    Identity copy=CreatedIdentity(8,43);
    copy.version=12;
    SetParent(copy,source,"copy",true);
    assert(copy.session==8 && copy.texture==43 && copy.version==12);
    assert(copy.parent==42 && copy.parentSession==7 && copy.assetHash==originalHash);
    assert(std::string(copy.role)=="copy");
    Identity discarded=CreatedIdentity(8,44);
    SetParent(discarded,copy,"discarded",false);
    assert(discarded.parent==43 && discarded.parentSession==8);
    assert(!discarded.assetHash && !discarded.asset[0] && !discarded.assetTruncated);
    assert(std::string(discarded.role)=="discarded");
    assert(source.session==7 && source.texture==42 && source.assetHash==originalHash);
    std::cout << "Layer hotspot context tests passed\n";
}
