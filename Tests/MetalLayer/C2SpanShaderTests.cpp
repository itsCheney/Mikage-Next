#include "LayerSpanCompositeGeometry.h"
#include "ProductionSpanMath.inc"
#include "Plutovg133SpanOracle.h"
#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>

void TVPTestSpanCompositeExecute(const TVPLayerSpanCompositePacket& p,
        const uint8_t* oldPixels,int pitch,std::vector<uint8_t>& output) {
    const int w=p.destination.Width(),h=p.destination.Height();
    output.resize(size_t(w)*h*4);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        uint32_t old;
        std::memcpy(&old,oldPixels+size_t(y+p.destination.top)*pitch+size_t(x+p.destination.left)*4,4);
        const uint32_t value=span_shader::spanComposePixel(old,x+p.destination.left,y+p.destination.top,uint32_t(y),
            p.spans.data(),p.sourcePixels.data(),p.rowOffsets.data(),p.rowEntries.data());
        std::memcpy(output.data()+(size_t(y)*w+x)*4,&value,4);
    }
}
namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
uint32_t oracle(uint32_t destination,uint32_t source,uint32_t coverage,uint32_t kind) {
    using namespace plutovg133_oracle;
    if(kind==uint32_t(TVPLayerSpanKind::SolidSource)) composition_solid_source(&destination,1,source,coverage);
    else if(kind==uint32_t(TVPLayerSpanKind::SolidSourceOver)) composition_solid_source_over(&destination,1,source,coverage);
    else composition_source_over(&destination,1,&source,coverage);
    return destination;
}
}
void RunC2SpanShaderTests() {
    using Kind=TVPLayerSpanKind;using Result=TVPLayerSpanCompositeResult;using Geometry=TVPLayerSpanCompositeGeometry;
    static_assert(uint32_t(Kind::Unsupported)==0 && uint32_t(Kind::SolidSource)==1 &&
        uint32_t(Kind::SolidSourceOver)==2 && uint32_t(Kind::ArraySourceOver)==3 && uint32_t(Kind::Count)==4);
    // All coverage/alpha pairs with arbitrary packed data, not only valid
    // premultiplied colors: packed carries/wrap must agree with original C.
    std::mt19937 rng(0xc2b133);
    for(uint32_t coverage=0;coverage<256;++coverage) for(uint32_t alpha=0;alpha<256;++alpha)
        for(uint32_t n=0;n<12;++n) {
            const uint32_t source=(uint32_t(rng())&0xffffffu)|(alpha<<24);
            const uint32_t old=uint32_t(rng());
            for(uint32_t kind=1;kind<4;++kind)
                check(span_shader::spanComposite(old,source,coverage,kind)==oracle(old,source,coverage,kind),
                    "span exact plutovg original-C oracle mismatch");
        }
    for(uint32_t source:{0u,0xffffffffu,0xff000000u,0x00ffffffu,0x01000000u,0x7fffffffu})
        for(uint32_t old:{0u,0xffffffffu,0x12345678u}) for(uint32_t coverage:{0u,1u,127u,254u,255u})
            for(uint32_t kind=1;kind<4;++kind)
                check(span_shader::spanComposite(old,source,coverage,kind)==oracle(old,source,coverage,kind),"span extreme fast branch mismatch");
    TVPLayerSpanCompositePacket p;p.destination={2,1,10,4};
    p.sourcePixels={0,0xffffffffu,0x20304050u,0xff010203u,0x80102030u,0x01020304u};
    // Deliberately interleaved rows and overlaps. CSR must retain call order.
    p.spans={{3,2,4,255,uint32_t(Kind::SolidSource),0x40506070u,0,0},
             {2,1,6,255,uint32_t(Kind::ArraySourceOver),0,0,0},
             {4,2,3,111,uint32_t(Kind::SolidSourceOver),0x90608040u,0,0},
             {4,1,4,173,uint32_t(Kind::SolidSource),0xc0908070u,0,0},
             {5,2,2,254,uint32_t(Kind::ArraySourceOver),0,3,0}};
    check(Geometry::PrepareRows(p,12,5)==Result::Applied,"span CSR prepare");
    check(p.rowOffsets==std::vector<uint32_t>({0,2,5,5}) && p.rowEntries==std::vector<uint32_t>({1,3,0,2,4}),"span stable CSR order");
    std::vector<uint32_t> old(60),expected;
    for(auto& pixel:old) pixel=uint32_t(rng());
    expected=old;
    for(const auto& s:p.spans) for(uint32_t n=0;n<s.length;++n) {
        auto& pixel=expected[size_t(s.y)*12+s.x+n];
        const uint32_t source=s.kind==uint32_t(Kind::ArraySourceOver)?p.sourcePixels[s.sourceOffset+n]:s.solid;
        pixel=oracle(pixel,source,s.coverage,s.kind);
    }
    std::vector<uint8_t> output;TVPTestSpanCompositeExecute(p,reinterpret_cast<const uint8_t*>(old.data()),48,output);
    for(int y=1;y<4;++y) for(int x=2;x<10;++x) {
        uint32_t actual;std::memcpy(&actual,output.data()+((y-1)*8+x-2)*4,4);
        check(actual==expected[size_t(y)*12+x],"span overlap ordering or untouched ROI holes");
    }
    auto bad=p;
    for(uint32_t kind:{0u,4u,5u,UINT32_MAX}) {bad=p;bad.spans[0].kind=kind;check(Geometry::Validate(bad,12,5)==Result::Unsupported,"invalid kind accepted");}
    bad=p;bad.spans[0].length=UINT32_MAX;check(Geometry::Validate(bad,12,5)==Result::Geometry,"span length overflow");
    bad=p;bad.spans[1].sourceOffset=UINT32_MAX;check(Geometry::Validate(bad,12,5)==Result::Resource,"span source offset overflow");
    bad=p;bad.spans[0].coverage=256;check(Geometry::Validate(bad,12,5)==Result::Geometry,"invalid span coverage");
    bad=p;bad.spans[0].reserved=1;check(Geometry::Validate(bad,12,5)==Result::Geometry,"unknown reserved span flag");
    bad=p;bad.spans[0].y=0;check(Geometry::Validate(bad,12,5)==Result::Geometry,"span row outside ROI");
    bad=p;bad.rowEntries[2]=4;check(Geometry::Validate(bad,12,5)==Result::Geometry,"reordered row refs accepted");
    bad=p;bad.rowEntries[0]=0;check(Geometry::Validate(bad,12,5)==Result::Geometry,"wrong row refs accepted");
    bad=p;bad.rowOffsets[1]=UINT32_MAX;check(Geometry::Validate(bad,12,5)==Result::Geometry,"CSR offset overflow");
    bad=p;bad.destination.right=INT32_MAX;bad.destination.bottom=INT32_MAX;
    check(Geometry::Validate(bad,UINT32_MAX,UINT32_MAX)==Result::ParameterBudget,"scratch size overflow");
    bad={};bad.destination={0,0,1,1};bad.spans.assign(513,{0,0,1,255,uint32_t(Kind::SolidSource),0,0,0});
    check(Geometry::PrepareRows(bad,1,1)==Result::ParameterBudget,"row ref budget missing");
    bad.spans.resize(512);check(Geometry::PrepareRows(bad,1,1)==Result::Applied,"512 row ref boundary");
    bad={};bad.destination={0,0,4096,4096};check(Geometry::PrepareRows(bad,4096,4096)==Result::ParameterBudget,"joint scratch budget missing");
    bad={};bad.destination={0,0,0,0};check(Geometry::PrepareRows(bad,1,1)==Result::Applied,"empty span no-op validation");
    check(Geometry::StagedBudgetFits(Geometry::ParameterBudget-24,0),"actual staging budget exact boundary");
    check(!Geometry::StagedBudgetFits(Geometry::ParameterBudget-23,0),"actual staging allocation omitted uniforms");
    check(Geometry::StagedBudgetFits(1024,(Geometry::ParameterBudget-24-1024)/2),"actual staging and two scratch buffers exact boundary");
    check(!Geometry::StagedBudgetFits(1024,(Geometry::ParameterBudget-24-1024)/2+1),"actual staging and scratch budget exceeded");
    check(!Geometry::StagedBudgetFits(UINT64_MAX,UINT64_MAX),"actual staging budget arithmetic overflow");
    // A small logical packet fitting the geometry budget can borrow a large
    // pooled allocation; reject it before any encoder sees the parameter data.
    check(Geometry::StagedBudgetFits(1024,20ull*1024*1024) &&
          !Geometry::StagedBudgetFits(32ull*1024*1024,20ull*1024*1024),"oversized pooled staging allocation accepted");
    std::cout<<"PASS C2B production integer MSL vs original plutovg1.3.3 C, ordered overlaps, CSR and checked budgets\n";
}
