#include "CPUFrameDiagnostics.h"
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace { uint64_t timeNS=1;std::vector<std::string> lines; }
void CPUFrameTests() {
    using namespace krkrsdl3::cpu_frame;
    const auto check=[](bool value){if(!value)throw std::runtime_error("CPU frame attribution");};
    clockNS=[](){return timeNS;};rawLine=[](const char* s){lines.emplace_back(s);};
    captureStack=[](char* out,size_t bytes,unsigned frames){if(bytes!=513 || frames!=4)throw 1;std::strcpy(out,"scene.ks:7[run]");return true;};
    SetEnabled(true,1,10);
    {
        StepScope step;CallScope outer;const auto parent=callID;
        {CallScope child;check(callID!=parent);timeNS+=20000000;}
        timeNS+=20000000;
    }
    check(lines.size()==3 && window.metrics[size_t(Kind::VM)].calls==2);
    check(lines[0].find("parentCallID=1")!=std::string::npos);
    check(lines[0].find("stepID=1")!=std::string::npos);
    check(window.metrics[size_t(Kind::VM)].wallNS==60000000); // inclusive, not frame CPU
    check(window.metrics[size_t(Kind::Step)].wallNS==40000000);
    {
        StepScope step;
        for(int i=0;i<7;++i){EventScope event(Kind::KAGNextTag);timeNS+=20000000;}
    }
    check(window.frameDropped>0 && window.emitted<=8 && window.eligible==11);
    const auto before=window.metrics[size_t(Kind::Image)].calls;
    std::thread worker([](){EventScope event(Kind::Image);});worker.join();
    check(window.metrics[size_t(Kind::Image)].calls==before);
    {EventScope stale(Kind::Image);SetEnabled(true,2,20);timeNS+=20000000;}
    check(window.metrics[size_t(Kind::Image)].calls==0);
    try {EventScope failure(Kind::ScriptStorage);timeNS+=20000000;throw 1;}catch(int){}
    check(window.metrics[size_t(Kind::ScriptStorage)].failures==1);
    TakeWindow(20,21);check(lines.back().find("kind=kag.nextTag")!=std::string::npos);
    for(const auto& line:lines)check(line.size()<=960);
    SetEnabled(true,3,30);lines.clear();timeNS=1;
    captureStack=[](char* out,size_t bytes,unsigned){std::memset(out,'x',bytes-1);out[bytes-1]=0;return true;};
    for(unsigned w=30;w<34;++w) {
        for(int frame=0;frame<2;++frame) {StepScope step;
            for(int i=0;i<4;++i){EventScope call(Kind::VM);timeNS+=17000000;}}
        check(window.emitted<=8 && window.eligible==10);
        TakeWindow(w,w+1);
    }
    size_t bytes=0;for(const auto& line:lines)if(line.find("cpu.slow ")==0)bytes+=line.size()+1;
    check(bytes<=8192); // All four windows fit inside a single rolling second.
    rawLine=[](const char*){throw 1;};timeNS+=1000000000;
    {EventScope call(Kind::Image);timeNS+=17000000;}
    check(window.callbackFailures==1 && window.dropped==1);
    SetEnabled(false,3,30);clockNS=Clock;rawLine=nullptr;captureStack=nullptr;
}
