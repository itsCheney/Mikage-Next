// This exercises actual Metal storage/pass ordering; portable State tests do
// not establish native pixel precision, validation, or performance.
void NativeLayerInitializationTests(SDL_Window* window) {
    const bool previousDiagnostics=SDL_GetHintBoolean("MIKAGE_METAL_DIAGNOSTICS",false);
    SDL_SetHint("MIKAGE_METAL_LAYER_LAZY_INIT","1");
    std::unique_ptr<iTVPRenderBackend> backend(krkrsdl3::MetalRenderBackend::Create(window,false));
    Require(backend!=nullptr,"lazy initialization backend");
    auto& gpu=*backend;
    auto* metal=dynamic_cast<krkrsdl3::MetalRenderBackend*>(&gpu);
    const bool tile=metal && metal->IsLayerTileRenderingActive();
    const auto make=[&](int w=4,int h=4,TVPLayerTextureFormat format=TVPLayerTextureFormat::RGBA8) {
        void* handle=gpu.CreateLayerTexture(w,h,format);Require(handle!=nullptr,"lazy initialization allocation");return handle;
    };
    const auto zero=[](const std::vector<uint8_t>& pixels) {
        Require(std::all_of(pixels.begin(),pixels.end(),[](uint8_t value){return value==0;}),"logical zero bytes");
    };
    std::vector<uint8_t> actual;int pitch=0;
    int renders=krkrsdl3::g_renderEncoders;
    void* unused=make();gpu.DestroyLayerTexture(unused);
    Require(krkrsdl3::g_renderEncoders==renders,"unused Layer allocation/destruction encoded a clear");
    void* source=make();void* target=make();
    Require(krkrsdl3::g_renderEncoders==renders,"Layer allocation encoded a clear");
    Require(gpu.ReadLayerTextureRegion(source,{1,1,3,3},actual,pitch) && pitch==8,"pending region read");zero(actual);
    Require(krkrsdl3::g_renderEncoders==renders+1,"pending read needs one clear");
    Require(gpu.ReadLayerTexture(source,actual,pitch),"initialized zero reread");zero(actual);
    Require(krkrsdl3::g_renderEncoders==renders+1,"zero reread cleared again");
    void* mask=make(4,4,TVPLayerTextureFormat::R8);
    Require(gpu.ReadLayerTexture(mask,actual,pitch) && pitch==4 && actual.size()==16,"pending R8 read");zero(actual);
    gpu.DestroyLayerTexture(mask);
    mask=make(4,4,TVPLayerTextureFormat::R8);
    const uint8_t maskPatch[4]={17,38,129,255};
    Require(gpu.UpdateLayerTexture(mask,maskPatch,2,{1,1,3,3}),"pending R8 partial upload");
    Require(gpu.ReadLayerTexture(mask,actual,pitch),"R8 partial upload read");
    for(int y=0;y<4;++y) for(int x=0;x<4;++x)
        Require(actual[y*4+x]==(x>=1 && x<3 && y>=1 && y<3 ? maskPatch[(y-1)*2+x-1] : 0),"R8 partial zero border");
    gpu.DestroyLayerTexture(mask);
    void* asynchronous=make();
    auto request=std::make_shared<krkrsdl3::AsyncLayerReadback>();
    const int asyncSubmits=krkrsdl3::g_metalSubmits,asyncWaits=krkrsdl3::g_syncWaits;
    Require(gpu.RequestLayerTextureRegionRead(asynchronous,{1,1,3,3},request),"pending async region read");
    Require(krkrsdl3::g_metalSubmits==asyncSubmits && krkrsdl3::g_syncWaits==asyncWaits,"pending async read added submit/wait");
    Require(gpu.ReadLayerTexture(asynchronous,actual,pitch),"async verification submission");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!request->completed.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline) SDL_Delay(1);
    Require(request->completed.load(std::memory_order_acquire) && !request->failed.load() && request->pitch==8,"pending async completion");
    zero(request->rgba);request->ReleaseStorage();gpu.DestroyLayerTexture(asynchronous);
    uint8_t patch[16];std::memset(patch,0xa7,sizeof(patch));
    Require(gpu.UpdateLayerTexture(target,patch,8,{1,1,3,3}),"pending partial upload");
    Require(gpu.ReadLayerTexture(target,actual,pitch),"partial upload read");
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) for(int c=0;c<4;++c)
        Require(actual[(y*4+x)*4+c]==(x>=1 && x<3 && y>=1 && y<3 ? 0xa7 : 0),"partial upload lost zero border");
    gpu.DestroyLayerTexture(target);target=make();
    std::vector<uint8_t> seed(64,0x39);
    renders=krkrsdl3::g_renderEncoders;
    Require(gpu.UpdateLayerTexture(target,seed.data(),16,{0,0,4,4}),"pending full upload");
    Require(gpu.ReadLayerTexture(target,actual,pitch),"full upload read");Compare(actual,seed,0);
    Require(krkrsdl3::g_renderEncoders==renders,"full upload retained pending clear");
    void* copied=make();renders=krkrsdl3::g_renderEncoders;
    Require(gpu.CopyTargetToLayerTexture(target,copied),"pending full GPU destination");
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"full GPU copy read");Compare(actual,seed,0);
    Require(krkrsdl3::g_renderEncoders==renders,"full GPU blit retained destination clear");
    gpu.DestroyLayerTexture(copied);copied=make();
    Require(gpu.CopyTargetToLayerTextureRegion(target,copied,{1,1,3,3}),"pending partial GPU destination");
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"partial GPU copy read");
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) for(int c=0;c<4;++c)
        Require(actual[(y*4+x)*4+c]==(x>=1 && x<3 && y>=1 && y<3 ? 0x39 : 0),"partial GPU copy lost zero border");
    gpu.DestroyLayerTexture(copied);copied=make();
    TVPLayerOperation copy;copy.kind=TVPLayerOperationKind::Copy;
    // First attachment consumption clears in the operation's own tile pass.
    renders=krkrsdl3::g_renderEncoders;
    Require(gpu.OperateLayerRect(copy,copied,{0,0,4,4},target,{0,0,4,4},0),"first tile copy");
    if(tile) Require(krkrsdl3::g_renderEncoders==renders+1,"first tile target needs one fused pass");
    const int beforeUnused=krkrsdl3::g_renderEncoders;
    unused=make();
    Require(gpu.OperateLayerRect(copy,copied,{0,0,2,2},target,{0,0,2,2},0),"copy after unrelated allocation");
    if(tile) Require(krkrsdl3::g_renderEncoders==beforeUnused,"unrelated pending allocation broke the active pass");
    gpu.DestroyLayerTexture(unused);
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"tile copy read");Compare(actual,seed,0);
    gpu.DestroyLayerTexture(copied);copied=make();
    Require(gpu.OperateLayerRect(copy,copied,{1,1,4,4},copied,{0,0,3,3},0),"pending aliased copy");
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"pending alias read");zero(actual);
    gpu.DestroyLayerTexture(copied);copied=make();
    void* pendingSource=make();
    Require(gpu.OperateLayerRect(copy,copied,{1,1,3,3},pendingSource,{0,0,2,2},0),"pending source consumption");
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"pending source read");zero(actual);
    gpu.DestroyLayerTexture(pendingSource);
    gpu.DestroyLayerTexture(copied);
    copied=make();pendingSource=make();
    Require(gpu.CopyTargetToLayerTexture(pendingSource,copied),"pending source full GPU copy");
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"pending source full GPU read");zero(actual);
    gpu.DestroyLayerTexture(pendingSource);gpu.DestroyLayerTexture(copied);
    // Full-fill clear conversion must reproduce all byte alphas, including RGB
    // under alpha zero. Each read closes the prior pass to exercise loadClear.
    TVPLayerOperation fill;fill.kind=TVPLayerOperationKind::Fill;
    void* filled=make(2,2);
    for(unsigned alpha=0;alpha<256;++alpha) {
        fill.color=(alpha<<24)|((255-alpha)<<16)|((alpha^0x5a)<<8)|alpha;
        renders=krkrsdl3::g_renderEncoders;
        Require(gpu.OperateLayerRect(fill,filled,{0,0,2,2},nullptr,{},0),"full fill clear");
        Require(gpu.ReadLayerTexture(filled,actual,pitch),"full fill precision read");
        for(size_t i=0;i<actual.size();++i)
            Require(actual[i]==uint8_t(fill.color>>((i%4)*8)),"full fill clear byte precision");
        if(tile) Require(krkrsdl3::g_renderEncoders==renders+1,"full fill added a separate clear pass");
    }
    fill.color=0x12345678;
    Require(gpu.OperateLayerRect(fill,filled,{0,0,2,2},nullptr,{},0),"first full fill");
    renders=krkrsdl3::g_renderEncoders;
    fill.color=0xabcdef01;
    Require(gpu.OperateLayerRect(fill,filled,{0,0,2,2},nullptr,{},0),"same-pass full fill");
    if(tile) Require(krkrsdl3::g_renderEncoders==renders,"same-target fill restarted an active pass");
    Require(gpu.ReadLayerTexture(filled,actual,pitch),"same-pass fill ordering");
    for(size_t i=0;i<actual.size();++i) Require(actual[i]==uint8_t(fill.color>>((i%4)*8)),"fill order changed");
    // Replace alpha tables while the fast Fill's pass remains open. A cached
    // encoder must bind the new immutable buffer on its next real draw.
    std::vector<uint8_t> ratios(65536,0),negative(65536,37);
    Require(gpu.SetLayerAlphaTables(ratios.data(),negative.data()),"initial alpha tables");
    fill.color=0x12345678;
    Require(gpu.OperateLayerRect(fill,filled,{0,0,2,2},nullptr,{},0),"fill before table replacement");
    renders=krkrsdl3::g_renderEncoders;
    std::fill(negative.begin(),negative.end(),219);
    Require(gpu.SetLayerAlphaTables(ratios.data(),negative.data()),"replacement alpha tables");
    TVPLayerOperation alpha;alpha.kind=TVPLayerOperationKind::Alpha;alpha.flags=TVP_LAYER_DEST_ALPHA;
    Require(gpu.OperateLayerRect(alpha,filled,{0,0,2,2},target,{0,0,2,2},0),"draw after fast fill and table replacement");
    if(tile) Require(krkrsdl3::g_renderEncoders==renders,"table refresh split the Fill pass");
    Require(gpu.ReadLayerTexture(filled,actual,pitch),"table replacement read");
    for(size_t i=0;i<actual.size();++i)
        Require(actual[i]==(i%4==3?219:uint8_t(fill.color>>((i%4)*8))),"cached encoder retained old alpha tables");
    gpu.DestroyLayerTexture(filled);
    // Cross-interface consumption also sees Layer logical zero: mesh source,
    // mesh mask, target lock, window queue and capture all share the storage.
    void* meshSource=make();void* meshTarget=make();void* meshMask=make();
    gpu.SetTarget(meshTarget);gpu.SetMask(meshMask);
    const float vertices[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    const uint16_t indices[]={0,1,2,2,1,3};
    gpu.DrawMesh(vertices,4,indices,6,meshSource,1,nullptr);
    uint8_t* locked=gpu.LockTarget(meshTarget,pitch);
    Require(locked && pitch==16,"pending mesh target lock");
    Require(std::all_of(locked,locked+64,[](uint8_t value){return value==0;}),"pending mesh source/mask bytes");
    gpu.UnlockTarget(meshTarget);gpu.SetMask(nullptr);gpu.SetTarget(nullptr);
    gpu.BeginFrame(4,4);gpu.DrawWindowTexture(meshSource,0,0,4,4);
    int captureWidth=0,captureHeight=0;
    Require(gpu.CaptureFrame(actual,captureWidth,captureHeight,pitch) && captureWidth==4 && captureHeight==4,"pending Layer window capture");
    for(size_t i=0;i<actual.size();++i) Require(actual[i]==(i%4==3 ? 255 : 0),"pending window capture bytes");
    gpu.EndFrame();gpu.DestroyLayerTexture(meshSource);gpu.DestroyLayerTexture(meshTarget);gpu.DestroyLayerTexture(meshMask);
    // Rejected and empty work cannot consume a pending clear or submit/wait.
    copied=make();renders=krkrsdl3::g_renderEncoders;
    const int submits=krkrsdl3::g_metalSubmits,waits=krkrsdl3::g_syncWaits;
    Require(!gpu.UpdateLayerTexture(copied,patch,8,{-1,0,1,2}),"invalid upload accepted");
    Require(gpu.OperateLayerRect(copy,copied,{9,9,10,10},source,{0,0,1,1},0),"empty clipped copy");
    Require(krkrsdl3::g_renderEncoders==renders && krkrsdl3::g_metalSubmits==submits &&
        krkrsdl3::g_syncWaits==waits,"rejected/empty operation materialized pending clear");
    gpu.ResetLayerGlyphResources();gpu.EndFrame();
    Require(gpu.ReadLayerTexture(copied,actual,pitch),"pending zero survived reset/submission");zero(actual);
    gpu.DestroyLayerTexture(copied);gpu.DestroyLayerTexture(source);gpu.DestroyLayerTexture(target);
    backend.reset();
    SDL_SetHint("MIKAGE_METAL_LAYER_LAZY_INIT","0");
    backend.reset(krkrsdl3::MetalRenderBackend::Create(window,false));
    Require(backend!=nullptr,"eager initialization backend");
    renders=krkrsdl3::g_renderEncoders;
    copied=backend->CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
    Require(copied && krkrsdl3::g_renderEncoders==renders+1,"hint0 did not restore eager creation");
    Require(backend->ReadLayerTexture(copied,actual,pitch),"eager zero read");zero(actual);
    backend->DestroyLayerTexture(copied);backend.reset();
    SDL_SetHint("MIKAGE_METAL_LAYER_LAZY_INIT","1");
    int baselineSubmits=-1,baselineWaits=-1;
    for(bool diagnostic:{false,true}) {
        SDL_SetHint("MIKAGE_METAL_DIAGNOSTICS",diagnostic?"1":"0");
        backend.reset(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(backend!=nullptr,"diagnostic lazy backend");
        void* first=backend->CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
        void* second=backend->CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
        const int beforeSubmits=krkrsdl3::g_metalSubmits,beforeWaits=krkrsdl3::g_syncWaits;
        Require(first && second && backend->UpdateLayerTexture(first,seed.data(),16,{0,0,4,4}),"diagnostic full upload");
        fill.color=0x98765432;
        Require(backend->OperateLayerRect(fill,second,{0,0,4,4},nullptr,{},0),"diagnostic full fill");
        Require(backend->OperateLayerRect(copy,second,{1,1,3,3},first,{0,0,2,2},0),"diagnostic partial copy");
        Require(backend->ReadLayerTexture(second,actual,pitch),"diagnostic lazy result");
        if(!diagnostic) {baselineSubmits=krkrsdl3::g_metalSubmits-beforeSubmits;baselineWaits=krkrsdl3::g_syncWaits-beforeWaits;}
        else Require(krkrsdl3::g_metalSubmits-beforeSubmits==baselineSubmits &&
            krkrsdl3::g_syncWaits-beforeWaits==baselineWaits,"P2D diagnostics added submits/waits");
        backend->DestroyLayerTexture(first);backend->DestroyLayerTexture(second);backend.reset();
    }
    SDL_SetHint("MIKAGE_METAL_DIAGNOSTICS",previousDiagnostics?"1":"0");
}
