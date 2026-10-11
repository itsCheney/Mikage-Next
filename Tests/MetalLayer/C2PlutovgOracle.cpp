#include <plutovg.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef TVP_PLUTOVG_CAPTURE_ORACLE
#include "SpanCapture.h"
#include "ProductionSpanMath.inc"
#endif
static void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
static constexpr int Width=1541,Height=79;
static void Fixture(plutovg_canvas_t* canvas,int scene,plutovg_surface_t* texture) {
    plutovg_canvas_set_rgba(canvas,0.71f,0.28f,0.93f,0.63f);
    plutovg_canvas_clip_rect(canvas,3.4f,2.75f,1533.3f,72.2f);
    if(scene==0) {
        for(int n=0;n<22;++n) {
            plutovg_canvas_set_rgba(canvas,n%3*0.35f,0.62f,0.2f,n%4==0?1.f:0.37f);
            plutovg_canvas_fill_rect(canvas,5.3f+n*7.2f,6.3f+n%4*5.1f,92.4f,39.2f);
        }
        plutovg_canvas_set_operator(canvas,PLUTOVG_OPERATOR_SRC);
        plutovg_canvas_set_rgba(canvas,0.3f,0.15f,0.75f,0.23f);
        plutovg_canvas_fill_rect(canvas,13.2f,11.8f,135.4f,22.7f);
        plutovg_canvas_set_operator(canvas,PLUTOVG_OPERATOR_SRC_OVER);
        plutovg_canvas_clip_rect(canvas,0,0,Width,Height); // must remain intersected
        plutovg_canvas_fill_rect(canvas,0,0,Width,Height);
    } else if(scene==1 || scene==4 || scene==5) {
        plutovg_path_t* path=plutovg_path_create();
        plutovg_path_move_to(path,-7.3f,17.7f);
        plutovg_path_cubic_to(path,400.2f,-29.1f,801.3f,97.3f,1537.2f,29.4f);
        plutovg_path_line_to(path,140.4f,63.4f);
        const float dash[]={37.3f,12.1f,24.6f,8.7f};
        {
            const int cap=scene==1 ? 0 : scene-3;
            plutovg_canvas_set_line_width(canvas,8.3f+cap);
            plutovg_canvas_set_line_cap(canvas,plutovg_line_cap_t(cap));
            plutovg_canvas_set_line_join(canvas,plutovg_line_join_t(cap));
            plutovg_canvas_set_dash(canvas,-2.6f,dash,4);
            plutovg_canvas_stroke_path(canvas,path);
        }
        plutovg_canvas_set_dash(canvas,0,nullptr,0);
        plutovg_canvas_reset_matrix(canvas);
        plutovg_canvas_fill_path(canvas,path);
        plutovg_path_destroy(path);
    } else if(scene==2) {
        const plutovg_gradient_stop_t stops[]={
            {0,{0.8f,0.1f,0.2f,0.23f}},{0.37f,{0.1f,0.7f,0.9f,0.67f}},{1,{0.4f,0.2f,0.9f,0.92f}}};
        plutovg_matrix_t matrix;plutovg_matrix_init(&matrix,0.8f,0.1f,-0.17f,1.1f,7.2f,-4.3f);
        for(int spread=0;spread<3;++spread) {
            plutovg_canvas_set_linear_gradient(canvas,23.2f,9.5f,313.1f,61.4f,
                plutovg_spread_method_t(spread),stops,3,&matrix);
            plutovg_canvas_fill_rect(canvas,4.2f,7.1f+spread*9.4f,1529.7f,36.3f);
            plutovg_canvas_set_radial_gradient(canvas,121.4f,31.2f,41.7f,109.3f,26.7f,3.6f,
                plutovg_spread_method_t(spread),stops,3,&matrix);
            plutovg_canvas_fill_rect(canvas,6.8f,11.4f,1529.9f,49.6f);
        }
    } else {
        const plutovg_matrix_t transforms[]={
            {1,0,0,1,2.2f,3.1f},{0.083f,0.001f,-0.013f,0.62f,4.7f,2.3f},
            {1,0,0,1,0,0},{0.25f,0.03f,-0.2f,0.73f,-4.4f,1.7f}};
        for(int n=0;n<4;++n) {
            plutovg_canvas_set_texture(canvas,texture,n<2?PLUTOVG_TEXTURE_TYPE_PLAIN:
                PLUTOVG_TEXTURE_TYPE_TILED,0.67f,&transforms[n]);
            plutovg_canvas_fill_rect(canvas,2.4f,5.7f+n*3.3f,1535.2f,49.7f);
        }
    }
}
#ifdef TVP_PLUTOVG_CAPTURE_ORACLE
static void Replay(const TVPLayerSpanCompositePacket& p,std::vector<uint32_t>& pixels) {
    for(int y=0;y<Height;++y) for(int x=0;x<Width;++x) {
        auto& d=pixels[size_t(y)*Width+x];
        d=span_shader::spanComposePixel(d,x,y,y,p.spans.data(),p.sourcePixels.data(),
            p.rowOffsets.data(),p.rowEntries.data());
    }
}
static bool Reject(void* closure,plutovg_span_kind_t,int,int,int,int,uint32_t,const uint32_t*) {
    ++*static_cast<int*>(closure);return false;
}
#endif
int main(int argc,char** argv) {
    try {
        std::ofstream pixelOutput;
        if(argc>1) {pixelOutput.open(argv[1],std::ios::binary);Require(bool(pixelOutput),"cannot open pixel oracle output");}
        plutovg_surface_t* texture=plutovg_surface_create(37,29);
        auto* tex=reinterpret_cast<uint32_t*>(plutovg_surface_get_data(texture));
        for(int n=0;n<37*29;++n) {uint32_t a=(n*31)%256;tex[n]=(a<<24)|((a/2)<<16)|((a/3)<<8)|(a/4);}
        uint64_t hash=1469598103934665603ull;
        for(int scene=0;scene<6;++scene) {
            std::vector<uint32_t> initial(size_t(Width)*Height),pixels;
            for(size_t n=0;n<initial.size();++n) {uint32_t a=(n*37+91)%256;initial[n]=(a<<24)|((a/2)<<16)|((a/3)<<8)|(a/4);}
            pixels=initial;
            auto* surface=plutovg_surface_create_for_data(reinterpret_cast<unsigned char*>(pixels.data()),Width,Height,Width*4);
            auto* canvas=plutovg_canvas_create(surface);Fixture(canvas,scene,texture);
#ifdef TVP_PLUTOVG_CAPTURE_ORACLE
            auto* targetless=plutovg_surface_create_for_data(nullptr,Width,Height,Width*4);
            auto* capture=plutovg_canvas_create(targetless);
            TVPPlutovgSpanCapture sink;plutovg_canvas_set_span_capture(capture,&TVPPlutovgSpanCapture::Append,&sink);
            Fixture(capture,scene,texture);
            Require(!sink.failed && !plutovg_canvas_span_capture_failed(capture),"capture failed");
            sink.packet.destination={0,0,Width,Height};
            const auto status=TVPLayerSpanCompositeGeometry::PrepareRows(sink.packet,Width,Height);
            if(status!=TVPLayerSpanCompositeResult::Applied) {
                std::vector<int> rows(Height);for(const auto& span:sink.packet.spans) if(span.y>=0 && span.y<Height)++rows[span.y];
                std::cerr<<"scene="<<scene<<" status="<<uint32_t(status)<<" spans="<<sink.packet.spans.size()
                    <<" maxRow="<<*std::max_element(rows.begin(),rows.end())<<" sourcePixels="<<sink.packet.sourcePixels.size()<<'\n';
            }
            Require(status==TVPLayerSpanCompositeResult::Applied,"CSR failed");
            auto replay=initial;Replay(sink.packet,replay);Require(replay==pixels,"patched CPU/capture exact pixels differ");
            int calls=0;plutovg_canvas_set_span_capture(capture,&Reject,&calls);
            Fixture(capture,scene,texture);Require(calls==1 && plutovg_canvas_span_capture_failed(capture),"capture abort not sticky");
            plutovg_canvas_set_span_capture(capture,nullptr,nullptr);
            auto* clone=plutovg_canvas_clone_for_surface(canvas,targetless);Require(clone,"clip state clone failed");
            Require(plutovg_canvas_rebind_surface(clone,surface),"CPU rebind failed");
            plutovg_canvas_destroy(clone);plutovg_canvas_destroy(capture);plutovg_surface_destroy(targetless);
#endif
            for(uint32_t pixel:pixels) for(int shift=0;shift<32;shift+=8) {hash^=(pixel>>shift)&255;hash*=1099511628211ull;}
            if(pixelOutput.is_open()) {
                pixelOutput.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*4));
                Require(bool(pixelOutput),"cannot write pixel oracle output");
            }
            plutovg_canvas_destroy(canvas);plutovg_surface_destroy(surface);
        }
