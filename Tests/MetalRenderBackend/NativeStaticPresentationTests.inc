bool NativeStaticPresentationTests(SDL_Window* window) {
    bool verified=false;
    const bool previous=SDL_GetHintBoolean("MIKAGE_METAL_STATIC_PRESENT",true);
    SDL_SetHint("MIKAGE_METAL_STATIC_PRESENT","1");
    Require(SDL_ShowWindow(window),"show window for real presentation acknowledgement");
    SDL_PumpEvents();SDL_Delay(50);
    {
        std::unique_ptr<iTVPRenderBackend> owner(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(bool(owner),"static present backend");auto& gpu=*owner;
        auto* metal=dynamic_cast<krkrsdl3::MetalRenderBackend*>(&gpu);
        void* texture=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
        std::vector<uint8_t> seed(64,0x65);
        Require(texture && gpu.UpdateLayerTexture(texture,seed.data(),16,{0,0,4,4}),"static present image");
        const auto frame=[&](){gpu.BeginFrame(12,8);gpu.DrawWindowTexture(texture,0,0,12,8);gpu.EndFrame();};
        frame();SDL_Delay(100);
        bool unsupported=false,skipped=false;
        for(int i=0;i<50;++i) {
            SDL_PumpEvents();frame();const auto s=metal->GetStaticPresentationStats();
            unsupported=s.denied[unsigned(krkrsdl3::static_presentation::Denial::Unsupported)]!=0;
            if(unsupported || s.skipped) {skipped=s.skipped!=0;break;}
            SDL_Delay(100);
        }
        if(unsupported) std::cout<<"SKIP: static present needs real displayed acknowledgement on this platform\n";
        else {
            Require(skipped,"unchanged frame never confirmed/skipped");
            const auto before=metal->GetStaticPresentationStats().skipped;
            auto ticket=gpu.GetCurrentLayerPresentation();frame();
            Require(metal->GetStaticPresentationStats().skipped==before,"new alpha ticket skipped");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!ticket->presented.load(std::memory_order_acquire) && !ticket->failed.load(std::memory_order_acquire) &&
                  std::chrono::steady_clock::now()<deadline)SDL_Delay(10);
            Require(ticket->presented.load(std::memory_order_acquire) && !ticket->failed.load(),"fresh ticket lacked display proof");
            seed[0]^=0xff;Require(gpu.UpdateLayerTexture(texture,seed.data(),16,{0,0,4,4}),"mutated static image");
            frame();Require(metal->GetStaticPresentationStats().skipped==before,"mutated resource skipped");
            metal->InvalidatePresentation("test.expose");frame();
            Require(metal->GetStaticPresentationStats().skipped==before,"forced expose refresh skipped");
            std::vector<uint8_t> capture;int width=0,height=0,pitch=0;
            Require(gpu.CaptureFrame(capture,width,height,pitch),"static presentation capture");
            frame();Require(metal->GetStaticPresentationStats().skipped==before,"capture refresh skipped");
            verified=true;
        }
        gpu.DestroyLayerTexture(texture);
    }
    Require(SDL_HideWindow(window),"restore hidden native fixture");
    SDL_SetHint("MIKAGE_METAL_STATIC_PRESENT",previous?"1":"0");
    return verified;
}
