#include "CPUConsumerTrace.h"
#include <iostream>
namespace trace=krkrsdl3::cpu_consumer_trace;
namespace work=krkrsdl3::layer_work;
void Log(const char* line) {std::cout<<line<<'\n';}
int main() {
    trace::SetCallbacks(Log,nullptr);work::SetEnabled(true);
    for(unsigned i=0;i<71;++i) {
        trace::ConsumerScope scope("drawPath",trace::Access::Write,"method");
        auto r=trace::BeginRead(work::CaptureGeneration(),1);
        r.textureID=i+1;r.contentVersion=2;r.width=r.height=2;r.source="layerExDraw.write";
        r.bytes=16;r.wallNS=100;r.waitNS=50;
        auto input=trace::ReadInput(r);
        work::Record(false,r.textureID,2,2,16,100,50,false,r.source,r.epoch,false,&input);
        r.windowID=input.windowID;r.detailReserved=input.detail;r.callerReserved=input.caller;
        if(trace::ReportRead(r)) trace::ReportCaller(r,"original.ks:4");
        trace::ReportSpanRoute("drawPath","gpu","none",nullptr,1,0,64,256);
    }
    for(const auto* method:krkrsdl3::span_route::Methods) {
        if(!std::strcmp(method,"drawPath")) continue;
        trace::ConsumerScope scope(method,trace::Access::Write,"method");
        const bool clear=!std::strcmp(method,"clear");
        trace::ReportSpanRoute(method,clear?"cpu":"gpu",clear?"fullOverwrite":"none",nullptr,1,0,clear?0:64,clear?0:256);
    }
    for(int i=0;i<2;++i) {
        const auto out=work::Take();
        std::cout<<"WORK "<<out.spanRouteWindowID<<' '<<out.transferOrigins<<'\t'<<out.originOverflow<<'\n';
    }
    work::SetEnabled(false);
}
