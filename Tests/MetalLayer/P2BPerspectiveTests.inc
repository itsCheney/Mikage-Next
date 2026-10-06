// Shared test TU supplies native/double resources and exact surface comparison.
static void P2BSoftwareAndMath() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* method=sw->GetRenderMethod("Copy");
    auto image=Image(19,15,4,13),initial=Image(19,15,4,41);
    auto source=Create(sw,19,15,TVPTextureFormat::RGBA,image);
    tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10}};
    tTVPPointD dst[]={{2,2},{15,2},{2,11},{15,11}};
    auto expected=Create(sw,19,15,TVPTextureFormat::RGBA,initial),actual=Create(sw,19,15,TVPTextureFormat::RGBA,initial);
    sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
    Operation(sw,method,expected.get(),tTVPRect(2,2,15,11),source.get(),tTVPRect(1,1,14,10));
    std::pair<iTVPTexture2D*,const tTVPPointD*> input(source.get(),src);
    sw->OperatePerspective(method,1,actual.get(),nullptr,tTVPRect(0,0,19,15),dst,tRenderTexQuadArray(&input,1));
    Compare(expected.get(),actual.get(),0,"P2B independent point-3 rectangle oracle");
    // Disjoint rectangles must remain no-ops instead of entering full-clip warp.
    actual=Create(sw,19,15,TVPTextureFormat::RGBA,initial);
    auto unchanged=Create(sw,19,15,TVPTextureFormat::RGBA,initial);
    for(auto& p:dst) p.x+=30;
    sw->OperatePerspective(method,1,actual.get(),nullptr,tTVPRect(0,0,19,15),dst,tRenderTexQuadArray(&input,1));
    Compare(unchanged.get(),actual.get(),0,"P2B disjoint software rectangle is empty");
    // Rect clipping clamps before reads and preserves the ordinary crop formula.
    tTVPPointD clipped[]={{-2,-1},{11,-1},{-2,8},{11,8}};
    actual=Create(sw,19,15,TVPTextureFormat::RGBA,initial);expected=Create(sw,19,15,TVPTextureFormat::RGBA,initial);
    Operation(sw,method,expected.get(),tTVPRect(0,0,11,8),source.get(),tTVPRect(3,2,14,10));
    sw->OperatePerspective(method,1,actual.get(),nullptr,tTVPRect(-5,-4,25,20),clipped,tRenderTexQuadArray(&input,1));
    Compare(expected.get(),actual.get(),0,"P2B independent clamped rectangle oracle");
    uint64_t coordinates=0;
    tTVPPointD warp[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7}};
    for(int variant=0;variant<5;++variant) {
        auto qdst=std::array<tTVPPointD,4>{warp[0],warp[1],warp[2],warp[3]};
        if(variant==1) {qdst[0]={4,1};qdst[1]={7,1};qdst[2]={1,13};qdst[3]={18,13};}
        if(variant==2) {std::swap(qdst[0],qdst[1]);std::swap(qdst[2],qdst[3]);}
        if(variant==3) for(auto& p:qdst) {p.x+=0.037;p.y-=0.173;}
        if(variant==4) {qdst[1].x=18.7;qdst[3].x=7.6;}
        TVPLayerPerspectiveQuad map;
        Require(layer_perspective::PrepareQuad(qdst.data(),src,19,15,TVPLayerRect{3,2,16,12},map),"P2B math fixture prepare failed");
        float hi[9],lo[9];for(int i=0;i<9;++i){hi[i]=float(map.inverse[i]);lo[i]=float(map.inverse[i]-double(hi[i]));}
        for(int y=0;y<10;++y) for(int x=0;x<13;++x) for(int row=0;row<2;++row) {
            const int j=row*3;double cx=x+0.5,cy=y+0.5;
            double denominator=map.inverse[6]*cx+map.inverse[7]*cy+map.inverse[8];
            float wanted=std::abs(denominator)<1e-12?-1.0f:float((map.inverse[j]*cx+map.inverse[j+1]*cy+map.inverse[j+2])/denominator);
            float value=affine_shader::perspectiveCoordinate(hi[j],lo[j],hi[j+1],lo[j+1],hi[j+2],lo[j+2],hi[6],lo[6],hi[7],lo[7],hi[8],lo[8],float(cx),float(cy));
            if(value!=wanted) {std::cerr.precision(17);std::cerr<<"perspective pair mismatch variant="<<variant<<" x="<<x<<" y="<<y<<" row="<<row<<" expected="<<wanted<<" actual="<<value<<" inverse=";for(double v:map.inverse)std::cerr<<v<<',';std::cerr<<'\n';}
            Require(value==wanted,"P2B production perspective coordinate differs from software float rounding"); ++coordinates;
        }
    }
    Require(affine_shader::perspectiveCoordinate(1,0,0,0,0,0,0,0,0,0,0,0,0.5f,0.5f)==-1,
            "P2B zero denominator does not sample transparent");
    Require(affine_shader::perspectiveCoordinate(1,0,0,0,0,0,0,0,0,0,5e-13f,0,0.5f,0.5f)==-1,
            "P2B near-zero denominator threshold changed");
    std::cout<<"PASS P2B software rectangle repairs and "<<coordinates<<" exact production perspective coordinates\n";
}

