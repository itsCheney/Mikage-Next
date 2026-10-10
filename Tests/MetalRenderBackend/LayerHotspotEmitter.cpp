#include "backend/LayerHotspotDiagnostics.h"
#include "backend/LayerInitialization.h"
#include <cstdio>
#include <stdexcept>
using namespace krkrsdl3::layer_hotspot;
namespace layer_hotspot=krkrsdl3::layer_hotspot;
namespace layer_initialization=krkrsdl3::layer_initialization;
struct ProductionTrace {
    struct Resource {int width=0,height=0,bytesPerPixel=4;ResourceInfo diagnostic;};
    Recorder hotspots;uint64_t diagnosticResourceSerial=0,diagnosticEncoderSerial=8;
    #include "ProductionHotspotTrace.inc"
};
int main() {
    ProductionTrace production;
    production.hotspots.Enable(true);production.hotspots.Begin(1,1,true,16,0,0);
    ProductionTrace::Resource actualTarget,actualSource;
    actualTarget.width=actualSource.width=4;actualTarget.height=actualSource.height=4;
    actualTarget.diagnostic.identity=CreatedIdentity(44,45);
    actualSource.diagnostic.identity=CreatedIdentity(44,46);
    production.Trace(Access::Rect,1,&actualTarget,&actualSource,{0,0,4,4},{0,0,2,2},{1,1,4,4});
    auto actual=production.hotspots.Freeze(1);
    if(actual->totals.calls!=1 || actual->totals.pixels!=9 || actual->totals.scaledPixels!=9 ||
       actual->resources[0].generation==actual->resources[1].generation ||
       actual->operations[0].encoder!=8 || actual->operations[0].full || actual->operations[0].readsTarget)
        throw std::runtime_error("production trace resource/clip/traits");
    Recorder recorder;OutputGate gate;
    const auto log=[](const char* text){std::puts(text);};
    recorder.Begin(1,1,true,16,0,0);
    if(recorder.Freeze(1))throw std::runtime_error("disabled sample");
    recorder.Enable(true);
    ResourceInfo target,source;target.generation=10;source.generation=11;
    target.width=source.width=4;target.height=source.height=4;
    target.identity=CreatedIdentity(2,20);source.identity=CreatedIdentity(2,21);
    SetAsset(source.identity,"archive/街_通学路a.png");
    recorder.Begin(1,1,true,16,9,34);
    {
        Scope context(42,"main");
        recorder.Pass(0,&target,true,ClearOrigin::FusedInitialization,1);
        recorder.Record(Access::Rect,1,&target,&source,{0,0,4,4},{0,0,4,4},{0,0,4,4},16,
                        false,false,false,true,1,1);
        recorder.End(Boundary::Submit,1);
    }
    auto sample=recorder.Freeze(2);gate.Emit(1000000000,FormatSample(*sample,20,true,0,0),log);
    gate.Report(recorder,1000000000,1,false,log);
    // Non-sampled command still contributes exactly to the cumulative totals.
    recorder.Begin(2,3,false,16,0,0);
    recorder.Record(Access::Rect,5,&target,nullptr,{0,0,4,4},{},{0,0,4,4},16);
    gate.Report(recorder,3000000000,2,true,log);
    // Overflow preserves full totals, the formatter is bounded, and completion
    // can safely use the frozen values after recorder/resources have changed.
    Recorder overflowRecorder;overflowRecorder.Enable(true);overflowRecorder.Begin(3,4,true,16,0,0);
    for(int n=0;n<400;++n)overflowRecorder.Record(Access::Rect,1,&target,&source,{0,0,4,4},{0,0,4,4},{0,0,4,4},16);
    auto overflow=overflowRecorder.Freeze(4);
    if(overflow->operationOverflow!=144 || overflow->totals.calls!=400)
        throw std::runtime_error("overflow lost aggregate");
    auto lines=FormatSample(*overflow,30,true,12,0);size_t bytes=0;
    for(const auto& line:lines){if(line.size()>MessageBytes)throw std::runtime_error("message budget");bytes+=line.size()+1;}
    if(bytes>OutputBytes)throw std::runtime_error("sample budget");
    gate.Emit(5000000000,lines,log);
    recorder.Enable(false);gate.Report(recorder,7000000000,3,true,log);
    // A strict sliding window also charges emissions just before a second
    // boundary; it cannot emit two complete budgets a nanosecond apart.
    OutputGate bounded;std::vector<std::string> batch(30,std::string(900,'x'));size_t emitted=0;
    const auto count=[&](const char*){++emitted;};
    bounded.Emit(999999999,batch,count);bounded.Emit(1000000000,batch,count);
    if(emitted!=30 || bounded.droppedReports!=1)throw std::runtime_error("sliding output budget");
    bounded.Emit(1999999999,batch,count);
    if(emitted!=60)throw std::runtime_error("output budget expiration");
}
