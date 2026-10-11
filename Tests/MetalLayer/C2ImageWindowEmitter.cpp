#include "CPUConsumerTrace.h"
#include <iostream>
namespace trace=krkrsdl3::cpu_consumer_trace;
namespace work=krkrsdl3::layer_work;
namespace image=krkrsdl3::layer_image;
void ImageEmitterLog(const char* message) {std::cout<<message<<'\n';}
int main() {
    trace::SetCallbacks(ImageEmitterLog,nullptr);work::SetEnabled(true);
    image::Context constructed;constructed.method="modulate";constructed.stage="construct";
    constructed.route="metadata";constructed.reason="metadataOnly";constructed.metrics.calls=1;
    trace::BeginImage(constructed);trace::RecordImage(constructed);
    for(int i=0;i<71;++i) {
        image::Context c;c.method="modulate";c.metrics.calls=1;std::strcpy(c.parameters,"[30,17,-9]");
        trace::BeginImage(c);image::context=&c;
        trace::ConsumerScope scope("LayerExImage.modulate",trace::Access::Write,"native.image");
        auto r=trace::BeginRead(c.epoch,1);r.source="layerExImage.write";r.width=r.height=32;
        r.textureID=i+1;r.bytes=4096;r.wallNS=100;r.waitNS=90;
        auto input=trace::ReadInput(r);
        work::Record(false,r.textureID,32,32,r.bytes,r.wallNS,r.waitNS,false,r.source,r.epoch,false,&input);
        r.windowID=input.windowID;r.detailReserved=input.detail;r.callerReserved=input.caller;
        if(trace::ReportRead(r)) trace::ReportCaller(r,"original.tjs:243");
        trace::RecordImage(c);image::context=nullptr;
    }
    image::Context gpu;gpu.method="light";gpu.route="gpu";gpu.reason="applied";
    gpu.metrics.calls=1;gpu.metrics.parameterBytes=768;std::strcpy(gpu.parameters,"[13,0]");
    trace::BeginImage(gpu);trace::RecordImage(gpu);
    for(int i=0;i<2;++i) {
        const auto out=work::Take();
        std::cout<<"WORK "<<out.spanRouteWindowID<<' '<<out.transferOrigins<<'\t'<<out.originOverflow<<'\n';
    }
}