static void P2BPerspectives(iTVPRenderBackend* backend) {
    auto* sw=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    const char* names[]={"Copy","AlphaBlend","AlphaBlend_HDA","AlphaBlend_d","AlphaBlend_a","PerspectiveAlphaBlend_a",
        "ConstAlphaBlend","ConstAlphaBlend_HDA","ConstAlphaBlend_d","ConstAlphaBlend_a",
        "AdditiveAlphaBlend","AdditiveAlphaBlend_HDA","AdditiveAlphaBlend_a",
        "PsMulBlend","PsMulBlend_HDA","PsOverlayBlend","PsOverlayBlend_HDA","PsHardLightBlend","PsHardLightBlend_HDA",
        "PsScreenBlend","PsColorDodge5Blend","PsAlphaBlend","PsAddBlend","PsSubBlend","PsSoftLightBlend","PsColorDodgeBlend",
        "PsColorBurnBlend","PsLightenBlend","PsDarkenBlend","PsDiffBlend","PsDiff5Blend","PsExclusionBlend"};
    constexpr int w=19,h=15;
    auto initial=Image(w,h,4,41),image=Image(w,h,4,13);
    unsigned cases=0;
    for(auto name:names) for(int opacity:{0,1,127,128,254,255}) for(int sampling:{0,1,2})
    for(int geometry=0;geometry<6;++geometry) for(bool alias:{false,true}) {
        auto* method=gpu->GetRenderMethod(name);method->SetParameterOpa(0,opacity);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,image),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10}};
        tTVPPointD dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7}};
        if(geometry==0) std::copy(src,src+4,dst);
        if(geometry==1) {dst[0]={4,1};dst[1]={7,1};dst[2]={1,13};dst[3]={18,13};}
        if(geometry==2) {std::swap(dst[0],dst[1]);std::swap(dst[2],dst[3]);}
        if(geometry==3) for(auto& p:src) {p.x+=0.25;p.y+=0.375;}
        if(geometry==4) for(auto& p:src) {p.x-=4;p.y-=3;}
        if(geometry==5) {src[1].y+=1.25;src[2].x+=1.75;}
        const tTVPRect clip=geometry%2?tTVPRect(3,2,16,12):tTVPRect(-2,-1,23,19);
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected.get():ss.get(),src),gi(alias?actual.get():gs.get(),src);
        actual->GetTextureHandle();gs->GetTextureHandle();
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
        sw->OperatePerspective(method,1,expected.get(),ss.get(),clip,dst,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(method,1,actual.get(),cases%2?actual.get():gs.get(),clip,dst,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
            after.uploadedBytes==before.uploadedBytes && after.readbackBytes==before.readbackBytes,"P2B supported perspective lost residency");
        Compare(expected.get(),actual.get(),0,name);++cases;
    }
    // Multi-quad order and rectangle/warp mixtures, including alias snapshots
    // reading the prefix already written by the preceding quad.
    unsigned batches=0;
    for(auto name:{"Copy","AlphaBlend_d","ConstAlphaBlend_a","AdditiveAlphaBlend_a","PsSoftLightBlend"})
    for(int sampling:{0,1,2}) for(bool alias:{false,true}) {
        auto* method=gpu->GetRenderMethod(name);method->SetParameterOpa(0,191);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,image),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10},{1,1},{14,1},{1,10},{14,10},{2,2},{12,2},{2,11},{12,11}};
        tTVPPointD dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7},
            {1,1},{14,1},{1,10},{14,10},{1.7,3.3},{17.2,1.3},{2.7,13.3},{12.9,12.3}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected.get():ss.get(),src),gi(alias?actual.get():gs.get(),src);
        actual->GetTextureHandle();gs->GetTextureHandle();gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
        sw->OperatePerspective(method,3,expected.get(),ss.get(),tTVPRect(0,0,w,h),dst,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(method,3,actual.get(),gs.get(),tTVPRect(0,0,w,h),dst,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
            after.uploadedBytes==before.uploadedBytes && after.readbackBytes==before.readbackBytes,"P2B mixed perspective batch lost residency");
        Compare(expected.get(),actual.get(),0,"P2B mixed rectangle/warp ordered batch");++batches;
    }
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2B perspectives: "<<cases<<" exact surfaces and "<<batches<<" mixed ordered batches\n";
    (void)backend;
}

