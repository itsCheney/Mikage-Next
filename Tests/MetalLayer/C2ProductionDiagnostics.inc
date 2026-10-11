// Real facade/backend transactions; native builds execute the shrink kernel.
static void C2ProductionDiagnostics(iTVPRenderBackend*) {
    namespace work=krkrsdl3::layer_work;
    namespace trace=krkrsdl3::cpu_consumer_trace;
    const bool savedCapture=TVPTestCaptureLogs;
    const bool savedPointTrace=krkrsdl3::point_trace::Enabled();
    std::vector<uint8_t> outputs[2];
    std::array<uint64_t,4> counts[2]{};
#ifdef TEST_NATIVE_METAL
    std::array<uint64_t,2> nativeCounts[2]{};
#endif
    for(int enabled=0;enabled<2;++enabled) {
        work::SetEnabled(false);
        auto* manager=TVPGetRenderManager();
        auto source=Create(manager,4,4,TVPTextureFormat::RGBA,Image(4,4,4,82));
        auto target=Create(manager,2,2,TVPTextureFormat::RGBA,Image(2,2,4,9));
        source->GetTextureHandle();target->GetTextureHandle();
        TVPLayerShrinkOperation op;op.kind=TVPLayerShrinkKind::Fast;op.destination={0,0,2,2};
        op.sourceRows=4;
        auto axes=std::make_shared<std::vector<TVPLayerShrinkAxis>>();
        axes->push_back({0,2,0,0,0,0,2});axes->push_back({2,2,0,0,0,0,2});
        op.horizontal=axes;op.vertical=axes;
        TVPTestCaptureLogs=true;TVPTestLogs.clear();
        work::SetEnabled(enabled!=0);krkrsdl3::point_trace::SetEnabled(enabled!=0);
        const auto before=TVPGetMetalLayerRenderStats();
#ifdef TEST_NATIVE_METAL
        const auto beforeSubmits=testMetalSubmits,beforeWaits=testMetalWaits;
#endif
        {
            trace::ConsumerScope producer("Layer.shrinkCopyFast",trace::Access::Write,"native.shrinkCopy",42);
            krkrsdl3::point_trace::WriterScope writer("Layer.shrinkCopyFast");
            Require(TVPTryMetalLayerShrink(op,target.get(),source.get())==TVPLayerShrinkResult::Applied,
                    "C2 diagnostic shrink transaction rejected");
        }
        {
            trace::ConsumerScope consumer("Layer.saveLayerImage",trace::Access::Read,"native.saveLayerImage",42);
            const auto* data=static_cast<const uint8_t*>(target->GetScanLineForRead(0));
            outputs[enabled].assign(data,data+16);
            Require(target->GetScanLineForRead(0)==data,"C2 read cache changed between rows");
        }
        const auto after=TVPGetMetalLayerRenderStats();
        uint64_t reads=0;
        for(int i=0;i<int(TVPLayerReadbackSource::Count);++i)
            reads+=after.readbackCountBySource[i]-before.readbackCountBySource[i];
        counts[enabled]={after.cpuFallbacks-before.cpuFallbacks,after.uploadedBytes-before.uploadedBytes,
                         after.readbackBytes-before.readbackBytes,reads};
#ifdef TEST_NATIVE_METAL
        nativeCounts[enabled]={testMetalSubmits-beforeSubmits,testMetalWaits-beforeWaits};
#endif
        const auto profile=work::Take();
        size_t consumers=0,producers=0,callers=0;
        for(const auto& message:TVPTestLogs) {
            consumers+=message.find("metal.cpuConsumer {\"phase\":\"read\"")==0;
            callers+=message.find("metal.cpuConsumer {\"phase\":\"caller\"")==0;
            producers+=message.find("metal.cpuProducer {\"phase\":\"shrink\"")==0;
        }
        Require(consumers==size_t(enabled) && producers==size_t(enabled) && callers==size_t(enabled),
                "C2 cached read/disabled path emitted duplicate or missing detail");
        if(enabled) {
            const auto tuple=C0Tuple(profile.transferOrigins,"read:bitmap.scanline");
            Require(tuple[0]==1 && tuple[1]==16,"C2 detail duplicated C0 transfer totals");
            Require(profile.shrinkReadWaitSampleCount==0,"consumer read attributed to finished shrink scope");
        }
        work::SetEnabled(false);
    }
    TVPTestCaptureLogs=savedCapture;krkrsdl3::point_trace::SetEnabled(savedPointTrace);
    Require(outputs[0]==outputs[1],"C2 diagnostic switch changed exact output pixels");
    Require(counts[0]==counts[1] && counts[1][0]==0 && counts[1][1]==0 && counts[1][2]==16 && counts[1][3]==1,
            "C2 diagnostics changed fallback/upload/readback/backend read counts");
#ifdef TEST_NATIVE_METAL
    Require(nativeCounts[0]==nativeCounts[1],"C2 diagnostics added native submit or wait");
#endif
    std::cout<<"PASS C2A real renderer diagnostics: exact pixels/counters, one producer/read and cached-read suppression\n";
}
