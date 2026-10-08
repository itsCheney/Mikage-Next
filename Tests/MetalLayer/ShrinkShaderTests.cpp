#include "LayerShrinkGeometry.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "ProductionShrinkMath.inc"

void TVPTestShrinkExecute(const TVPLayerShrinkOperation& op,const uint8_t* source,int pitch,
                         std::vector<uint8_t>& output,int failureStage=-1) {
    const int w=op.destination.Width(),h=op.destination.Height();
    const bool wide=op.avgBits==64 && !TVPLayerShrinkGeometry::CanUse32(op);
    output.resize(size_t(w)*h*4);
    if(wide) {
        const shrink_shader64::ShrinkParams p{int(op.kind),w,h,op.sourceTop,op.sourceRows,op.hu,op.vu};
        const shrink_shader64::ShrinkSurface surface{source,pitch};
        std::vector<shrink_shader64::ShrinkSum> rows(size_t(w)*op.sourceRows);
        for(int y=0;y<op.sourceRows;++y) for(int x=0;x<w;++x)
            rows[size_t(y)*w+x]=shrink_shader64::shrinkHorizontal((*op.horizontal)[x],op.sourceTop+y,p,surface);
        if(failureStage==1) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            auto pixel=shrink_shader64::shrinkVertical(x,(*op.horizontal)[x],(*op.vertical)[y],p,rows.data());
            std::memcpy(output.data()+(size_t(y)*w+x)*4,&pixel,4);
        }
    } else {
        const shrink_shader32::ShrinkParams p{int(op.kind),w,h,op.sourceTop,op.sourceRows,op.hu,op.vu};
        const shrink_shader32::ShrinkSurface surface{source,pitch};
        std::vector<shrink_shader32::ShrinkSum> rows(size_t(w)*op.sourceRows);
        for(int y=0;y<op.sourceRows;++y) for(int x=0;x<w;++x)
            rows[size_t(y)*w+x]=shrink_shader32::shrinkHorizontal((*op.horizontal)[x],op.sourceTop+y,p,surface);
        if(failureStage==1) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            auto pixel=shrink_shader32::shrinkVertical(x,(*op.horizontal)[x],(*op.vertical)[y],p,rows.data());
            std::memcpy(output.data()+(size_t(y)*w+x)*4,&pixel,4);
        }
    }
}
void ShrinkShaderContractTests() {
    static_assert(int(TVPLayerShrinkKind::Area)==1 && int(TVPLayerShrinkKind::Fast)==2 && int(TVPLayerShrinkKind::Count)==3);
    auto check=[](bool ok,const char* m){if(!ok) throw std::runtime_error(m);};
    TVPLayerShrinkOperation op;
    for(auto kind:{0u,3u,4u,UINT32_MAX}) {
        op.kind=TVPLayerShrinkKind(kind);
        check(TVPLayerShrinkGeometry::Validate(op,1,1,1,1,false)==TVPLayerShrinkResult::Unsupported,"bad shrink kind accepted");
    }
    // A real unsigned-32 divisor wrap-to-zero must fail before encoding.
    op.kind=TVPLayerShrinkKind::Area;op.destination={0,0,1,1};op.sourceRows=1;
    auto axis=std::make_shared<std::vector<TVPLayerShrinkAxis>>(1);
    (*axis)[0]={1,0,256,256,0,0,65536};op.horizontal=axis;op.vertical=axis;
    check(TVPLayerShrinkGeometry::Validate(op,1,1,1,1,false)==TVPLayerShrinkResult::Arithmetic,"zero modular divisor accepted");
    std::cout<<"PASS shrink IDs and unsigned modular divisor preflight\n";
}
