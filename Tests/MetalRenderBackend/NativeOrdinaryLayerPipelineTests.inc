// Runs the production backend with specialization/coalescing independently
// disabled/enabled. This body also gets a compile-only portable check.
void NativeOrdinaryLayerPipelineTests(SDL_Window* window) {
    using krkrsdl3::ordinary_layer::Pipeline;
    const bool oldSpecialize=SDL_GetHintBoolean("MIKAGE_METAL_LAYER_SPECIALIZED_PIPELINES",true);
    const bool oldCoalesce=SDL_GetHintBoolean("MIKAGE_METAL_LAYER_COALESCE_FILL",true);
    const TVPLayerRect full{0,0,8,6},part{1,1,6,5};
    std::vector<uint8_t> seed(8*6*4),source(seed.size());
    const uint8_t alphas[]={0,1,127,128,254,255};
    for(int y=0;y<6;++y)for(int x=0;x<8;++x) {
        const int i=(y*8+x)*4;
        seed[i]=uint8_t(x*31);seed[i+1]=uint8_t(200-y*13);seed[i+2]=uint8_t(255-x*19);
        seed[i+3]=uint8_t(17+x*19+y*7);
        source[i]=255;source[i+1]=0;source[i+2]=uint8_t(y*43+x*11);source[i+3]=alphas[(x+y)%6];
    }
    std::vector<std::vector<uint8_t>> baseline;
    for(bool coalesce:{false,true})for(bool specialize:{false,true}) {
        SDL_SetHint("MIKAGE_METAL_LAYER_SPECIALIZED_PIPELINES",specialize?"1":"0");
        SDL_SetHint("MIKAGE_METAL_LAYER_COALESCE_FILL",coalesce?"1":"0");
        std::unique_ptr<iTVPRenderBackend> owner(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(bool(owner),"specialized Layer backend");auto& gpu=*owner;
        auto* metal=dynamic_cast<krkrsdl3::MetalRenderBackend*>(&gpu);
        const bool tile=metal->IsLayerTileRenderingActive();
        if(tile)Require(metal->GetLayerPipelineStats().availableMask==(specialize?31u:1u),
                        "specialized pipeline failed to compile (generic fallback is not native validation)");
        void* target=gpu.CreateLayerTexture(8,6,TVPLayerTextureFormat::RGBA8);
        void* input=gpu.CreateLayerTexture(8,6,TVPLayerTextureFormat::RGBA8);
        Require(target && input,"specialized allocations");
        Require(gpu.UpdateLayerTexture(target,seed.data(),32,full) &&
                gpu.UpdateLayerTexture(input,source.data(),32,full),"specialized seed uploads");
        std::vector<uint8_t> expected=seed,actual;int pitch=0;
        TVPLayerOperation copy;copy.kind=TVPLayerOperationKind::Copy;
        TVPLayerOperation copyColor;copyColor.kind=TVPLayerOperationKind::CopyColor;
        TVPLayerOperation fill;fill.kind=TVPLayerOperationKind::Fill;fill.color=0x6a90b3d7;
        TVPLayerOperation alpha;alpha.kind=TVPLayerOperationKind::Alpha;
        alpha.flags=TVP_LAYER_HOLD_ALPHA|TVP_LAYER_FULL_OPACITY_BRANCH;
        TVPLayerOperation mask;mask.kind=TVPLayerOperationKind::FillMask;mask.opacity=73;
        const auto apply=[&](const TVPLayerOperation& op,TVPLayerRect region,TVPLayerRect src) {
            const bool solid=op.kind==TVPLayerOperationKind::Fill || op.kind==TVPLayerOperationKind::FillMask;
            Require(gpu.OperateLayerRect(op,target,region,solid?nullptr:input,src,0),"mixed pipeline draw");
            // Independent byte reference for the equal-size, forward cases.
            for(int y=region.top;y<region.bottom;++y)for(int x=region.left;x<region.right;++x) {
                const int d=(y*8+x)*4,s=((src.top+y-region.top)*8+src.left+x-region.left)*4;
                if(op.kind==TVPLayerOperationKind::Fill) {
                    for(int c=0;c<4;++c)expected[d+c]=uint8_t(op.color>>(c*8));
                } else if(op.kind==TVPLayerOperationKind::FillMask)expected[d+3]=uint8_t(op.opacity);
                else if(op.kind==TVPLayerOperationKind::Alpha) {
                    const int a=op.opacity==255?source[s+3]:(int(source[s+3])*op.opacity)>>8;
                    for(int c=0;c<3;++c) {
                        const int product=(int(source[s+c])-int(expected[d+c]))*a;
                        // Floor division expressed independently of signed >>.
                        expected[d+c]=uint8_t(int(expected[d+c])+(product>=0?product/256:-((-product+255)/256)));
                    }
                } else for(int c=0;c<(op.kind==TVPLayerOperationKind::Copy?4:3);++c)expected[d+c]=source[s+c];
            }
        };
        const auto read=[&](){Require(gpu.ReadLayerTexture(target,actual,pitch) && pitch==32,"mixed pipeline read");
            Require(actual==expected,"mixed pipeline exact bytes/order/write mask");};
        int renders=krkrsdl3::g_renderEncoders,submits=krkrsdl3::g_metalSubmits,waits=krkrsdl3::g_syncWaits;
        apply(copyColor,full,full);apply(fill,part,{});apply(alpha,full,full);
        apply(mask,{0,0,4,4},{});apply(copy,{2,1,7,5},{0,0,5,4});
        alpha.opacity=254;apply(alpha,full,full);alpha.opacity=255;
        if(tile)Require(krkrsdl3::g_renderEncoders==renders+1,"PSO switches split render pass");
        Require(krkrsdl3::g_metalSubmits==submits && krkrsdl3::g_syncWaits==waits,
                "PSO switches added submit/wait");read();
        const auto fillBefore=metal->GetLayerFillCoalescingStats();
        apply(copyColor,full,full);apply(fill,part,{});apply(copy,full,full);
        apply(copyColor,full,full);apply(fill,part,{});apply(alpha,full,full);
        // Submit materializes pending Fill while the previous PSO is RGB-only.
        apply(fill,{0,0,2,2},{});gpu.EndFrame();read();
        const auto fillAfter=metal->GetLayerFillCoalescingStats();
        Require(fillAfter.omitted-fillBefore.omitted==(coalesce && tile?1u:0u),"Fill omission changed across PSOs");
        const auto stats=metal->GetLayerPipelineStats();
        if(tile && specialize) {
            const std::array<uint64_t,5> draws={2,2,coalesce?3u:4u,3,2};
            const std::array<uint64_t,5> pixels={64,68,coalesce?44u:64u,144,96};
            Require(stats.draws==draws && stats.pixels==pixels,
                    "pipeline counters include omitted Fill or miss materialized draws");
        } else for(unsigned i=1;i<krkrsdl3::ordinary_layer::PipelineCount;++i)
            Require(stats.draws[i]==0,"disabled/compute path used specialized shader");
        if(tile && !specialize)Require(stats.draws[0]==(coalesce?12u:13u),"generic actual draw counter");
        // A full Fill opening a new pass is a load clear, not a shader hit.
        apply(fill,full,{});read();
        Require(metal->GetLayerPipelineStats().draws==stats.draws,"load clear counted as fragment draw");
        // Scaled, clipped and reversed source rectangles use the unchanged
        // software sampler. Compare specialized output to generic byte-for-byte.
        unsigned caseIndex=0;
        for(int sampling:{0,1})for(bool reverse:{false,true})for(bool rgbOnly:{false,true}) {
            Require(gpu.UpdateLayerTexture(target,seed.data(),32,full),"sampling seed");
            const TVPLayerRect from=reverse?TVPLayerRect{7,5,1,1}:TVPLayerRect{1,1,7,5};
            Require(gpu.OperateLayerRect(rgbOnly?copyColor:copy,target,{-1,-1,9,7},input,from,sampling),"specialized sampling");
            Require(gpu.ReadLayerTexture(target,actual,pitch),"sampling result");
            if(!specialize && !coalesce)baseline.push_back(actual);
            else Require(actual==baseline[caseIndex],"specialization changed scaled/clipped/mirrored sampling");
            ++caseIndex;
        }
        // Explicit byte boundaries prohibit an opaque-source shortcut or
        // truncating division in the dedicated HDA formula.
        for(int x=0;x<8;++x) {
            seed[x*4]=x==0?0:200;source[x*4]=x==0?255:0;
            source[x*4+3]=x==0?255:1;
        }
        Require(gpu.UpdateLayerTexture(target,seed.data(),32,full) &&
                gpu.UpdateLayerTexture(input,source.data(),32,full),"alpha boundary seeds");
        expected=seed;apply(alpha,full,full);read();
        Require(actual[0]==254 && actual[4]==199 && actual[3]==seed[3] && actual[7]==seed[7],"HDA alpha boundaries");
        gpu.DestroyLayerTexture(target);gpu.DestroyLayerTexture(input);
        // Restore fixtures mutated by the edge case before the next backend.
        for(int x=0;x<8;++x) {seed[x*4]=uint8_t(x*31);source[x*4]=255;source[x*4+3]=alphas[x%6];}
    }
    SDL_SetHint("MIKAGE_METAL_LAYER_SPECIALIZED_PIPELINES",oldSpecialize?"1":"0");
    SDL_SetHint("MIKAGE_METAL_LAYER_COALESCE_FILL",oldCoalesce?"1":"0");
}
