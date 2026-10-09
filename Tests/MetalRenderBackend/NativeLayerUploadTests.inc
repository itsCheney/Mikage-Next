// Native tests intentionally compare the new path to the existing integer
// operator, including every staged source version, not a duplicated formula.
void TinyUploadBatchTests(iTVPRenderBackend& gpu) {
    SDL_SetHint("MIKAGE_METAL_TINY_UPLOAD_BATCHING","1");
    void* textures[2]={gpu.CreateLayerTexture(32,32,TVPLayerTextureFormat::RGBA8),
                       gpu.CreateLayerTexture(32,32,TVPLayerTextureFormat::RGBA8)};
    Require(textures[0] && textures[1],"tiny resources");
    std::vector<uint8_t> expected[2]={std::vector<uint8_t>(32*32*4),std::vector<uint8_t>(32*32*4)};
    std::vector<uint8_t> discarded;int pitch=0;
    Require(gpu.ReadLayerTexture(textures[0],discarded,pitch),"prime upload boundary");
    const int blits=krkrsdl3::g_blitEncoders,submits=krkrsdl3::g_metalSubmits,waits=krkrsdl3::g_syncWaits;
    for(int i=0;i<32;++i) {
        const int index=i%2,x=(i*3)%28,y=(i*5)%29;
        uint8_t pixels[60];std::memset(pixels,i+1,sizeof(pixels));
        Require(gpu.UpdateLayerTexture(textures[index],pixels,20,{x,y,x+4,y+3}),"tiny update");
        for(int row=0;row<3;++row) std::memcpy(expected[index].data()+((y+row)*32+x)*4,pixels+row*20,16);
        std::memset(pixels,0xee,sizeof(pixels)); // borrowed input ends at return
    }
    Require(krkrsdl3::g_blitEncoders==blits+1,"32 consecutive updates need one upload encoder");
    Require(krkrsdl3::g_metalSubmits==submits && krkrsdl3::g_syncWaits==waits,"tiny batch added submit/wait");
    for(int i=0;i<2;++i) {
        Require(gpu.ReadLayerTexture(textures[i],discarded,pitch),"tiny read");Compare(discarded,expected[i],0);
        gpu.DestroyLayerTexture(textures[i]);
    }
    // A consumer between updates must observe the earlier bytes. This cannot
    // be implemented by keeping only the last dirty rectangle/source pointer.
    void* source=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::RGBA8);
    void* target=gpu.CreateLayerTexture(8,4,TVPLayerTextureFormat::RGBA8);
    std::vector<uint8_t> red(64,0x32),blue(64,0xc4);
    TVPLayerOperation copy;copy.kind=TVPLayerOperationKind::Copy;
    Require(gpu.UpdateLayerTexture(source,red.data(),16,{0,0,4,4}),"first version");
    Require(gpu.OperateLayerRect(copy,target,{0,0,4,4},source,{0,0,4,4},0),"first consumer");
    Require(gpu.UpdateLayerTexture(source,blue.data(),16,{0,0,4,4}),"second version");
    Require(gpu.OperateLayerRect(copy,target,{4,0,8,4},source,{0,0,4,4},0),"second consumer");
    Require(gpu.ReadLayerTexture(target,discarded,pitch),"intermediate versions read");
    for(int y=0;y<4;++y) for(int x=0;x<8;++x) for(int c=0;c<4;++c)
        Require(discarded[(y*8+x)*4+c]==(x<4 ? 0x32 : 0xc4),"upload crossed a consumer boundary");
    gpu.DestroyLayerTexture(source);gpu.DestroyLayerTexture(target);
}
void GlyphAtlasNativeTests(iTVPRenderBackend& gpu) {
    SDL_SetHint("MIKAGE_METAL_GLYPH_ATLAS","1");
    std::vector<uint8_t> tables(65536,0);Require(gpu.SetLayerAlphaTables(tables.data(),tables.data()),"glyph tables");
    void* target=gpu.CreateLayerTexture(64,64,TVPLayerTextureFormat::RGBA8);
    void* reference=gpu.CreateLayerTexture(64,64,TVPLayerTextureFormat::RGBA8);
    void* scratch=gpu.CreateLayerTexture(4,4,TVPLayerTextureFormat::R8);
    Require(target && reference && scratch,"glyph resources");
    TVPLayerOperation op;op.kind=TVPLayerOperationKind::ColorMap;op.opacity=191;
    op.color=0xff3579bd;op.flags=TVP_LAYER_HOLD_ALPHA;
    uint8_t pixels[16];std::memset(pixels,32,sizeof(pixels));
    krkrsdl3::TVPLayerGlyphUploadInfo info;
    const bool available=gpu.OperateLayerGlyph(op,target,{0,0,4,4},pixels,4,4,4,0,info);
    if(available) {
        std::vector<uint8_t> discarded;int pitch=0;
        Require(gpu.ReadLayerTexture(target,discarded,pitch),"glyph prime");
        const int blits=krkrsdl3::g_blitEncoders,renders=krkrsdl3::g_renderEncoders;
        const int submits=krkrsdl3::g_metalSubmits,waits=krkrsdl3::g_syncWaits;
        for(int i=0;i<128;++i) {
            std::memset(pixels,(i*7)%65,sizeof(pixels));const int x=(i%16)*4,y=((i/16)%16)*4;
            Require(gpu.OperateLayerGlyph(op,target,{x,y,x+4,y+4},pixels,4,4,4,0,info),"glyph atlas draw");
            Require(info.textureID && info.bytes==16 && info.targetWritten,"glyph source/accounting");
            std::memset(pixels,0xff,sizeof(pixels));
        }
        const int fastBlits=krkrsdl3::g_blitEncoders-blits;
        auto* metal=dynamic_cast<krkrsdl3::MetalRenderBackend*>(&gpu);
        if(metal && metal->IsLayerTileRenderingActive()) {
            Require(fastBlits==0,"tile glyphs must not blit");
            Require(krkrsdl3::g_renderEncoders==renders+1,"128 glyphs need one ordinary pass");
        }
        Require(krkrsdl3::g_metalSubmits==submits && krkrsdl3::g_syncWaits==waits,"glyphs added submit/wait");
        std::vector<uint8_t> actual;Require(gpu.ReadLayerTexture(target,actual,pitch),"atlas result");
        std::memset(pixels,32,sizeof(pixels));
        Require(gpu.UpdateLayerTexture(scratch,pixels,4,{0,0,4,4}) &&
            gpu.OperateLayerRect(op,reference,{0,0,4,4},scratch,{0,0,4,4},0),"legacy prime");
        const int referenceBlits=krkrsdl3::g_blitEncoders;
        for(int i=0;i<128;++i) {
            std::memset(pixels,(i*7)%65,sizeof(pixels));const int x=(i%16)*4,y=((i/16)%16)*4;
            Require(gpu.UpdateLayerTexture(scratch,pixels,4,{0,0,4,4}) &&
                gpu.OperateLayerRect(op,reference,{x,y,x+4,y+4},scratch,{0,0,4,4},0),"legacy glyph oracle");
        }
        // Snapshot-compute devices legitimately retain target snapshot blits;
        // compare like-for-like operator costs and remove only upload blits.
        Require(krkrsdl3::g_blitEncoders-referenceBlits==fastBlits+128,"glyph uploads still opened blit encoders");
        Require(gpu.ReadLayerTexture(reference,discarded,pitch),"legacy glyph result");Compare(actual,discarded,0);
        // Retirement/reset must also work while the last command is unsubmitted.
        Require(gpu.OperateLayerGlyph(op,target,{8,8,12,12},pixels,4,4,4,0,info),"pre-reset glyph");
        gpu.ResetLayerGlyphResources();
        Require(gpu.ReadLayerTexture(target,discarded,pitch),"reset dropped encoded glyph resources");
        // Reset cannot erase the physical budget while the same unsubmitted
        // command still retains old pages. Repeated session resets must hit the
        // cap, then recover after completion without forcing an early Submit.
        const int resetSubmits=krkrsdl3::g_metalSubmits;
        int accepted=0;
        for(int i=0;i<17;++i) {
            if(!gpu.OperateLayerGlyph(op,target,{8,8,12,12},pixels,4,4,4,0,info)) {
                Require(std::strcmp(info.reason,"capacity")==0 && !info.targetWritten,"reset budget decline");
                break;
            }
            ++accepted;gpu.ResetLayerGlyphResources();
        }
        Require(accepted>0 && accepted<=16,"unsubmitted reset pages escaped the atlas budget");
        Require(krkrsdl3::g_metalSubmits==resetSubmits,"reset forced a Submit");
        Require(gpu.ReadLayerTexture(target,discarded,pitch),"reset retirement flush");
        Require(gpu.OperateLayerGlyph(op,target,{8,8,12,12},pixels,4,4,4,0,info),"completed reset pages did not retire");
    } else Require(std::strcmp(info.reason,"sharedUnavailable")==0,"unexpected atlas decline on native device");
    op.opacity=-1;
    Require(!gpu.OperateLayerGlyph(op,target,{0,0,4,4},pixels,4,4,4,0,info) && !info.targetWritten,"invalid glyph touched target");
    gpu.DestroyLayerTexture(scratch);gpu.DestroyLayerTexture(reference);gpu.DestroyLayerTexture(target);
}
