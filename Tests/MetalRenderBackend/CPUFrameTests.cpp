#include "CPUFrameDiagnostics.h"
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
uint64_t timeNS=1;
std::vector<std::string> lines;
void check(bool value) {if(!value)throw std::runtime_error("CPU frame attribution");}
uint64_t number(const std::string& line,const char* name) {
    const std::string key=std::string(name)+"=";
    auto p=line.find(key);check(p!=std::string::npos);return std::stoull(line.substr(p+key.size()));
}
const std::string& find(const std::string& tag) {
    auto p=std::find_if(lines.begin(),lines.end(),[&](const auto& s){return s.find(tag)!=std::string::npos;});
    check(p!=lines.end());return *p;
}
}
void CPUFrameTests() {
    using namespace krkrsdl3::cpu_frame;
    clockNS=[](){return timeNS;};
    rawLine=[](const char* s){lines.emplace_back(s);};
    captureStack=[](char* out,size_t bytes,unsigned frames){
        check(bytes==513 && frames==4);
        std::snprintf(out,bytes,"scene.ks:7[call%llu]",(unsigned long long)callID);return true;
    };
    SetEnabled(true,1,10);lines.clear();
    {
        StepScope step;CallScope outer;
        {CallScope inner;{EventScope native(Kind::PSBMaterialize);native.SetWorkBytes(1234);timeNS+=20000000;}
            timeNS+=20000000;}
        timeNS+=20000000;
    }
    check(lines.empty()); // Hold copied stacks, not log output or VM references.
    check(window.metrics[size_t(Kind::VM)].calls==2 && window.metrics[size_t(Kind::VM)].wallNS==100000000);
    check(window.metrics[size_t(Kind::Step)].wallNS==60000000);
    check(window.metrics[size_t(Kind::PSBMaterialize)].workBytes==1234);
    TakeWindow(10,11);
    check(number(find("cpu.summary"),"emitted")==4 && number(find("cpu.summary"),"dropped")==0);
    const auto& root=find("kind=vm wallNS=60000000");
    check(number(root,"parentCallID")==0 && number(root,"childWallNS")==40000000 && number(root,"residualWallNS")==20000000);
    check(root.find("call1]")!=std::string::npos); // captured before callID/tracer unwind
    check(number(root,"vmCalls")==2);
    const auto& native=find("kind=psb.materialize wallNS=20000000");
    check(number(native,"parentScopeID")==number(find("kind=vm wallNS=40000000"),"scopeID"));
    check(number(native,"vmCalls")==0 && number(find("kind=vm wallNS=40000000"),"vmCalls")==1);
    check(number(find("kind=step wallNS=60000000"),"childWallNS")==60000000);
    check(number(find("kind=step wallNS=60000000"),"vmCalls")==2);
    for(const auto& line:lines)check(line.size()<=960);

    // Many early children must not exclude the late outer call or native phase.
    SetEnabled(true,2,20);lines.clear();
    {
        StepScope step;CallScope outer;
        for(int i=0;i<7;++i){CallScope child;timeNS+=20000000;}
        {EventScope nativePhase(Kind::PSBDestroy);timeNS+=30000000;}
    }
    TakeWindow(20,21);
    check(number(find("cpu.summary"),"emitted")==4 && number(find("cpu.summary"),"frameDropped")==6);
    check(number(find("kind=vm wallNS=170000000"),"role")==1);
    check(number(find("kind=psb.destroy wallNS=30000000"),"role")==2);

    // Window selection favors a late 900ms step over an early 100ms step.
    SetEnabled(true,3,30);lines.clear();
    for(uint64_t duration:{100000000ULL,200000000ULL,900000000ULL}) {
        StepScope step;CallScope outer;
        {EventScope phase(Kind::PSBRead);timeNS+=20000000;}
        timeNS+=duration-20000000;
    }
    TakeWindow(30,31);
    check(number(find("cpu.summary"),"eligible")==9 && number(find("cpu.summary"),"emitted")==6);
    check(number(find("cpu.summary"),"windowDropped")==3);
    for(const auto& line:lines)if(line.find("cpu.slow")==0)check(number(line,"stepID")!=1);
    check(std::any_of(lines.begin(),lines.end(),[](const auto& line){
        return line.find("cpu.slow")==0 && line.find("kind=psb.read ")!=std::string::npos && number(line,"stepID")==3;
    })); // The phase finished before the late step was known to be heavy.

    // Owner, epoch, exception and cross-window boundaries.
    SetEnabled(true,4,40);lines.clear();
    const auto before=window.metrics[size_t(Kind::Image)].calls;
    std::thread worker([](){EventScope event(Kind::Image);});worker.join();
    check(window.metrics[size_t(Kind::Image)].calls==before);
    {EventScope stale(Kind::Image);SetEnabled(true,5,50);timeNS+=20000000;}
    check(window.metrics[size_t(Kind::Image)].calls==0 && activeScope==nullptr);
    try {StepScope step;CallScope outer;EventScope failure(Kind::PSBRead);timeNS+=20000000;throw 1;}catch(int){}
    check(window.metrics[size_t(Kind::PSBRead)].failures==1);
    TakeWindow(50,51);check(number(find("kind=psb.read wallNS=20000000"),"failed")==1);
    SetEnabled(true,6,60);lines.clear();
    {StepScope step;CallScope outer;
        {EventScope child(Kind::PSBRead);timeNS+=20000000;}
        TakeWindow(60,61);timeNS+=20000000;}
    TakeWindow(61,62);
    check(number(find("kind=vm wallNS=40000000"),"startWindow")==60);
    for(const auto& line:lines)if(line.find("cpu.summary")==0)
        check(number(line,"eligible")==number(line,"emitted")+number(line,"dropped"));

    // Rolling byte budget survives window resets; quote expansion is explicit.
    SetEnabled(true,7,70);lines.clear();timeNS=1;
    captureStack=[](char* out,size_t bytes,unsigned){std::memset(out,'"',bytes-1);out[bytes-1]=0;return true;};
    for(unsigned w=70;w<74;++w) {
        for(int frame=0;frame<2;++frame) {
            StepScope step;CallScope outer;
            {CallScope inner;{EventScope nativePhase(Kind::PSBRead);timeNS+=17000000;}timeNS+=17000000;}
        }
        TakeWindow(w,w+1);
    }
    size_t bytes=0;bool truncated=false,byteDrop=false;
    for(const auto& line:lines) {
        check(line.size()<=960);
        if(line.find("cpu.slow")==0){bytes+=line.size()+1;truncated|=number(line,"stackTruncated")!=0;}
        if(line.find("cpu.summary")==0) {
            byteDrop|=number(line,"byteDropped")!=0;
            check(number(line,"eligible")==number(line,"emitted")+number(line,"dropped"));
        }
    }
    check(bytes<=8192 && truncated && byteDrop);

    SetEnabled(true,8,80);lines.clear();
    rawLine=[](const char* s){if(std::strstr(s,"cpu.slow"))throw 1;lines.emplace_back(s);};
    {StepScope step;CallScope outer;timeNS+=20000000;}
    TakeWindow(80,81);
    check(number(find("cpu.summary"),"callbackDropped")==2 && number(find("cpu.summary"),"callbackFailures")==2);
    check(number(find("cpu.summary"),"eligible")==number(find("cpu.summary"),"dropped"));

    // Logging or stack-capture reentry cannot contaminate a new session.
    SetEnabled(true,9,90);lines.clear();
    rawLine=[](const char*){SetEnabled(true,10,100);throw 1;};
    {StepScope step;CallScope outer;timeNS+=20000000;}
    TakeWindow(90,91);check(epoch==10 && windowID==100 && window.eligible==0 && window.emitted==0 && window.callbackFailures==0);
    rawLine=[](const char* s){lines.emplace_back(s);};
    captureStack=[](char*,size_t,unsigned){SetEnabled(true,11,110);return false;};
    {EventScope stale(Kind::Image);timeNS+=20000000;}
    check(epoch==11 && window.eligible==0 && window.stackFailures==0 && activeScope==nullptr);
    SetEnabled(false,12,120);clockNS=Clock;rawLine=nullptr;captureStack=nullptr;
}
