// Integration with real texture/cache/lease paths; native builds use Metal.
static std::array<uint64_t,4> C0Tuple(const std::string& value,const std::string& key) {
    const auto found=value.find(key+"="); Require(found!=std::string::npos,(std::string("C0 missing origin: ")+key).c_str());
    unsigned long long a,b,c,d;
    Require(std::sscanf(value.c_str()+found+key.size()+1,"%llu/%llu/%llu/%llu",&a,&b,&c,&d)==4,"C0 malformed metric tuple");
    return {uint64_t(a),uint64_t(b),uint64_t(c),uint64_t(d)};
}
static void C0ProductionDiagnostics(iTVPRenderBackend* backend) {
    namespace work=krkrsdl3::layer_work;
    auto* gpu=TVPGetRenderManager();auto* sw=TVPGetSoftwareRenderManager();
    auto image=Image(8,8,4,31);
    unsigned originCases=0;
    for(const char* origin:{"transition.source","transition.outputOverwrite","shrinkCopy.read","shrinkCopy.write",
            "layerExBase","layerExBase.write","layerExDraw.write"}) {
        work::SetEnabled(false);
        auto t=Create(gpu,8,8,TVPTextureFormat::RGBA,image);t->GetTextureHandle();
        work::SetEnabled(true);
        uint64_t wait=0;
        {work::SourceScope tag(origin);t->GetScanLineForRead(0);wait=backend->GetLastReadbackWaitNanoseconds();t->GetScanLineForRead(0);}
        {tTVPScopedTexturePixels write;write.Acquire(t.get(),true,origin);
         auto* data=static_cast<uint32_t*>(write.Data());data[9]=0x8f123456;write.Written(tTVPRect(1,1,2,2));}
        t->GetTextureHandle();t->GetTextureHandle();
        const auto captured=work::Take();
        const auto read=C0Tuple(captured.transferOrigins,std::string("read:")+origin);
        const auto upload=C0Tuple(captured.transferOrigins,std::string("upload:")+origin);
        Require(read[0]==1&&read[1]==image.size()&&read[3]==wait,"C0 full read/cache/wait ownership wrong");
        Require(upload[0]==1&&upload[1]==4&&upload[3]==0,"C0 deferred ROI upload ownership wrong");
        ++originCases;
    }
    // Defaults and tiny direct updates have stable origins without a caller tag.
    work::SetEnabled(false);auto t=Create(gpu,8,8,TVPTextureFormat::RGBA,image);t->GetTextureHandle();
    work::SetEnabled(true);t->LockCPURead();t->UnlockCPU();
    Require(C0Tuple(work::Take().transferOrigins,"read:bitmap.lock")[1]==image.size(),"C0 default lock lost origin");
    t->InvalidateCPUCache();t->GetScanLineForRead(0);
    Require(C0Tuple(work::Take().transferOrigins,"read:bitmap.scanline")[0]==1,"C0 default scanline lost origin");
    const uint32_t pixel=0x29123456;
    for(int i=0;i<20;++i) t->Update(&pixel,TVPTextureFormat::RGBA,4,tTVPRect(i%8,1,i%8+1,2));
    auto tiny=C0Tuple(work::Take().transferOrigins,"upload:bitmap.update");
    Require(tiny[0]==20&&tiny[1]==80,"C0 tiny-update counts/bytes changed");
    // A deferred name is already stored in a fixed-size texture field. It must
    // remain explicitly oversized instead of masquerading as its truncated name.
    const std::string longOrigin(60,'x');
    {tTVPScopedTexturePixels write;write.Acquire(t.get(),true,longOrigin.c_str());
     static_cast<uint32_t*>(write.Data())[0]=pixel;write.Written(tTVPRect(0,0,1,1));}
    t->GetTextureHandle();const auto longProfile=work::Take();
    Require(C0Tuple(longProfile.originOverflow,"upload")[1]==4&&
        longProfile.originOverflow.find("oversizeRecords=2")!=std::string::npos,"C0 deferred long name was silently truncated");
    {work::SourceScope tag(longOrigin.c_str());t->GetPersistentCPUData(true);}
    {work::SourceScope tag("test.shortAgain");t->GetPersistentCPUData(true);}
    t->GetTextureHandle();const auto rawLong=work::Take();
    Require(C0Tuple(rawLong.originOverflow,"upload")[1]==image.size() &&
        rawLong.transferOrigins.find("upload:test.shortAgain=")==std::string::npos,"C0 raw lease erased its earlier oversized origin");
    t->ReleasePersistentCPUData(nullptr);
    work::SetEnabled(false);
    // A/B observation must leave the same actual GPU/CPU work and exact pixels.
    std::array<uint64_t,7> reference{};
    for(bool enabled:{false,true}) {
        auto target=Create(gpu,8,8,TVPTextureFormat::RGBA,image);
        auto expected=Create(sw,8,8,TVPTextureFormat::RGBA,image);target->GetTextureHandle();
        work::SetEnabled(enabled);const auto before=TVPGetMetalLayerRenderStats();
#ifdef TEST_NATIVE_METAL
        const auto submits=testMetalSubmits,waits=testMetalWaits;
#else
        auto* device=static_cast<DeviceDouble*>(backend);
        const auto reads=device->fullReads,updates=device->updateCalls,regions=device->syncRegionReads;
#endif
        {tTVPScopedTexturePixels write;write.Acquire(target.get(),true,"shrinkCopy.write");
         static_cast<uint32_t*>(write.Data())[2]=pixel;write.Written(tTVPRect(2,0,3,1));}
        static_cast<uint32_t*>(expected->GetScanLineForWrite(0))[2]=pixel;
        target->GetTextureHandle();
        auto* fill=gpu->GetRenderMethod("FillColor");fill->SetParameterColor4B(0,0x00224466);
        Operation(gpu,fill,target.get(),tTVPRect(5,5,6,6),nullptr,tTVPRect());
        Operation(sw,fill,expected.get(),tTVPRect(5,5,6,6),nullptr,tTVPRect());
        target->GetPoint(5,5);const auto after=TVPGetMetalLayerRenderStats();
        std::array<uint64_t,7> measured={after.gpuOperations-before.gpuOperations,after.cpuFallbacks-before.cpuFallbacks,
            after.uploadedBytes-before.uploadedBytes,after.readbackBytes-before.readbackBytes,0,0,0};
#ifdef TEST_NATIVE_METAL
        measured[4]=testMetalSubmits-submits;measured[5]=testMetalWaits-waits;
#else
        measured[4]=device->fullReads-reads;measured[5]=device->updateCalls-updates;measured[6]=device->syncRegionReads-regions;
#endif
        if(!enabled) reference=measured;else Require(reference==measured,"C0 diagnostics changed GPU work/submit/wait/transfer counts");
        const auto profile=work::Take();
        Require(enabled?!profile.transferOrigins.empty():profile.transferOrigins.empty(),"C0 disabled recording retained origins");
        work::SetEnabled(false);Compare(expected.get(),target.get(),0,"C0 diagnostics exact pixels");
    }
#ifndef TEST_NATIVE_METAL
    auto* device=static_cast<DeviceDouble*>(backend);
    // Profiling generation changes inside an existing backend call must not
    // charge that completed old-generation call to the new profile.
    auto stale=Create(gpu,8,8,TVPTextureFormat::RGBA,image);stale->GetTextureHandle();
    work::SetEnabled(true);device->cycleDiagnosticsOnRead=true;stale->GetScanLineForRead(0);
    auto cleared=work::Take();Require(cleared.transfers.empty()&&cleared.transferOrigins.empty(),"C0 late real read survived generation reset");
    auto direct=Create(gpu,8,8,TVPTextureFormat::RGBA,image);direct->GetTextureHandle();
    work::SetEnabled(true);device->cycleDiagnosticsOnUpload=true;
    direct->Update(&pixel,TVPTextureFormat::RGBA,4,tTVPRect(0,0,1,1));
    cleared=work::Take();Require(cleared.transfers.empty()&&cleared.transferOrigins.empty(),"C0 late direct update survived generation reset");
    {tTVPScopedTexturePixels write;write.Acquire(direct.get(),true,"test.deferredEpoch");
     static_cast<uint32_t*>(write.Data())[1]=pixel;write.Written(tTVPRect(1,0,2,1));}
    work::SetEnabled(true);device->cycleDiagnosticsOnUpload=true;direct->GetTextureHandle();
    cleared=work::Take();Require(cleared.transfers.empty()&&cleared.transferOrigins.empty(),"C0 late deferred upload survived generation reset");
    direct->InvalidateCPUCache();work::SetEnabled(true);device->cycleDiagnosticsOnRegionRead=true;direct->GetPoint(2,2);
    cleared=work::Take();Require(cleared.transfers.empty()&&cleared.transferOrigins.empty(),"C0 late point region survived generation reset");
    stale->InvalidateCPUCache();
    work::SetEnabled(true);device->failDiagnosticUpload=true;bool threw=false;
    try {stale->Update(&pixel,TVPTextureFormat::RGBA,4,tTVPRect(0,0,1,1));}catch(...){threw=true;}
    device->failDiagnosticUpload=false;
    Require(threw&&work::Take().transferOrigins.empty(),"C0 failed backend upload was recorded as successful");
#endif
    work::SetEnabled(false);
    std::cout<<"PASS C0 production origins: "<<originCases<<" read/write tags, cache hits, tiny ROI, deferred oversized names, disabled/on exact work\n";
}
