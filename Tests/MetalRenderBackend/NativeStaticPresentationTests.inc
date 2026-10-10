// A working Metal device does not establish that the runner has a drawable
// display surface. Probe one real presentation before testing skip behavior.
static void PumpStaticPresentationEvents() {
    SDL_PumpEvents();
    SDL_Event event;
    while(SDL_PollEvent(&event)) {}
}
static bool WaitForStaticPresentation(const std::shared_ptr<krkrsdl3::AsyncLayerPresentation>& ticket) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!ticket->presented.load(std::memory_order_acquire) && !ticket->failed.load(std::memory_order_acquire) &&
          std::chrono::steady_clock::now()<deadline) {
        PumpStaticPresentationEvents();SDL_Delay(10);
    }
    return ticket->presented.load(std::memory_order_acquire) && !ticket->failed.load(std::memory_order_acquire);
}
bool NativeStaticPresentationTests(SDL_Window* window) {
    bool verified=false;
    const bool previous=SDL_GetHintBoolean("MIKAGE_METAL_STATIC_PRESENT",true);
    SDL_SetHint("MIKAGE_METAL_STATIC_PRESENT","1");
    Require(SDL_ShowWindow(window),"show window for real presentation acknowledgement");
    PumpStaticPresentationEvents();SDL_Delay(50);
    {
        std::unique_ptr<iTVPRenderBackend> owner(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(bool(owner),"static present backend");auto& gpu=*owner;
        auto* metal=dynamic_cast<krkrsdl3::MetalRenderBackend*>(&gpu);
        void* texture=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
        std::vector<uint8_t> seed(64,0x65);
        Require(texture && gpu.UpdateLayerTexture(texture,seed.data(),16,{0,0,4,4}),"static present image");
        const auto frame=[&](){gpu.BeginFrame(256,256);gpu.DrawWindowTexture(texture,0,0,256,256);gpu.EndFrame();};
        auto initial=gpu.GetCurrentLayerPresentation();frame();
        std::vector<uint8_t> probe;int probePitch=0;
        Require(gpu.ReadLayerTexture(texture,probe,probePitch) && probe==seed,"static present probe command failed");
        const bool unsupported=metal->GetStaticPresentationStats().denied[
            unsigned(krkrsdl3::static_presentation::Denial::Unsupported)]!=0;
        // Do not repeatedly enqueue presents while waiting: 16 unacknowledged
        // records correctly disable skipping until lifecycle invalidation.
        const bool displayed=!unsupported && WaitForStaticPresentation(initial);
        if(unsupported) std::cout<<"SKIP: static present needs real displayed acknowledgement on this platform\n";
        else if(!displayed && !initial->failed.load(std::memory_order_acquire))
            std::cout<<"SKIP: runner did not acknowledge the first real drawable display; static present remains unverified\n";
        else {
            Require(displayed,"static present probe failed");
            bool skipped=false;
            for(int i=0;i<10;++i) {
                PumpStaticPresentationEvents();frame();
                if(metal->GetStaticPresentationStats().skipped) {skipped=true;break;}
                for(int wait=0;wait<10;++wait) {PumpStaticPresentationEvents();SDL_Delay(10);}
            }
            if(!skipped) {
                const auto stats=metal->GetStaticPresentationStats();
                std::cerr<<"static present denials:";
                for(size_t i=0;i<stats.denied.size();++i)std::cerr<<' '<<i<<'='<<stats.denied[i];
                std::cerr<<'\n';
            }
            Require(skipped,"unchanged frame never confirmed/skipped");
            const auto before=metal->GetStaticPresentationStats().skipped;
            auto ticket=gpu.GetCurrentLayerPresentation();frame();
            Require(metal->GetStaticPresentationStats().skipped==before,"new alpha ticket skipped");
            Require(WaitForStaticPresentation(ticket),"fresh ticket lacked display proof");
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
