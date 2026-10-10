// Production backend ordering and bytes; portable policy checks cannot sign
// off Metal encoding. This body is also compiled (not run) on Windows.
void NativePendingFillTests(SDL_Window* window) {
    const bool previous=SDL_GetHintBoolean("MIKAGE_METAL_LAYER_COALESCE_FILL",true);
    for(bool enabled:{false,true}) {
        SDL_SetHint("MIKAGE_METAL_LAYER_COALESCE_FILL",enabled?"1":"0");
        std::unique_ptr<iTVPRenderBackend> owner(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(bool(owner),"pending Fill backend");auto& gpu=*owner;
        auto* metal=dynamic_cast<krkrsdl3::MetalRenderBackend*>(&gpu);
        const bool tile=metal->IsLayerTileRenderingActive();
        const TVPLayerRect full{0,0,4,4},part{1,1,3,3};
        void* target=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
        void* source=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
        Require(target && source,"pending Fill allocations");
        std::vector<uint8_t> seed(64,0x65),actual;int pitch=0;
        Require(gpu.UpdateLayerTexture(source,seed.data(),16,full),"pending Fill source upload");
        TVPLayerOperation fill;fill.kind=TVPLayerOperationKind::Fill;fill.color=0x12345678;
        TVPLayerOperation copy;copy.kind=TVPLayerOperationKind::Copy;
        const auto paint=[&](TVPLayerRect region,uint32_t color){fill.color=color;
            Require(gpu.OperateLayerRect(fill,target,region,nullptr,{},0),"pending Fill accepted");};
        const auto read=[&](){Require(gpu.ReadLayerTexture(target,actual,pitch) && pitch==16,"pending Fill read");};
        paint(full,0x12345678); // first load clear remains
        const auto before=metal->GetLayerFillCoalescingStats();
        paint(full,0x22334455);
        Require(gpu.OperateLayerRect(copy,target,full,source,full,0),"full Copy after Fill");
        read();Require(actual==seed,"Fill -> Fill -> Copy bytes");
        const auto after=metal->GetLayerFillCoalescingStats();
        Require(after.omitted-before.omitted==(enabled && tile?1:0),"full Copy omission mechanism");
        paint(part,0x11223344);paint(full,0x89abcdef);
        Require(gpu.OperateLayerRect(copy,target,part,source,{0,0,2,2},0),"partial Copy after full Fill");
        read();
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)for(int c=0;c<4;++c)
            Require(actual[(y*4+x)*4+c]==(x>=1 && x<3 && y>=1 && y<3?0x65:uint8_t(0x89abcdefu>>(8*c))),"partial Copy lost outside Fill");
        paint(full,0x10203040);paint(part,0x50607080);read();
        Require(actual[0]==0x40 && actual[20]==0x80,"read observes pending Fill");
        paint(part,0x76543210);
        Require(!gpu.OperateLayerRect(copy,target,full,source,{-1,0,3,4},0),"invalid Copy rejected");
        read();Require(actual[20]==0x10,"rejection discarded accepted Fill");
        paint(part,0xfedcba98);
        Require(gpu.OperateLayerRect(copy,target,full,target,full,0),"alias Copy accepted");
        read();Require(actual[20]==0x98,"alias snapshot missed Fill");
        paint(part,0x01020304);gpu.EndFrame();read();Require(actual[20]==4,"submit missed Fill");
        paint(part,0x05060708);gpu.DestroyLayerTexture(target); // no dangling pending target
        target=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);read();
        Require(std::all_of(actual.begin(),actual.end(),[](uint8_t v){return !v;}),"destroy/recreate pending Fill");
        gpu.DestroyLayerTexture(target);gpu.DestroyLayerTexture(source);
    }
    SDL_SetHint("MIKAGE_METAL_LAYER_COALESCE_FILL",previous?"1":"0");
}
