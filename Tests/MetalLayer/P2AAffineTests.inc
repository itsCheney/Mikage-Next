// Kept in the main test translation unit to share native/double texture helpers.
static void P2AAffineBlends(iTVPRenderBackend* backend) {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    const char* methods[]={"AlphaBlend","AlphaBlend_HDA","AlphaBlend_d","AlphaBlend_a",
        "ConstAlphaBlend","ConstAlphaBlend_HDA","ConstAlphaBlend_d","ConstAlphaBlend_a",
        "AdditiveAlphaBlend","AdditiveAlphaBlend_HDA","AdditiveAlphaBlend_a",
        "PsMulBlend","PsMulBlend_HDA","PsOverlayBlend","PsOverlayBlend_HDA",
        "PsHardLightBlend","PsHardLightBlend_HDA","PsScreenBlend","PsColorDodge5Blend",
        "PsAlphaBlend","PsAddBlend","PsSubBlend","PsSoftLightBlend","PsColorDodgeBlend",
        "PsColorBurnBlend","PsLightenBlend","PsDarkenBlend","PsDiffBlend","PsDiff5Blend","PsExclusionBlend"};
    constexpr int w=19,h=15;
    // One rectangle, rotated/fractional, shear, and reflected nonrectangular quad.
    const double matrices[][6]={{1,0,0,1,0,0},{0.8,0.6,-0.6,0.8,7.3,-1.7},
        {1,0.25,0.35,1,0.2,1.3},{-0.8,0.6,0.6,0.8,11.2,0.7}};
    const tTVPRect clips[]={tTVPRect(0,0,w,h),tTVPRect(3,2,16,12)};
    const tTVPRect crops[]={tTVPRect(0,0,11,9),tTVPRect(3,2,14,11)};
    auto destination=Image(w,h,4,41),source=Image(w,h,4,13),reference=Image(w,h,4,71);
    unsigned cases=0;
    TVPTakeMetalLayerTriangleProfile();
    for(auto name:methods) for(int opacity:{0,1,127,128,254,255})
    for(int sampling:{0,1,2}) for(unsigned geometry=0;geometry<4;++geometry)
    for(const auto& clip:clips) for(const auto& crop:crops) for(bool alias:{false,true}) {
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,opacity);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,destination),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,destination);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,source),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,source);
        auto sr=Create(sw,w,h,TVPTextureFormat::RGBA,reference),gr=Create(gpu,w,h,TVPTextureFormat::RGBA,reference);
        tTVPPointD sp[]={{double(crop.left),double(crop.top)},{double(crop.right),double(crop.top)},
            {double(crop.left),double(crop.bottom)},{double(crop.right),double(crop.top)},
            {double(crop.left),double(crop.bottom)},{double(crop.right),double(crop.bottom)}};
        const auto& m=matrices[geometry];
        auto transform=[&](double x,double y) {return tTVPPointD{m[0]*x+m[2]*y+m[4]+(geometry?0:crop.left),
            m[1]*x+m[3]*y+m[5]+(geometry?0:crop.top)};};
        tTVPPointD dp[6]; dp[0]=transform(geometry?-0.5:0,geometry?-0.5:0);
        dp[1]=transform(crop.get_width()-(geometry?0.5:0),geometry?-0.5:0);
        dp[2]=transform(geometry?-0.5:0,crop.get_height()-(geometry?0.5:0));
        dp[3]=dp[1]; dp[4]=dp[2]; dp[5]={dp[1].x-dp[0].x+dp[2].x,dp[1].y-dp[0].y+dp[2].y};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected.get():ss.get(),sp),gi(alias?actual.get():gs.get(),sp);
        // Triangle software ignores reference. Vary it without changing the oracle.
        iTVPTexture2D* gpuReference=cases%3==0?nullptr:cases%3==1?actual.get():gr.get();
        actual->GetTextureHandle(); gs->GetTextureHandle(); gr->GetTextureHandle();
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
        sw->OperateTriangles(method,2,expected.get(),sr.get(),clip,dp,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateTriangles(method,2,actual.get(),gpuReference,clip,dp,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.uploadedBytes==before.uploadedBytes && after.readbackBytes==before.readbackBytes,
                "P2A supported blend did not remain resident");
        Compare(expected.get(),actual.get(),0,name); ++cases;
    }
    const auto profile=TVPTakeMetalLayerTriangleProfile();
    Require(profile.stats.gpuCalls==cases && profile.stats.calls==0,"P2A supported triangle profile lost successful calls");
    Require(TVPTakeMetalLayerTriangleProfile().stats.gpuCalls==0,"P2A profile interval did not reset");
    unsigned boundaryCases=0;
    for(auto name:{"AlphaBlend_d","ConstAlphaBlend_HDA","AdditiveAlphaBlend_a","PsSoftLightBlend"})
    for(const auto& size:std::vector<std::pair<int,int>>{{1,1},{1,7},{7,1}})
    for(int sampling:{0,1,2}) for(const auto& clip:{tTVPRect(0,0,w,h),tTVPRect(6,6,7,7)}) {
        auto* blend=gpu->GetRenderMethod(name);blend->SetParameterOpa(0,127);
        auto image=Image(size.first,size.second,4,37);
        auto ss=Create(sw,size.first,size.second,TVPTextureFormat::RGBA,image),gs=Create(gpu,size.first,size.second,TVPTextureFormat::RGBA,image);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,destination),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,destination);
        tTVPPointD sp[]={{0,0},{double(size.first),0},{0,double(size.second)},
            {double(size.first),0},{0,double(size.second)},{double(size.first),double(size.second)}};
        tTVPPointD dp[]={{2,3},{12,4},{3,12},{12,4},{3,12},{13,13}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),sp),gi(gs.get(),sp);
        actual->GetTextureHandle();gs->GetTextureHandle();
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
        sw->OperateTriangles(blend,2,expected.get(),nullptr,clip,dp,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateTriangles(blend,2,actual.get(),nullptr,clip,dp,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "P2A single-pixel/dimension boundary left GPU residency");
        Compare(expected.get(),actual.get(),0,"P2A single-pixel/dimension affine boundary"); ++boundaryCases;
    }

    // Rejected calls are also pixel oracles: sampler, source ROI, malformed quad,
    // mirrored rectangle, shifted rectangle alias, CPU pin and outstanding lease.
    auto* method=gpu->GetRenderMethod("AlphaBlend_d"); method->SetParameterOpa(0,127);
    unsigned fallbackCases=0;
    for(unsigned failure=0;failure<9;++failure) {
#ifdef TEST_NATIVE_METAL
        if(failure==8) continue;
#else
        auto* doubleDevice=static_cast<DeviceDouble*>(backend); doubleDevice->rejectAffine=failure==8;
#endif
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,destination),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,destination);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,source),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,source);
        tTVPPointD sp[]={{1,1},{14,1},{1,10},{14,1},{1,10},{14,10}};
        tTVPPointD dp[]={{2,2},{16,3},{3,12},{16,3},{3,12},{17,13}};
        if(failure==1) for(auto& p:sp) p.x+=0.25;
        if(failure==2) dp[5].x+=1;
        if(failure==3) {dp[0]={16,2};dp[1]={2,2};dp[2]={16,12};dp[3]=dp[1];dp[4]=dp[2];dp[5]={2,12};}
        if(failure==4) {dp[0]={2,2};dp[1]={15,2};dp[2]={2,11};dp[3]=dp[1];dp[4]=dp[2];dp[5]={15,11};}
        actual->GetTextureHandle(); gs->GetTextureHandle();
        void* lease=nullptr;
        if(failure==5 || failure==6) lease=actual->GetPersistentCPUData(failure==6);
        tTVPScopedTexturePixels scoped;
        if(failure==7) scoped.Acquire(actual.get(),true,"test.affine.lease");
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(failure==4?expected.get():ss.get(),sp),gi(failure==4?actual.get():gs.get(),sp);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),failure==0?3:1);
        const auto& clip=failure==7?clips[1]:clips[0];
        sw->OperateTriangles(method,2,expected.get(),nullptr,clip,dp,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateTriangles(method,2,actual.get(),nullptr,clip,dp,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.cpuFallbacks==before.cpuFallbacks+1 && after.gpuOperations==before.gpuOperations,
                "P2A unsupported blend domain did not fall back");
        const auto reason=failure==0?TVPLayerGPURejectReason::UnsupportedStretch:
            failure==4?TVPLayerGPURejectReason::AffineAlias:
            failure==8?TVPLayerGPURejectReason::BackendFailure:
            failure>=5?TVPLayerGPURejectReason::TargetCPUResident:TVPLayerGPURejectReason::InvalidGeometry;
        Require(after.gpuRejectCountByReason[int(reason)]==before.gpuRejectCountByReason[int(reason)]+1,
                "P2A rejection lost specific diagnostics");
        Compare(expected.get(),actual.get(),0,"P2A unsupported affine blend");
        if(failure==7) {scoped.Written(clip); scoped.Reset();}
        if(lease) Require(actual->GetPersistentCPUData(failure==6)==lease,"P2A CPU fallback changed retained pointer");
        if(failure==6) actual->ReleasePersistentCPUData(nullptr);
#ifndef TEST_NATIVE_METAL
        doubleDevice->rejectAffine=false;
#endif
        ++fallbackCases;
    }
    // COW copies stay resident and preserve the shared old image while affine
    // reads the new target, regardless of a distinct old-image reference.
    // HDA retains cached alpha; d/a refresh it, and every method refreshes RGB.
    unsigned cacheCases=0;
    for(auto name:{"AlphaBlend_HDA","ConstAlphaBlend_HDA","AdditiveAlphaBlend",
                   "PsSoftLightBlend","AlphaBlend_d","AlphaBlend_a","ConstAlphaBlend_d",
                   "ConstAlphaBlend_a","AdditiveAlphaBlend_a"}) {
        auto* blend=gpu->GetRenderMethod(name); blend->SetParameterOpa(0,191);
        auto old=Create(gpu,w,h,TVPTextureFormat::RGBA,destination);
        old->GetTextureHandle(); old->AddRef();
        const auto copyBefore=TVPGetMetalLayerRenderStats();
        Texture actual(gpu->CreateTexture2D(w,h,old.get()));
        old->Release();
        Require(TVPGetMetalLayerRenderStats().readbackBytes==copyBefore.readbackBytes,"P2A COW copied through CPU");
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,destination);
        auto unchanged=Create(sw,w,h,TVPTextureFormat::RGBA,destination);
        tTVPPointD sp[]={{1,1},{14,1},{1,10},{14,1},{1,10},{14,10}};
        tTVPPointD dp[]={{2,2},{16,3},{3,12},{16,3},{3,12},{17,13}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(expected.get(),sp),gi(actual.get(),sp);
        TVPLayerOperation op;Require(blend->DescribeGpuOperation(op),"P2A cache descriptor missing");
        const bool preserve=TVPLayerOperationPreservesAlpha(op,true);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),1);
        actual->GetPoint(6,6);
        for(unsigned i=0;i<3;++i) {
            const auto before=TVPGetMetalLayerRenderStats();
            sw->OperateTriangles(blend,2,expected.get(),unchanged.get(),clips[1],dp,tRenderTexQuadArray(&si,1));
            gpu->OperateTriangles(blend,2,actual.get(),old.get(),clips[1],dp,tRenderTexQuadArray(&gi,1));
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                    after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                    "P2A COW affine lost GPU residency");
            const auto count=after.readbackCountBySource[int(TVPLayerReadbackSource::Point)];
            Require(actual->GetPointAlpha(6,6)==expected->GetPointAlpha(6,6),"P2A alpha cache is stale");
            Require(TVPGetMetalLayerRenderStats().readbackCountBySource[int(TVPLayerReadbackSource::Point)]==count+(preserve?0:1),
                    "P2A affine alpha cache invalidation differs from flags");
            Require(actual->GetPoint(6,6)==expected->GetPoint(6,6),"P2A RGB cache is stale");
        }
        Compare(expected.get(),actual.get(),0,"P2A COW repeated affine");
        Compare(unchanged.get(),old.get(),0,"P2A shared old image unchanged"); ++cacheCases;
    }
    // Keep all GPU work queued until the two final comparisons. Different-size
    // targets and alternating clips force alias/destination snapshot reuse and
    // resizing, while subsequent operations must observe earlier rounded bytes.
    {
        auto first=Image(19,15,4,41),second=Image(31,23,4,57),pixels=Image(31,23,4,13);
        Texture expected[]={Create(sw,19,15,TVPTextureFormat::RGBA,first),Create(sw,31,23,TVPTextureFormat::RGBA,second)};
        Texture actual[]={Create(gpu,19,15,TVPTextureFormat::RGBA,first),Create(gpu,31,23,TVPTextureFormat::RGBA,second)};
        auto ss=Create(sw,31,23,TVPTextureFormat::RGBA,pixels),gs=Create(gpu,31,23,TVPTextureFormat::RGBA,pixels);
        actual[0]->GetTextureHandle();actual[1]->GetTextureHandle();gs->GetTextureHandle();
        tTVPPointD dp[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{16.3,3.7},{3.8,11.7},{17.8,13.7}};
        const char* names[]={"AlphaBlend_d","ConstAlphaBlend_a","AdditiveAlphaBlend_a","PsSoftLightBlend"};
        const int opacity[]={0,1,127,128,254,255};
        const auto before=TVPGetMetalLayerRenderStats();
#ifdef TEST_NATIVE_METAL
        const auto submits=testMetalSubmits,waits=testMetalWaits;
#endif
        for(unsigned i=0;i<24;++i) {
            const unsigned target=i%2; const bool alias=i%3!=2;
            // Resize the source-crop snapshot as well as the old-target clip.
            const bool smallCrop=(i/2)%2!=0;
            const double right=smallCrop?9:14,bottom=smallCrop?7:10;
            tTVPPointD sp[]={{1,1},{right,1},{1,bottom},{right,1},{1,bottom},{right,bottom}};
            auto* blend=gpu->GetRenderMethod(names[i%4]);blend->SetParameterOpa(0,opacity[i%6]);
            const tTVPRect clip=i%4<2?tTVPRect(0,0,target?31:19,target?23:15):tTVPRect(3,2,16,12);
            std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected[target].get():ss.get(),sp),
                gi(alias?actual[target].get():gs.get(),sp);
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),i%3);
            sw->OperateTriangles(blend,2,expected[target].get(),expected[1-target].get(),clip,dp,tRenderTexQuadArray(&si,1));
            gpu->OperateTriangles(blend,2,actual[target].get(),actual[1-target].get(),clip,dp,tRenderTexQuadArray(&gi,1));
        }
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+24 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "P2A multi-blend queued batch transferred CPU pixels");
#ifdef TEST_NATIVE_METAL
        Require(testMetalSubmits-submits<=1 && testMetalWaits==waits,"P2A multi-blend batch submitted/waited per operation");
