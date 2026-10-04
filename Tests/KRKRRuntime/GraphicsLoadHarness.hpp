#include <algorithm>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "LayerWorkDiagnostics.h"
using tjs_uint64=uint64_t; using tjs_uint=unsigned; using tjs_uint32=uint32_t;
using tjs_int32=int32_t; using tjs_int=int; using ttstr=std::string;
enum tTVPGraphicLoadMode { glmNormal,glmGray };
constexpr int TVP_clNone=-1,TVP_COMPACT_LEVEL_MINIMIZE=15,TVP_COMPACT_LEVEL_MAX=100;
struct tTVPCompactEventCallbackIntf { virtual ~tTVPCompactEventCallbackIntf()=default; virtual void OnCompact(tjs_int)=0; };
struct tTVPGraphicMetaInfoPair { int value=0; };
struct iTJSDispatch2 { int metadata=0; };
struct iTVPBaseBitmap { unsigned marker=0; };
unsigned opens=0,liveBitmaps=0,nextMarker=0; bool failAssignment=false;
struct tTVPBitmap {
    unsigned marker=++nextMarker;
    tTVPBitmap() { ++liveBitmaps; }
    void Release() { --liveBitmaps; delete this; }
    unsigned GetWidth() const { return 1024; } unsigned GetHeight() const { return 1024; }
    unsigned GetBPP() const { return 32; }
};
ttstr TVPNormalizeStorageName(const ttstr& name) { return name; }
iTJSDispatch2* TVPMetaInfoPairsToDictionary(std::vector<tTVPGraphicMetaInfoPair>* mi) {
    return new iTJSDispatch2{mi->front().value};
}
// The decoded-image boundary records each opening and supplies deterministic
// bytes/metadata. The removed stub would have opened the same storage first.
tTVPBitmap* TVPInternalLoadBitmap(const ttstr& name,int,unsigned,unsigned,
    std::vector<tTVPGraphicMetaInfoPair>** mi,tTVPGraphicLoadMode,ttstr* pn) {
    ++opens; *mi=new std::vector<tTVPGraphicMetaInfoPair>{{int(opens)}}; *pn=name+".province";
    return new tTVPBitmap;
}
struct tTVPGraphicImageData {
    unsigned refs=1,marker=0; ttstr ProvinceName; std::vector<tTVPGraphicMetaInfoPair>* MetaInfo=nullptr;
    ~tTVPGraphicImageData() { delete MetaInfo; }
    void AddRef() { ++refs; } void Release() { if(--refs==0) delete this; }
    void AssignBitmap(tTVPBitmap* bmp) { marker=bmp->marker; }
    void AssignToTexture(iTVPBaseBitmap* dest) {
        if(failAssignment) throw std::runtime_error("assignment failed"); dest->marker=marker;
    }
    unsigned GetSize() const { return 4u*1024u*1024u; }
};
struct tTVPGraphicImageHolder {
    std::shared_ptr<tTVPGraphicImageData> value;
    explicit tTVPGraphicImageHolder(tTVPGraphicImageData* p):value(p,[](auto* p){p->Release();}) { p->AddRef(); }
    tTVPGraphicImageData* GetObjectNoAddRef() const { return value.get(); }
};
struct tTVPGraphicsSearchData {
    ttstr Name; int KeyIdx=0; tTVPGraphicLoadMode Mode=glmNormal; unsigned DesW=0,DesH=0;
    bool operator==(const tTVPGraphicsSearchData& b) const {
        return Name==b.Name && KeyIdx==b.KeyIdx && Mode==b.Mode && DesW==b.DesW && DesH==b.DesH;
    }
};
struct tTVPGraphicCache {
    struct Entry { tTVPGraphicsSearchData key; tTVPGraphicImageHolder holder; };
    std::list<Entry> entries;
    struct tIterator {
        Entry* entry=nullptr;
        bool IsNull() const { return entry==nullptr; }
        bool operator!=(const tIterator& b) const { return entry!=b.entry; }
        tTVPGraphicImageHolder& GetValue() { return entry->holder; }
    };
    static unsigned MakeHash(const tTVPGraphicsSearchData&) { return 0; }
    tTVPGraphicImageHolder* FindAndTouchWithHash(const tTVPGraphicsSearchData& key,unsigned) {
        for(auto i=entries.begin();i!=entries.end();++i) if(i->key==key) {
            entries.splice(entries.begin(),entries,i); return &entries.front().holder;
        }
        return nullptr;
    }
    tIterator GetLast() { return {entries.empty() ? nullptr : &entries.back()}; }
    tIterator GetEnd() { return {}; }
    void ChopLast(unsigned) { entries.pop_back(); }
    void Clear() { entries.clear(); }
    void AddWithHash(const tTVPGraphicsSearchData& key,unsigned,const tTVPGraphicImageHolder& holder) {
        entries.push_front({key,holder});
    }
} TVPGraphicCache;
bool TVPGraphicCacheEnabled=true;
tjs_uint64 TVPGraphicCacheLimit=64u*1024u*1024u,TVPGraphicCacheTotalBytes=0;
std::recursive_mutex TVPGraphicCacheMutex;