static void P2BRejectsAndLifetimes(iTVPRenderBackend* backend) {
    auto* sw=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    constexpr int w=19,h=15;
    auto initial=Image(w,h,4,41),image=Image(w,h,4,13);
    auto* method=gpu->GetRenderMethod("AlphaBlend_d");method->SetParameterOpa(0,127);
    unsigned safety=0,fallbacks=0;
    // Independent horizon oracle: x=5.5 produces exactly zero denominator.
    // Source bounds and the horizon are transparent; nearest pixels elsewhere
    // come from the initialized source without reading any prepared GPU result.
    {
        std::vector<uint8_t> frame(size_t(w)*h*4);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            double denominator=x+0.5-5.5;
            if(std::abs(denominator)<1e-12) continue;
            float sx=float((x+0.5)/denominator),sy=float((y+0.5)/denominator);
            if(sx<0.5f||sx>=w-0.5f||sy<0.5f||sy>=h-0.5f) continue;
            int px=std::clamp(int(sx+0.5f),0,w-1),py=std::clamp(int(sy+0.5f),0,h-1);
            std::memcpy(frame.data()+(size_t(y)*w+x)*4,image.data()+(size_t(py)*w+px)*4,4);
        }
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,frame),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto source=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        TVPLayerPerspectiveQuad horizon;horizon.clip={0,0,w,h};
        horizon.inverse[0]=horizon.inverse[4]=horizon.inverse[6]=1;horizon.inverse[8]=-5.5;
        TVPLayerOperation copy;copy.kind=TVPLayerOperationKind::Copy;
        Require(backend->OperateLayerPerspective(copy,actual->GetTextureHandle(),&horizon,1,source->GetTextureHandle(),0),"P2B horizon mapping not accepted");
        actual->CommitGPURegionWrite(tTVPRect(0,0,w,h));
        Compare(expected.get(),actual.get(),0,"P2B independent denominator-zero transparency");
    }
    {
        auto actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial),unchanged=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
        auto gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        void* target=actual->GetTextureHandle();void* source=gs->GetTextureHandle();
        TVPLayerOperation op;Require(method->DescribeGpuOperation(op),"P2B direct descriptor missing");
        Require(!backend->iTVPRenderBackend::OperateLayerPerspective(op,target,nullptr,0,source,0),"P2B optional backend API default is not false");
        TVPLayerPerspectiveQuad maps[2];
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10}},dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7}};
        Require(layer_perspective::PrepareQuad(dst,src,w,h,TVPLayerRect{0,0,w,h},maps[0]),"P2B direct prepared fixture failed");
#ifdef TEST_NATIVE_METAL
        const auto submits=testMetalSubmits,waits=testMetalWaits,blits=testMetalBlits;
#endif
        for(int invalid=0;invalid<10;++invalid) {
            maps[1]=maps[0];auto invalidOp=op;int sampler=0;size_t count=2;
            if(invalid==0) maps[1].inverse[0]=std::numeric_limits<double>::quiet_NaN();
            if(invalid==1) maps[1].inverse[0]=1000001;
            if(invalid==2) std::fill(maps[1].inverse,maps[1].inverse+9,0);
            if(invalid==3) maps[1].clip.left=-1;
            if(invalid==4) invalidOp.flags=16;
            if(invalid==5) invalidOp.opacity=-1;
            if(invalid==6) invalidOp.opacity=256;
            if(invalid==7) sampler=2;
            if(invalid==8) count=TVP_LAYER_PERSPECTIVE_MAX_QUADS+1;
            if(invalid==9) {maps[1].rectangle=true;maps[1].source={1,1,14,10};maps[1].destination=maps[1].clip={2,2,15,11};}
            Require(!backend->OperateLayerPerspective(invalidOp,target,maps,count,invalid==9?target:source,sampler),"P2B direct invalid batch encoded prefix");
        }