#endif
        Compare(expected[0].get(),actual[0].get(),0,"P2A queued blend batch first target");
        Compare(expected[1].get(),actual[1].get(),0,"P2A queued blend batch resized snapshots");
    }
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2A affine blends: "<<cases<<" exact comparisons, "<<boundaryCases<<" boundary comparisons, "<<fallbackCases
             <<" fallback comparisons, "<<cacheCases<<" COW/cache cases and 24-operation queued blend batch\n";
    (void)backend;
}
static void P2AAffineAlphaTableFallback(iTVPRenderBackend* backend) {
#ifndef TEST_NATIVE_METAL
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    auto* device=static_cast<DeviceDouble*>(backend);device->rejectAlphaTables=true;
    auto* method=gpu->GetRenderMethod("AlphaBlend_d");method->SetParameterOpa(0,127);
    constexpr int w=19,h=15;
    auto destination=Image(w,h,4,41),source=Image(w,h,4,13);
    auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,destination),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,destination);
    auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,source),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,source);
    tTVPPointD sp[]={{1,1},{14,1},{1,10},{14,1},{1,10},{14,10}};
    tTVPPointD dp[]={{2,2},{16,3},{3,12},{16,3},{3,12},{17,13}};
    std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),sp),gi(gs.get(),sp);
    actual->GetTextureHandle();gs->GetTextureHandle();
    const tTVPRect clip(0,0,w,h);
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),1);
    sw->OperateTriangles(method,2,expected.get(),nullptr,clip,dp,tRenderTexQuadArray(&si,1));
    const auto before=TVPGetMetalLayerRenderStats();
    gpu->OperateTriangles(method,2,actual.get(),nullptr,clip,dp,tRenderTexQuadArray(&gi,1));
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.cpuFallbacks==before.cpuFallbacks+1 && after.gpuOperations==before.gpuOperations &&
        after.gpuRejectCountByReason[int(TVPLayerGPURejectReason::AlphaTables)]==
        before.gpuRejectCountByReason[int(TVPLayerGPURejectReason::AlphaTables)]+1,
        "P2A alpha-table provisioning failure encoded or lost attribution");
    Compare(expected.get(),actual.get(),0,"P2A alpha tables fail before affine write");
    auto untouched=Create(sw,w,h,TVPTextureFormat::RGBA,source);
    Compare(untouched.get(),gs.get(),0,"P2A alpha-table failure preserves source");
    device->rejectAlphaTables=false;
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2A affine alpha tables: provisioning failure before writes\n";
#else
    (void)backend;
