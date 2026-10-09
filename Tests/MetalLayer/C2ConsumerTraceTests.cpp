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
std::vector<std::string> spanMessages;
std::vector<std::string> readAggregateMessages;
unsigned producerCaptures=0;
void Require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
void Capture(const char* value) {
    if(std::strncmp(value,"metal.layerSpan ",16)==0) spanMessages.emplace_back(value);
    else if(std::strncmp(value,"metal.cpuConsumerAggregate ",27)==0) readAggregateMessages.emplace_back(value);
    else messages.emplace_back(value);
}
void ThrowingLog(const char*) {throw std::runtime_error("diagnostic callback");}
bool ThrowingProducer(void*,trace::Producer&) {throw std::runtime_error("diagnostic resource capture");}
bool Producer(void* resource,trace::Producer& p) {
    ++producerCaptures;
    if(!resource) return false;
    p={77,123,5,17,13};return true;
}
bool ProducerTakesWindow(void* resource,trace::Producer& p) {
    const bool result=Producer(resource,p);
    work::Take();return result;
}
bool ProducerResetsSession(void* resource,trace::Producer& p) {
    const bool result=Producer(resource,p);
    work::SetEnabled(false);work::SetEnabled(true);return result;
}
void Begin() {work::SetEnabled(true);messages.clear();spanMessages.clear();readAggregateMessages.clear();producerCaptures=0;}
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
void CompleteReadAggregation() {
    Begin();
    {
        trace::ConsumerScope c("drawImageStretch",trace::Access::Write,"method");
        for(unsigned i=0;i<71;++i) {auto r=Read();r.textureID=i+1;Report(r);}
    }
    {std::lock_guard<std::mutex> lock(work::mutex);
        const auto& w=work::profile.cpuConsumerBudget.readWindow;
        Require(w.totals.calls==71 && w.groups[0].metrics.calls==71 && w.repeatedReads==70 &&
            w.totals.bytes==71*884 && w.totals.wallNS==71*91 && w.totals.waitNS==71*63,
            "CPU read aggregation lost texture-independent metrics after detail limit");}
    work::Take();
    Require(readAggregateMessages.size()==4 && readAggregateMessages.back().find("\"calls\":71")!=std::string::npos,
        "CPU read aggregate footer/overflow rows missing");
    for(const auto& message:readAggregateMessages) Require(message.size()<=900,"CPU read aggregate line exceeds bound");
    Begin();
    for(unsigned i=0;i<70;++i) {
        const auto name=std::string("dynamic")+std::to_string(i);
        trace::ConsumerScope c(name.c_str(),trace::Access::Read,"native");Report(Read());
    }
    {trace::ConsumerScope c("clear",trace::Access::Write,"method");Report(Read());}
    {std::string name(49,'a');trace::ConsumerScope c(name.c_str(),trace::Access::Read,"native");Report(Read());}
    {std::lock_guard<std::mutex> lock(work::mutex);const auto& w=work::profile.cpuConsumerBudget.readWindow;
        Require(w.totals.calls==72 && w.capacityRecords==14 && w.oversizeRecords==1 &&
            w.groups[4].metrics.calls==1 && w.capacityOverflow.bytes==14*884 && w.oversizeOverflow.waitNS==63,
            "CPU read protected slot or independent overflow failed");}
    work::Take();
    Begin();work::Take();Require(readAggregateMessages.size()==3,"empty CPU read window has no explicit zero footer");
    Begin();auto r=Read();auto input=trace::ReadInput(r);
    work::Record(false,r.textureID,r.width,r.height,r.bytes,r.wallNS,r.waitNS,false,r.source,r.epoch,false,&input);
    r.windowID=input.windowID;r.detailReserved=input.detail;r.callerReserved=input.caller;
    work::Take();
    Require(!trace::ReportRead(r),"late read detail was attached to next window");
    {std::lock_guard<std::mutex> lock(work::mutex);
        Require(work::profile.cpuConsumerBudget.readWindow.totals.calls==0,"late read was double counted in new window");}
    krkrsdl3::cpu_reads::Window overflow;
    overflow.Record("clear","method","write","origin",{1,UINT64_MAX,UINT64_MAX,UINT64_MAX},true);
    overflow.Record("clear","method","write","origin",{1,1,1,1},true);
    Require(overflow.overflow && overflow.totals.calls==2 && overflow.totals.bytes==UINT64_MAX,
        "successful-read metric overflow was silent or wrapped");
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
        for(unsigned i=0;i<71;++i) trace::ReportSpanRoute("drawLine","gpu","applied",
            reinterpret_cast<void*>(uintptr_t(1)),4,16,188,1024);
    }
    Require(spanMessages.empty() && producerCaptures==1,"C2B repeated routes queried identities or emitted per-call logs");
    const auto profile=work::Take();
    Require(profile.transfers.empty() && spanMessages.size()==3,"C2B aggregate/sample/window missing or changed C0");
    Require(spanMessages[0].find("\"calls\":71,\"spanCount\":284,\"sourceBytes\":1136,\"parameterBytes\":13348,\"scratchBytes\":72704")!=std::string::npos,
        "C2B 71-call totals lost or double-counted");
    Require(spanMessages[1].find("\"sessionID\":77,\"textureID\":123,\"contentVersion\":5")!=std::string::npos &&
        spanMessages.back().find("\"samples\":1,\"repeatedOmitted\":70,\"capacityOmitted\":0")!=std::string::npos,
        "C2B representative or repeated-call accounting failed");
    const auto firstID=profile.spanRouteWindowID;
    Require(firstID!=0,"C2B window ID missing");
    spanMessages.clear();
    const auto empty=work::Take();
    Require(empty.spanRouteWindowID>firstID && spanMessages.size()==1 && spanMessages[0].find("\"calls\":0")!=std::string::npos,
        "C2B zero-call window missing or window ID repeated");
    // Force a window handoff inside the metadata callback. The old admission
    // may emit unknown identity, but must never populate the new window.
    Begin();trace::SetCallbacks(Capture,ProducerTakesWindow);
    {
        trace::ConsumerScope scope("drawLine",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawLine","gpu","none",reinterpret_cast<void*>(uintptr_t(1)),1,4,48,512);
    }
    Require(spanMessages.size()==3 && producerCaptures==1 && spanMessages[1].find("\"sessionID\":null")!=std::string::npos,
        "C2B metadata handoff fabricated identity for the already-taken window");
    work::Take();
    Require(spanMessages.size()==4 && spanMessages.back().find("\"calls\":0")!=std::string::npos &&
        spanMessages.back().find("\"samples\":0")!=std::string::npos,
        "C2B metadata from a taken window leaked into the next window");
    Begin();trace::SetCallbacks(Capture,ProducerResetsSession);
    {
        trace::ConsumerScope scope("drawPath",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawPath","gpu","none",reinterpret_cast<void*>(uintptr_t(1)),1,0,48,512);
    }
    work::Take();
    Require(producerCaptures==1 && spanMessages.size()==1 && spanMessages[0].find("\"calls\":0")!=std::string::npos &&
        spanMessages[0].find("\"samples\":0")!=std::string::npos,
        "C2B metadata from the previous generation leaked after session reset");
    trace::SetCallbacks(Capture,Producer);
    Begin();
    {
        trace::ConsumerScope scope("drawPath",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawPath","cpu","record",nullptr,0,0,0,0);
        work::Take();
        Require(spanMessages.size()==3 && spanMessages[1].find("\"sessionID\":null,\"textureID\":null,\"contentVersion\":null")!=std::string::npos,
            "C2B unavailable CPU target identity fabricated zeros");
        work::SetEnabled(false);work::SetEnabled(true);spanMessages.clear();
        trace::ReportSpanRoute("drawPath","gpu","applied",reinterpret_cast<void*>(uintptr_t(1)),1,0,100,200);
        Require(spanMessages.empty() && producerCaptures==0,"C2B stale route queried resources");
    }
    work::SetEnabled(false);
    {
        trace::ConsumerScope scope("drawLine",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawLine","cpu","unsupported",reinterpret_cast<void*>(uintptr_t(1)),0,0,0,0);
    }
    Require(spanMessages.empty() && producerCaptures==0,"C2B disabled route queried resources");
    Begin();
    {
        trace::ConsumerScope scope("drawPath",trace::Access::Write,"method");
        for(unsigned i=0;i<5000;++i) trace::ReportSpanRoute("drawPath","cpu","record",nullptr,0,0,0,0);
        trace::ReportSpanRoute("drawPath","gpu","none",reinterpret_cast<void*>(uintptr_t(1)),1,0,48,512);
    }
    work::Take();
    Require(spanMessages.size()==5 && producerCaptures==1 &&
        spanMessages.back().find("\"gpuCalls\":1,\"cpuCalls\":5000")!=std::string::npos,
        "C2B CPU burst hid the later GPU route");
    Begin();
    {
        trace::ConsumerScope scope("drawLine",trace::Access::Write,"method");
        for(const auto* method:krkrsdl3::span_route::Methods)
            for(const auto* route:krkrsdl3::span_route::Routes)
                for(const auto* reason:krkrsdl3::span_route::Reasons)
                    trace::ReportSpanRoute(method,route,reason,reinterpret_cast<void*>(uintptr_t(1)),1,4,48,512);
    }
    work::Take();
    Require(producerCaptures==32 && spanMessages.size()==krkrsdl3::span_route::GroupCount+33 &&
        spanMessages.back().find("\"samples\":32,\"repeatedOmitted\":0,\"capacityOmitted\":"+
            std::to_string(krkrsdl3::span_route::GroupCount-32))!=std::string::npos,
        "C2 fixed aggregation or 15+17 representative bound failed");
    for(const auto& message:spanMessages) Require(message.size()<=900,"C2B v2 native line exceeded 900 bytes");
    Begin();
    {
        trace::ConsumerScope scope("drawLine",trace::Access::Write,"method");
        trace::ReportSpanRoute("drawLine","gpu","none",nullptr,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX);
        trace::ReportSpanRoute("drawLine","gpu","none",nullptr,1,1,1,1);
        trace::ReportSpanRoute("unknown","gpu","none",nullptr,0,0,0,0);
    }
    work::Take();
    Require(spanMessages.back().find("\"invalidRecords\":1,\"overflow\":true")!=std::string::npos,
        "C2B overflow or unknown dictionary entry was silent");
    for(const auto& message:spanMessages) Require(message.size()<=900,"C2B max uint64 wire width exceeded bound");
}
}
void RunC2ConsumerTraceTests() {
    const auto logger=trace::logMessage;const auto producer=trace::captureProducer;
    struct Restore {
        void (*logger)(const char*);bool (*producer)(void*,trace::Producer&);
        ~Restore() {work::SetEnabled(false);trace::SetCallbacks(logger,producer);}
    } restore{logger,producer};
    trace::SetCallbacks(Capture,Producer);
    CompleteReadAggregation();ContextAndAttribution();WindowBudgets();EpochsAndDisabled();UTF8AndWireLimits();ProducerSafety();RejectedNamesAndCallbackErrors();SpanRoutes();
    std::cout<<"PASS C2 CPU consumers: scoped identity, bounded read/caller/producer windows, epochs and UTF8 wire safety\n";
}