#ifdef TEST_NATIVE_METAL
        Require(testMetalSubmits==submits&&testMetalWaits==waits&&testMetalBlits==blits,"P2B invalid backend batch encoded or waited before validation");
#else
        // Read physical backend storage before the facade can replay fallback.
        // A Layer CPU cache may still hold the initial image and otherwise hide
        // an incorrectly committed temporary prefix after a late API failure.
        maps[1]=maps[0];auto* device=static_cast<DeviceDouble*>(backend);device->rejectPerspectiveQuad=1;
        Require(!backend->OperateLayerPerspective(op,target,maps,2,source,1),"P2B injected late raw failure unexpectedly committed");
        device->rejectPerspectiveQuad=-1;
        std::vector<uint8_t> physical;int pitch=0;
        Require(backend->ReadLayerTexture(target,physical,pitch)&&pitch==w*4&&physical==initial,
            "P2B late raw API failure committed physical target prefix");
        Require(backend->ReadLayerTexture(source,physical,pitch)&&pitch==w*4&&physical==image,
            "P2B late raw API failure modified independent physical source");
#endif
        Compare(unchanged.get(),actual.get(),0,"P2B raw late map rejects before target write");
    }
    for(int invalid=0;invalid<8;++invalid) for(auto* manager:{sw,gpu}) {
        auto actual=Create(manager,w,h,TVPTextureFormat::RGBA,initial),unchanged=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
        auto source=Create(manager,w,h,invalid==4?TVPTextureFormat::Gray:TVPTextureFormat::RGBA,invalid==4?Image(w,h,1,13):image);
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10},{1,1},{14,1},{1,10},{14,10}};
        tTVPPointD dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7},{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7}};
        int count=2,sampling=1;
        if(invalid==0) for(int i=4;i<8;++i) dst[i]={5,5};
        if(invalid==1) src[6].x=std::numeric_limits<double>::quiet_NaN();
        if(invalid==2) dst[7].y=std::numeric_limits<double>::infinity();
        if(invalid==3) count=-1;
        if(invalid==5) sampling=-1;
        if(invalid==6) {src[4]={-3,0};src[5]={9,0};src[6]={-3,9};src[7]={9,9};dst[4]={1,1};dst[5]={13,1};dst[6]={1,10};dst[7]={13,10};}
        std::pair<iTVPTexture2D*,const tTVPPointD*> inputs[]={{source.get(),src},{source.get(),src}};
        if(manager==gpu) {actual->GetTextureHandle();source->GetTextureHandle();}
        manager->SetParameterInt(manager->EnumParameterID("StretchType"),sampling);
        const auto before=TVPGetMetalLayerRenderStats();bool threw=false;
        try {manager->OperatePerspective(method,count,actual.get(),nullptr,tTVPRect(0,0,w,h),dst,tRenderTexQuadArray(inputs,invalid==7?2:1));}
        catch(...) {threw=true;}
        const auto after=TVPGetMetalLayerRenderStats();
        Require(threw && after.gpuOperations==before.gpuOperations && after.cpuFallbacks==before.cpuFallbacks &&
            after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,"P2B invalid late quad touched target/source or replayed prefix");
        Compare(unchanged.get(),actual.get(),0,"P2B invalid batch leaves entire target unchanged");++safety;
    }
    for(int failure=0;failure<7;++failure) {
#ifdef TEST_NATIVE_METAL
        if(failure==5) continue;
#else
        auto* device=static_cast<DeviceDouble*>(backend);device->rejectPerspectiveQuad=failure==5?1:-1;
#endif
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,image),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10},{1,1},{14,1},{1,10},{14,10}};
        tTVPPointD dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7},{1.7,3.3},{17.2,1.3},{2.7,13.3},{12.9,12.3}};
        if(failure==1 || failure==2) {dst[4]={2,2};dst[5]={15,2};dst[6]={2,11};dst[7]={15,11};}
        auto* blend=gpu->GetRenderMethod(failure==2?"Copy":failure==6?"DoGrayScale":"AlphaBlend_d");blend->SetParameterOpa(0,127);
        const bool alias=failure==1||failure==2;
        actual->GetTextureHandle();gs->GetTextureHandle();
        void* pointer=nullptr;tTVPScopedTexturePixels scoped;
        if(failure==3) pointer=actual->GetPersistentCPUData(true);
        if(failure==4) scoped.Acquire(actual.get(),true,"test.perspective.lease");
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected.get():ss.get(),src),gi(alias?actual.get():gs.get(),src);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),failure==0?3:1);
        sw->OperatePerspective(blend,2,expected.get(),nullptr,tTVPRect(3,2,16,12),dst,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(blend,2,actual.get(),nullptr,tTVPRect(3,2,16,12),dst,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        const auto reason=failure==0?TVPLayerGPURejectReason::UnsupportedStretch:
            failure==1||failure==2?TVPLayerGPURejectReason::PerspectiveAlias:
            failure==3||failure==4?TVPLayerGPURejectReason::TargetCPUResident:
            failure==5?TVPLayerGPURejectReason::BackendFailure:TVPLayerGPURejectReason::UnsupportedKind;
        Require(after.gpuOperations==before.gpuOperations && after.cpuFallbacks==before.cpuFallbacks+1 &&
            after.gpuRejectCountByReason[int(reason)]==before.gpuRejectCountByReason[int(reason)]+1,"P2B rejected batch attribution changed");
        const std::string label="P2B rejected batch is rendered once by software: failure "+std::to_string(failure);
        Compare(expected.get(),actual.get(),0,label.c_str());++fallbacks;
        if(pointer) {Require(pointer==actual->GetPersistentCPUData(true),"P2B retained lease pointer changed");actual->ReleasePersistentCPUData(nullptr);}
        if(failure==4) {scoped.Written(tTVPRect(3,2,16,12));scoped.Reset();}
#ifndef TEST_NATIVE_METAL
        device->rejectPerspectiveQuad=-1;
#endif
    }
    {
        // Finite software matrices outside the backend coefficient bound remain
        // valid software requests; shared preparation must not reject them.
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,image),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        tTVPPointD src[]={{1,1},{14,2},{2,10},{15,11}},dst[]={{2,2},{2.000001,2.0000001},{2.0000001,2.000001},{2.0000011,2.0000011}};
        TVPLayerPerspectiveQuad quad;Require(layer_perspective::PrepareQuad(dst,src,w,h,TVPLayerRect{0,0,w,h},quad),"P2B finite large inverse was rejected by software preparation");
        bool large=false;for(double v:quad.inverse)large|=std::abs(v)>1000000;Require(large,"P2B large inverse fixture no longer exceeds backend range");
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),src),gi(gs.get(),src);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
        sw->OperatePerspective(method,1,expected.get(),nullptr,tTVPRect(0,0,w,h),dst,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(method,1,actual.get(),nullptr,tTVPRect(0,0,w,h),dst,tRenderTexQuadArray(&gi,1));
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks+1,"P2B finite large inverse entered GPU");
        Compare(expected.get(),actual.get(),0,"P2B finite large inverse software fallback");++fallbacks;
    }
    // Oversized GPU batch remains a safe, ordered software request.
    {
        std::vector<tTVPPointD> src,dst;
        for(unsigned i=0;i<TVP_LAYER_PERSPECTIVE_MAX_QUADS+1;++i) {
            for(const auto& p:std::array<tTVPPointD,4>{{{1,1},{14,1},{1,10},{14,10}}}) src.push_back(p);
            for(const auto& p:std::array<tTVPPointD,4>{{{1.3,1.7},{15.3,3.7},{2.8,11.7},{14.8,13.7}}}) dst.push_back(p);
        }
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,image),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),src.data()),gi(gs.get(),src.data());
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
        sw->OperatePerspective(method,TVP_LAYER_PERSPECTIVE_MAX_QUADS+1,expected.get(),nullptr,tTVPRect(0,0,w,h),dst.data(),tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(method,TVP_LAYER_PERSPECTIVE_MAX_QUADS+1,actual.get(),nullptr,tTVPRect(0,0,w,h),dst.data(),tRenderTexQuadArray(&gi,1));
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks+1,"P2B GPU cap accidentally rejects software batch");
        Compare(expected.get(),actual.get(),0,"P2B oversized software batch");++fallbacks;
    }
    for(auto name:{"AlphaBlend_HDA","ConstAlphaBlend_HDA","PsSoftLightBlend","AlphaBlend_d","AlphaBlend_a","AdditiveAlphaBlend_a"}) {
        auto* blend=gpu->GetRenderMethod(name);blend->SetParameterOpa(0,191);
        auto old=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);old->GetTextureHandle();old->AddRef();
        const auto copyBefore=TVPGetMetalLayerRenderStats();Texture actual(gpu->CreateTexture2D(w,h,old.get()));old->Release();
        Require(TVPGetMetalLayerRenderStats().readbackBytes==copyBefore.readbackBytes,"P2B COW copy read CPU");
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),unchanged=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10}},dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(expected.get(),src),gi(actual.get(),src);
        TVPLayerOperation op;Require(blend->DescribeGpuOperation(op),"P2B COW descriptor missing");
        const bool preserve=TVPLayerOperationPreservesAlpha(op,true);actual->GetPoint(6,6);
        for(int i=0;i<3;++i) {
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),i%3);
            sw->OperatePerspective(blend,1,expected.get(),unchanged.get(),tTVPRect(3,2,16,12),dst,tRenderTexQuadArray(&si,1));
            const auto before=TVPGetMetalLayerRenderStats();
            gpu->OperatePerspective(blend,1,actual.get(),old.get(),tTVPRect(3,2,16,12),dst,tRenderTexQuadArray(&gi,1));
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,"P2B COW perspective lost residency");
            const auto pointCount=after.readbackCountBySource[int(TVPLayerReadbackSource::Point)];
            Require(actual->GetPointAlpha(6,6)==expected->GetPointAlpha(6,6),"P2B alpha cache stale");
            Require(TVPGetMetalLayerRenderStats().readbackCountBySource[int(TVPLayerReadbackSource::Point)]==pointCount+(preserve?0:1),"P2B alpha cache invalidation changed");
            Require(actual->GetPoint(6,6)==expected->GetPoint(6,6),"P2B RGB cache stale");
        }
        Compare(expected.get(),actual.get(),0,"P2B repeated COW perspective");Compare(unchanged.get(),old.get(),0,"P2B COW old remains unchanged");
    }
    // Empty/count-zero calls do not read source or publish GPU work.
    {
        auto actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial),unchanged=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
        auto source=Create(gpu,w,h,TVPTextureFormat::RGBA,image);actual->GetTextureHandle();source->GetTextureHandle();
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10}},dst[]={{1,1},{14,1},{1,10},{14,10}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> input(source.get(),src);
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(method,0,actual.get(),nullptr,tTVPRect(0,0,w,h),nullptr,tRenderTexQuadArray(&input,1));
        gpu->OperatePerspective(method,1,actual.get(),nullptr,tTVPRect(25,25,30,30),dst,tRenderTexQuadArray(&input,1));
        for(auto& p:dst) p.x+=30;
        gpu->OperatePerspective(method,1,actual.get(),nullptr,tTVPRect(0,0,w,h),dst,tRenderTexQuadArray(&input,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations&&after.cpuFallbacks==before.cpuFallbacks&&
            after.readbackBytes==before.readbackBytes&&after.uploadedBytes==before.uploadedBytes,"P2B empty batch accessed source or published work");
        Compare(unchanged.get(),actual.get(),0,"P2B empty GPU requests preserve target");
    }
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2B safety: "<<safety<<" atomic invalid batches, "<<fallbacks<<" software fallbacks and 6 COW/cache cases\n";
}

