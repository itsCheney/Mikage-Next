#include "CPUConsumerTrace.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace trace=krkrsdl3::cpu_consumer_trace;
namespace work=krkrsdl3::layer_work;
std::vector<std::string> messages;
unsigned producerCaptures=0;
void Require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
void Capture(const char* value) {messages.emplace_back(value);}
void ThrowingLog(const char*) {throw std::runtime_error("diagnostic callback");}
bool ThrowingProducer(void*,trace::Producer&) {throw std::runtime_error("diagnostic resource capture");}
bool Producer(void* resource,trace::Producer& p) {
    ++producerCaptures;
    if(!resource) return false;
    p={77,123,5,17,13};return true;
}
void Begin() {work::SetEnabled(true);messages.clear();producerCaptures=0;}
trace::Read Read(uint64_t session=77) {
    auto read=trace::BeginRead(work::CaptureGeneration(),session);
    read.textureID=123;read.contentVersion=5;read.width=17;read.height=13;
    read.source="layerExDraw.write";read.lastWriter="Layer.shrinkCopy";
    read.lastWrite[0]=1;read.lastWrite[1]=2;read.lastWrite[2]=12;read.lastWrite[3]=11;
    read.bytes=884;read.wallNS=91;read.waitNS=63;return read;
}
void Report(const trace::Read& read,const char* stack="script.ks:4") {
    if(trace::ReportRead(read)) trace::ReportCaller(read,stack);
}
size_t Count(const char* fragment) {
    size_t result=0;for(const auto& message:messages) if(message.find(fragment)!=std::string::npos) ++result;return result;
}
void ContextAndAttribution() {
    Begin();
    Require(trace::Current()==nullptr,"C2 initial context leaked");
    {
        trace::ConsumerScope outer("drawLine",trace::Access::Write,"method",91);
        const auto* first=trace::Current();const auto outerID=first->traceID;
        {
            trace::ConsumerScope bridge("Layer.mainImageBufferForWrite",trace::Access::Read,"native.get",0,true);
            Require(trace::Current()->traceID==outerID && trace::Current()->owner==91 &&
                trace::Current()->access==trace::Access::Write,"C2 bridge lost outer consumer context");
            work::SourceScope c0("layerExBase.write");
            Report(Read());
            Require(std::string(work::source)=="layerExBase.write","C2 context mutated C0 transfer origin");
        }
        try {
            trace::ConsumerScope inner("getRecordImage",trace::Access::Read,"method",92);
            Require(trace::Current()->traceID!=outerID,"C2 inner operation reused trace identity");
            throw 1;
        } catch(int) {}
        Require(trace::Current()==first && trace::Current()->traceID==outerID,"C2 throw did not restore outer scope");
    }
    Require(trace::Current()==nullptr,"C2 scope retained consumer pointer");
    Require(messages.size()==2 && messages[0].find("\"method\":\"drawLine\",\"access\":\"write\"")!=std::string::npos &&
        messages[0].find("\"source\":\"layerExDraw.write\"")!=std::string::npos &&
        messages[0].find("\"lastSubmittedID\":null,\"renderFrame\":null")!=std::string::npos,
        "C2 consumer source or unknown GPU metadata wire contract failed");
    Require(messages[1].find("\"positions\":\"unverified\"")!=std::string::npos,"C2 caller overstated source positions");
    const auto sample=work::Take();
    Require(sample.transfers.empty(),"C2 native detail duplicated transfer totals");
}
void WindowBudgets() {
    Begin();
    {
        trace::ConsumerScope scope("Layer.shrinkCopy",trace::Access::Write,"native.shrinkCopy");
        for(unsigned i=0;i<35;++i) {
            Report(Read());trace::ReportShrinkOutput(reinterpret_cast<void*>(uintptr_t(1)),1,2,12,11);
        }
    }
    Require(Count("\"phase\":\"read\"")==32 && Count("\"phase\":\"caller\"")==8 &&
        Count("metal.cpuProducer ")==32,"C2 native per-window budgets not bounded");
    work::Take();
    Require(messages.back().find("\"readRecords\":32,\"readExceeded\":3,\"callerRecords\":8,\"callerExceeded\":24")!=std::string::npos &&
        messages.back().find("\"producerRecords\":32,\"producerExceeded\":3,\"oversizeRecords\":0")!=std::string::npos,
        "C2 window take did not disclose budget losses");
    messages.clear();
    Report(Read());work::Take();
    Require(messages.size()==3 && messages.back().find("\"readRecords\":1,\"readExceeded\":0,\"callerRecords\":1")!=std::string::npos,
        "C2 window take did not reset record and caller budgets");
    messages.clear();work::Take();Require(messages.empty(),"C2 empty take replayed budget diagnostics");
}
void EpochsAndDisabled() {
    Begin();const auto stale=Read();
    {
        trace::ConsumerScope old("old",trace::Access::Read,"method");
        work::SetEnabled(false);work::SetEnabled(true);
        Require(trace::BeginRead(work::CaptureGeneration(),77).epoch==0,"C2 toggled old consumer survived new generation");
        Report(stale);trace::ReportCaller(stale,"obsolete frame");
        trace::ReportShrinkOutput(reinterpret_cast<void*>(uintptr_t(1)),0,0,1,1);
    }
    Require(messages.empty() && producerCaptures==0,"C2 stale generation emitted native diagnostics or queried resource");
    const auto current=Read(88);Report(current,nullptr);
    Require(messages[0].find("\"sessionID\":88")!=std::string::npos &&
        messages[1].find("\"traceState\":\"unavailable\",\"positions\":\"unavailable\",\"trace\":\"\"")!=std::string::npos,
        "C2 session identity/unavailable caller contract failed");
    work::SetEnabled(false);messages.clear();
    {
        trace::ConsumerScope off("disabled",trace::Access::Write,"method");
        Report(Read());trace::ReportShrinkOutput(reinterpret_cast<void*>(uintptr_t(1)),0,0,1,1);
        work::SetEnabled(true);
        Require(trace::BeginRead(work::CaptureGeneration(),99).epoch==0,"C2 disabled scope attributed an enable inside the call");
    }
    Require(messages.empty() && producerCaptures==0,"C2 disabled instrumentation emitted or queried resources");
    Report(Read());work::Take();
    Require(messages.back().find("\"readRecords\":1")!=std::string::npos,"C2 reset retained previous budget");
}
void UTF8AndWireLimits() {
    const std::string three="\xe4\xb8\xad",four="\xf0\x9f\x98\x80";
    Require(trace::BoundedUTF8((std::string(511,'x')+three).c_str(),512,512)==std::string(511,'x'),
        "C2 UTF8 truncation split three-byte code point");
    Require(trace::BoundedUTF8((std::string(508,'x')+four+"y").c_str(),512,512)==std::string(508,'x')+four,
        "C2 UTF8 truncation dropped complete boundary code point");
    Require(trace::BoundedUTF8((std::string(509,'x')+four).c_str(),512,512)==std::string(509,'x'),
        "C2 UTF8 truncation split four-byte code point");
    Require(trace::BoundedUTF8("\xe4\xb8",512,512).empty() && trace::BoundedUTF8("\xc0\xaf",512,512).empty() &&
        trace::BoundedUTF8("\xed\xa0\x80",512,512).empty(),"C2 incomplete or invalid UTF8 was emitted");
    Require(trace::BoundedUTF8(std::string(512,'\n').c_str(),512,512).size()==85,"C2 JSON escaped trace exceeded escaped byte budget");
    Begin();
    auto read=Read();
    read.readID=read.consumer.traceID=read.epoch=read.sessionID=read.textureID=read.contentVersion=
        read.consumer.owner=read.bytes=read.wallNS=read.waitNS=std::numeric_limits<uint64_t>::max();
    read.epoch=work::CaptureGeneration();
    read.width=read.height=std::numeric_limits<int>::min();
    for(auto& value:read.lastWrite) value=std::numeric_limits<int>::min();
    Report(read,(std::string(508,'x')+four).c_str());
    Require(messages.size()==2,"C2 max-width IDs or counters lost a read/caller record");
    Require(messages[1].find(four)!=std::string::npos,"C2 caller UTF8 was not preserved");
    for(const auto& message:messages) Require(message.size()<=900,"C2 native message exceeded 900 UTF8 bytes");
    Report(read,std::string(512,'\n').c_str());
    for(const auto& message:messages) Require(message.size()<=900,"C2 escaped caller exceeded 900-byte native limit");
    const auto before=messages.size();
    Require(!trace::Emit(read.epoch,std::string(901,'x')) && messages.size()==before,"C2 oversize native buffer emitted");
    work::Take();Require(messages.back().find("\"oversizeRecords\":1")!=std::string::npos,"C2 oversize native buffer not disclosed");
}
void ProducerSafety() {
    Begin();
    {
        trace::ConsumerScope shrink("Layer.shrinkCopy",trace::Access::Write,"native.shrinkCopy");
        Require(!trace::Current()->shrinkSucceeded,"C2 shrink producer assumed computation success");
        trace::MarkShrinkSuccess();Require(trace::Current()->shrinkSucceeded,"C2 shrink success marker lost");
        trace::ReportShrinkOutput(nullptr,1,2,3,4);
        Require(messages.empty(),"C2 failed producer capture fabricated output identity");
        trace::ReportShrinkOutput(reinterpret_cast<void*>(uintptr_t(1)),1,2,3,4);
        Require(messages.size()==1 && messages[0].find("\"textureID\":123,\"contentVersion\":5")!=std::string::npos &&
            messages[0].find("\"outputROI\":[1,2,3,4]")!=std::string::npos,"C2 producer key/ROI contract failed");
    }
    const auto profile=work::Take();
    Require(profile.transfers.empty() && profile.shrinkProfiles=="[]","C2 producer changed work/transfer counters");
}
void RejectedNamesAndCallbackErrors() {
    Begin();
    const auto tooLong=std::string(49,'x');
    auto read=Read();read.source=tooLong.c_str();Report(read);
    Require(messages.empty(),"C2 oversized origin was shortened to a false C0 identity");
    {
        trace::ConsumerScope scope(tooLong.c_str(),trace::Access::Write,"method");
        trace::ReportShrinkOutput(reinterpret_cast<void*>(uintptr_t(1)),1,2,3,4);
    }
    Require(messages.empty(),"C2 oversized producer method was shortened to a false identity");
    work::Take();
    Require(messages.back().find("\"callerRecords\":0")!=std::string::npos &&
        messages.back().find("\"oversizeRecords\":2")!=std::string::npos,
        "C2 rejected names consumed caller budget or failed to disclose rejection");
    messages.clear();
    trace::SetCallbacks(ThrowingLog,ThrowingProducer);
    {
        trace::ConsumerScope scope("Layer.shrinkCopy",trace::Access::Write,"method");
        Report(Read());trace::ReportCaller(Read(),"frame");
        trace::ReportShrinkOutput(reinterpret_cast<void*>(uintptr_t(1)),1,2,3,4);
    }
    const auto profile=work::Take();
    Require(profile.transfers.empty() && messages.empty(),"C2 callback failure changed transfer totals");
    trace::SetCallbacks(Capture,Producer);
}
void SpanRoutes() {
    Begin();
    {
        trace::ConsumerScope scope("drawLine",trace::Access::Write,"method",5);
        for(unsigned i=0;i<35;++i) trace::ReportSpanRoute("drawLine","gpu","applied",
            reinterpret_cast<void*>(uintptr_t(1)),4,16,188,1024);
    }
    Require(Count("metal.layerSpan ")==32 && producerCaptures==32,"C2B route budget queried or emitted excess records");
    Require(messages[0].find("\"route\":\"gpu\",\"reason\":\"applied\"")!=std::string::npos &&
            messages[0].find("\"sourceBytes\":16,\"parameterBytes\":188,\"scratchBytes\":1024")!=std::string::npos,
            "C2B route and separate packet metrics missing");
    const auto profile=work::Take();
    Require(profile.transfers.empty() && messages.back().find("\"spanRouteRecords\":32,\"spanRouteExceeded\":3")!=std::string::npos,
        "C2B routes changed C0 totals or hid sampling loss");
    messages.clear();
    {
        trace::ConsumerScope scope("drawPath",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawPath","cpu","record",nullptr,0,0,0,0);
        Require(messages.size()==1 && messages[0].find("\"sessionID\":null,\"textureID\":null,\"contentVersion\":null")!=std::string::npos,
            "C2B unavailable CPU target identity fabricated zeros");
        work::SetEnabled(false);work::SetEnabled(true);messages.clear();
        trace::ReportSpanRoute("drawPath","gpu","applied",reinterpret_cast<void*>(uintptr_t(1)),1,0,100,200);
        Require(messages.empty() && producerCaptures==32,"C2B stale route queried resources");
    }
    work::SetEnabled(false);
    {
        trace::ConsumerScope scope("drawLine",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawLine","cpu","unsupported",reinterpret_cast<void*>(uintptr_t(1)),0,0,0,0);
    }
    Require(messages.empty() && producerCaptures==32,"C2B disabled route queried resources");
}
}
void RunC2ConsumerTraceTests() {
    const auto logger=trace::logMessage;const auto producer=trace::captureProducer;
    struct Restore {
        void (*logger)(const char*);bool (*producer)(void*,trace::Producer&);
        ~Restore() {work::SetEnabled(false);trace::SetCallbacks(logger,producer);}
    } restore{logger,producer};
    trace::SetCallbacks(Capture,Producer);
    ContextAndAttribution();WindowBudgets();EpochsAndDisabled();UTF8AndWireLimits();ProducerSafety();RejectedNamesAndCallbackErrors();SpanRoutes();
    std::cout<<"PASS C2 CPU consumers: scoped identity, bounded read/caller/producer windows, epochs and UTF8 wire safety\n";
}