#endif
}
static void P2AAffineTableFallbacks(iTVPRenderBackend* backend) {
#ifndef TEST_NATIVE_METAL
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    constexpr int w=19,h=15;
    auto destination=Image(w,h,4,41),source=Image(w,h,4,13);
    // Table availability is a backend failure, even for a prepared affine warp.
    auto* device=dynamic_cast<DeviceDouble*>(backend); Require(device,"P2A expected device double");
    device->rejectPsTables=true;
    for(auto name:{"PsSoftLightBlend","PsColorDodgeBlend","PsColorBurnBlend"}) {
        auto* ps=gpu->GetRenderMethod(name); ps->SetParameterOpa(0,128);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,destination),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,destination);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,source),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,source);
        tTVPPointD sp[]={{1,1},{14,1},{1,10},{14,1},{1,10},{14,10}};
        tTVPPointD dp[]={{2,2},{16,3},{3,12},{16,3},{3,12},{17,13}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),sp),gi(gs.get(),sp);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),1);
        const tTVPRect clip(0,0,w,h);
        sw->OperateTriangles(ps,2,expected.get(),nullptr,clip,dp,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateTriangles(ps,2,actual.get(),nullptr,clip,dp,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.cpuFallbacks==before.cpuFallbacks+1 &&
            after.gpuRejectCountByReason[int(TVPLayerGPURejectReason::PsTables)]==
            before.gpuRejectCountByReason[int(TVPLayerGPURejectReason::PsTables)]+1,"P2A missing PS tables did not fall back");
        Compare(expected.get(),actual.get(),0,"P2A affine missing PS tables");
    }
    device->rejectPsTables=false;
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2A affine PS tables: 3 injected provisioning failures\n";
#else
    (void)backend;
#endif
}