static void P2BResourceFailures(iTVPRenderBackend* backend,bool alpha) {
#ifndef TEST_NATIVE_METAL
    auto* device=static_cast<DeviceDouble*>(backend);
    auto* sw=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    device->rejectAlphaTables=alpha;device->rejectPsTables=!alpha;
    for(auto name:alpha?std::vector<const char*>{"AlphaBlend_d"}:std::vector<const char*>{"PsSoftLightBlend","PsColorDodgeBlend","PsColorBurnBlend"}) {
        auto* method=gpu->GetRenderMethod(name);method->SetParameterOpa(0,128);
        auto initial=Image(19,15,4,41),image=Image(19,15,4,13);
        auto expected=Create(sw,19,15,TVPTextureFormat::RGBA,initial),actual=Create(gpu,19,15,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,19,15,TVPTextureFormat::RGBA,image),gs=Create(gpu,19,15,TVPTextureFormat::RGBA,image);
        tTVPPointD src[]={{1,1},{14,1},{1,10},{14,10},{1,1},{14,1},{1,10},{14,10}},
            dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7},{1.7,3.3},{17.2,1.3},{2.7,13.3},{12.9,12.3}};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),src),gi(gs.get(),src);
        actual->GetTextureHandle();gs->GetTextureHandle();gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),1);
        sw->OperatePerspective(method,2,expected.get(),nullptr,tTVPRect(0,0,19,15),dst,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperatePerspective(method,2,actual.get(),nullptr,tTVPRect(0,0,19,15),dst,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();const auto reason=alpha?TVPLayerGPURejectReason::AlphaTables:TVPLayerGPURejectReason::PsTables;
        Require(after.gpuOperations==before.gpuOperations&&after.cpuFallbacks==before.cpuFallbacks+1&&
            after.gpuRejectCountByReason[int(reason)]==before.gpuRejectCountByReason[int(reason)]+1,"P2B missing tables encoded or lost rejection attribution");
        Compare(expected.get(),actual.get(),0,"P2B missing tables replay whole batch once");
    }
    device->rejectAlphaTables=device->rejectPsTables=false;
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2B perspective "<<(alpha?"alpha":"PS")<<" table provisioning failures before writes\n";
#else
    (void)backend;(void)alpha;
#endif
}

