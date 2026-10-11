void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        iTVPBaseBitmap dest; ttstr province; iTJSDispatch2* metadata=nullptr;
        krkrsdl3::layer_work::SetEnabled(true);
        Require(TVPLoadGraphic(&dest,"ui",TVP_clNone,0,0,glmNormal,&province,&metadata)==4*1024*1024,"wrong image byte count");
        Require(opens==1 && liveBitmaps==0 && province=="ui.province" && metadata->metadata==1,"single-load ownership or metadata changed");
        Require(dest.texture.tags==1 && std::string(dest.texture.identity.asset)=="ui","decoded image lost P2D asset tag");
        const auto assetHash=dest.texture.identity.assetHash;
        auto original=dest.marker; delete metadata; metadata=nullptr;
        TVPLoadGraphic(&dest,"ui",TVP_clNone,0,0,glmNormal,&province,&metadata);
        Require(opens==1 && dest.marker==original && metadata->metadata==1,"cache hit re-opened or lost metadata"); delete metadata;
        Require(dest.texture.tags==2 && dest.texture.identity.assetHash==assetHash,"cache hit lost P2D asset tag");
        auto timing=krkrsdl3::layer_work::Take();
        Require(timing.stages.find("imageLoad:2/")!=std::string::npos && timing.stages.find("imageDecode:1/")!=std::string::npos &&
            timing.stages.find("imageCacheHit:1/")!=std::string::npos,"image stage/cache attribution wrong");
        TVPLoadGraphic(&dest,"ui",0,0,0,glmNormal,nullptr,nullptr);
        TVPLoadGraphic(&dest,"ui",TVP_clNone,10,0,glmNormal,nullptr,nullptr);
        TVPLoadGraphic(&dest,"ui",TVP_clNone,0,10,glmNormal,nullptr,nullptr);
        TVPLoadGraphic(&dest,"ui",TVP_clNone,0,0,glmGray,nullptr,nullptr);
        Require(opens==5,"key/color/size/mode cache keys collapsed");
        for(int i=0;i<6;++i) TVPLoadGraphic(&dest,"page"+std::to_string(i),TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        tTVPClearGraphicCacheCallback compact; compact.OnCompact(TVP_COMPACT_LEVEL_MINIMIZE);
        Require(TVPGraphicCacheTotalBytes<=16u*1024u*1024u && TVPGraphicCache.entries.size()==4,"minimize did not trim recent cache");
        auto before=opens; TVPLoadGraphic(&dest,"page5",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        Require(opens==before,"minimize discarded newest image");
        TVPLoadGraphic(&dest,"ui",TVP_clNone,0,0,glmNormal,nullptr,nullptr); Require(opens==before+1,"minimize kept cold oldest image");
        compact.OnCompact(TVP_COMPACT_LEVEL_MAX);
        Require(TVPGraphicCacheTotalBytes==0 && TVPGraphicCache.entries.empty(),"maximum compaction retained images");
        // Actual common commit policy: capacity is true immediately after
        // every insertion, replacement changes only the committed bytes, and
        // failed hash-table insertion does not corrupt totals or old entries.
        TVPGraphicCacheLimit=4u*1024u*1024u;
        TVPLoadGraphic(&dest,"capacity-a",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        TVPLoadGraphic(&dest,"capacity-b",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        Require(TVPGraphicCacheTotalBytes==TVPGraphicCacheLimit && TVPGraphicCache.entries.size()==1,"after-insert capacity exceeded");
        tTVPGraphicsSearchData replacement;replacement.Name="capacity-b";replacement.KeyIdx=TVP_clNone;
        auto* data=new tTVPGraphicImageData();data->bitmapAvailable=false;
        Require(TVPCommitGraphicCache(replacement,0,data),"replacement rejected");data->Release();
        Require(TVPGraphicCacheTotalBytes==4u*1024u*1024u && TVPGraphicCache.entries.size()==1,"replacement double-counted bytes");
        failCacheInsert=true;data=new tTVPGraphicImageData();bool insertFailed=false;
        replacement.Name="failed-insert";
        try {TVPCommitGraphicCache(replacement,0,data);} catch(...) {insertFailed=true;}
        data->Release();failCacheInsert=false;
        Require(insertFailed && TVPGraphicCacheTotalBytes==4u*1024u*1024u && TVPGraphicCache.entries.size()==1,"failed insertion changed committed state");
        tTVPBaseBitmap cpuBitmap;cpuBitmap.marker=777;
        Require(!TVPCheckImageCache("capacity-b",&cpuBitmap,glmNormal,0,0,TVP_clNone,nullptr) && cpuBitmap.marker==777,
            "texture-only holder produced a false Bitmap hit");
        TVPClearGraphicCache();TVPGraphicCacheLimit=2u*1024u*1024u;
        before=opens;TVPLoadGraphic(&dest,"oversize",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        Require(opens==before+1 && dest.marker && TVPGraphicCacheTotalBytes==0 && TVPGraphicCache.entries.empty(),"oversize did not return normally without admission");
        TVPGraphicCacheLimit=0;
        TVPLoadGraphic(&dest,"cache-zero",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        Require(TVPGraphicCacheTotalBytes==0 && TVPGraphicCache.entries.empty(),"enabled zero-capacity cache admitted an image");
        TVPGraphicCacheLimit=0; compact.OnCompact(TVP_COMPACT_LEVEL_MINIMIZE);
        Require(TVPGraphicCache.entries.empty(),"zero cache limit ignored");
        TVPGraphicCacheEnabled=false; before=opens;
        TVPLoadGraphic(&dest,"uncached",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        TVPLoadGraphic(&dest,"uncached",TVP_clNone,0,0,glmNormal,nullptr,nullptr);
        Require(opens==before+2 && liveBitmaps==0,"uncached loads leaked or re-opened");
        failAssignment=true; bool failed=false;
        const auto tagsBeforeFailure=dest.texture.tags;
        try { TVPLoadGraphic(&dest,"failed",TVP_clNone,0,0,glmNormal,nullptr,nullptr); }
        catch(const std::runtime_error&) { failed=true; }
        Require(failed && liveBitmaps==0,"failed image assignment leaked decoded bitmap");
        Require(dest.texture.tags==tagsBeforeFailure,"failed assignment falsely tagged an asset");
        std::cout<<"PASS production graphic single load, cache keys/metadata, bounded minimize, explicit purge and exception cleanup\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