#ifdef TVP_PLUTOVG_CAPTURE_ORACLE
        {
            // Finite but extreme paint coordinates must reject capture before
            // an unsafe float-to-int sample cast, without writing target data.
            std::vector<uint32_t> untouched(64,0x12345678u);
            auto* surface=plutovg_surface_create_for_data(reinterpret_cast<unsigned char*>(untouched.data()),8,8,32);
            auto* canvas=plutovg_canvas_create(surface);
            TVPPlutovgSpanCapture sink;
            plutovg_canvas_set_span_capture(canvas,&TVPPlutovgSpanCapture::Append,&sink);
            const plutovg_gradient_stop_t stops[]={{0,{1,0,0,1}},{1,{0,1,0,1}}};
            plutovg_canvas_set_linear_gradient(canvas,0,0,1e-20f,0,PLUTOVG_SPREAD_METHOD_REPEAT,stops,2,nullptr);
            plutovg_canvas_fill_rect(canvas,0,0,8,8);
            Require(plutovg_canvas_span_capture_failed(canvas) && !sink.failed && sink.packet.spans.empty(),
                "unsafe sampling was accepted or mislabeled as packet budget failure");
            Require(std::all_of(untouched.begin(),untouched.end(),[](uint32_t p){return p==0x12345678u;}),
                "rejected sampling changed destination pixels");
            plutovg_canvas_destroy(canvas);plutovg_surface_destroy(surface);
        }
#endif
        plutovg_surface_destroy(texture);
        std::cout<<"plutovg1.3.3 exact fixtures=6 pixels="<<size_t(Width)*Height*6<<" hash="<<hash<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