static void P2BQueuedBatches() {
    auto* sw=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    auto first=Image(19,15,4,41),second=Image(31,23,4,57),image=Image(31,23,4,13);
    Texture expected[]={Create(sw,19,15,TVPTextureFormat::RGBA,first),Create(sw,31,23,TVPTextureFormat::RGBA,second)},
        actual[]={Create(gpu,19,15,TVPTextureFormat::RGBA,first),Create(gpu,31,23,TVPTextureFormat::RGBA,second)};
    auto ss=Create(sw,31,23,TVPTextureFormat::RGBA,image),gs=Create(gpu,31,23,TVPTextureFormat::RGBA,image);
    actual[0]->GetTextureHandle();actual[1]->GetTextureHandle();gs->GetTextureHandle();
    const char* names[]={"Copy","AlphaBlend_d","ConstAlphaBlend_a","AdditiveAlphaBlend_a","PsSoftLightBlend"};
    const int opacity[]={0,1,127,128,254,255};
    const auto before=TVPGetMetalLayerRenderStats();
#ifdef TEST_NATIVE_METAL
    const auto submits=testMetalSubmits,waits=testMetalWaits;
#endif
    for(int i=0;i<20;++i) {
        int index=i%2;bool alias=i%3!=2;
        auto* method=gpu->GetRenderMethod(names[i%5]);method->SetParameterOpa(0,opacity[i%6]);
        double right=i%2?9:14,bottom=i%2?7:10;
        tTVPPointD src[]={{1,1},{right,1},{1,bottom},{right,bottom},{1,1},{right,1},{1,bottom},{right,bottom}},
            dst[]={{2.3,1.7},{16.3,3.7},{3.8,11.7},{14.8,13.7},{1.7,3.3},{17.2,1.3},{2.7,13.3},{12.9,12.3}};
        if(i%4==0) for(int j=4;j<8;++j) dst[j]=src[j];
        const tTVPRect clip=i%4<2?tTVPRect(0,0,index?31:19,index?23:15):tTVPRect(3,2,16,12);
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected[index].get():ss.get(),src),gi(alias?actual[index].get():gs.get(),src);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),i%3);
        sw->OperatePerspective(method,2,expected[index].get(),expected[1-index].get(),clip,dst,tRenderTexQuadArray(&si,1));
        gpu->OperatePerspective(method,2,actual[index].get(),actual[1-index].get(),clip,dst,tRenderTexQuadArray(&gi,1));
    }
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.gpuOperations==before.gpuOperations+20&&after.cpuFallbacks==before.cpuFallbacks&&
        after.readbackBytes==before.readbackBytes&&after.uploadedBytes==before.uploadedBytes,"P2B queued multi-quad batches transferred CPU pixels");
#ifdef TEST_NATIVE_METAL
    Require(testMetalSubmits-submits<=1&&testMetalWaits==waits,"P2B queued perspective submitted/waited per operation");
#endif
    Compare(expected[0].get(),actual[0].get(),0,"P2B pending multi-quad first target");
    Compare(expected[1].get(),actual[1].get(),0,"P2B pending multi-quad resized snapshots");
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS P2B queued batches: 20 two-quad calls, two targets, varying clip/source and mixed alias order\n";
}
