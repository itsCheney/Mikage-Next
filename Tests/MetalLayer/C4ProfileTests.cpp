#include "LayerWorkDiagnostics.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
namespace work=krkrsdl3::layer_work;
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void Row(const char* name,uint64_t frame=1) {
    work::TransitionScope scope(name,name,127,91,131,97,139,101,17,13,500,frame);
    scope.SetPixels(21);
    work::Record(false,1,3,7,84,9,4);
    work::RecordTransitionResult(true,"applied",21,1,128);
}
void AttributionAndRouting() {
    work::SetEnabled(true);
    Row("unknown,\"handler\\中文",1);Row("unknown,\"handler\\中文",1);Row("unknown,\"handler\\中文",2);
    auto sample=work::Take();
    Require(sample.transitionProfileVersion==1 && sample.transitionProfilesDropped==0,"C4 version/dropped contract failed");
    Require(sample.transitionProfiles.find("\"frames\":2,\"gpuCalls\":3")!=std::string::npos,"C4 partitions counted as frames or lost Process calls");
    Require(sample.transitionProfiles.find("\"parameterUploads\":3,\"parameterBytes\":384")!=std::string::npos,"C4 parameter uploads lost");
    Require(sample.transitionProfiles.find("\"readCalls\":3,\"readBytes\":252,\"readWallNS\":27,\"readWaitNS\":12")!=std::string::npos,"C4 transfer detail lost metrics");
    Require(sample.transferOrigins.find("=3/252/27/12")!=std::string::npos,"C4 detail duplicated overall transfers");
    Require(sample.transitionProfiles.find("unknown,\\\"handler\\\\中文")!=std::string::npos,"C4 unknown UTF8 provider name not preserved");
    {
        work::TransitionScope scope("scroll","scroll",8,8,8,8,8,8,8,8);
        scope.SetPixels(64);
        work::RecordTransitionResult(true,"renderManager",24);
        work::RecordTransitionResult(false,"sourceUnavailable",16);
        work::RecordTransitionResult(true,"renderManager",24);
    }
    sample=work::Take();
    Require(sample.transitionProfiles.find("mixed.sourceUnavailable")!=std::string::npos &&
        sample.transitionProfiles.find("\"gpuCalls\":0,\"cpuCalls\":1,\"passthroughCalls\":0,\"pixels\":64")!=std::string::npos,
        "C4 mixed scroll hid CPU fallback or counted every draw as Process");
    {
        work::TransitionScope scope("crossfade","crossfade",8,8,8,8,8,8,8,8);
        work::RecordTransitionResult(false,"passthrough",64);
    }
    sample=work::Take();
    Require(sample.transitionProfiles.find("\"gpuCalls\":0,\"cpuCalls\":0,\"passthroughCalls\":1")!=std::string::npos,"C4 passthrough became CPU fallback");
}
void ScopesAndEpochs() {
    work::SetEnabled(true);
    {
        work::TransitionScope outer("outer","outer",8,8,8,8,8,8,8,8);
        work::Record(false,1,1,1,4);
        try {
            work::TransitionScope inner("inner","inner",8,8,8,8,8,8,8,8);
            work::Record(false,1,1,1,8);throw 1;
        } catch(int) {}
        work::Record(true,1,1,1,12);
    }
    auto sample=work::Take();
    Require(sample.transitionProfiles.find("\"requested\":\"inner\"")!=std::string::npos &&
        sample.transitionProfiles.find("\"readCalls\":1,\"readBytes\":8")!=std::string::npos &&
        sample.transitionProfiles.find("\"uploadCalls\":1,\"uploadBytes\":12")!=std::string::npos,
        "C4 nested/exception scope failed to restore attribution");
    {
        work::TransitionScope old("old","old",8,8,8,8,8,8,8,8);
        const auto epoch=work::CaptureGeneration();
        work::SetEnabled(false);work::SetEnabled(true);
        work::Record(false,1,1,1,4,0,0,false,nullptr,epoch);
        work::RecordTransitionResult(true,"applied",1);
    }
    Require(work::Take().transitionProfiles=="[]","C4 old generation scope survived reset");
    work::SetEnabled(false);
    Row("disabled");sample=work::Take();
    Require(sample.transitionProfiles=="[]" && sample.transferOrigins.empty(),"C4 disabled scope recorded work");
    work::SetEnabled(true);Row("newSession");
    Require(work::Take().transitionProfiles.find("newSession")!=std::string::npos,"C4 disabled scope leaked into new session");
}
void CapacityAndWireBounds() {
    work::SetEnabled(true);
    for(int i=0;i<64;++i) Row(("handler"+std::to_string(i)).c_str(),i);
    Row("capacity.extra");Row((std::string(96,'x')+"suffix").c_str());
    {
        work::TransitionScope scope("metadataTooLong","metadataTooLong",8,8,8,8,8,8,8,8);
        work::SetTransitionMetadata(std::string(256,'x').c_str());
        work::RecordTransitionResult(false,"unsupported",64);
    }
    const auto sample=work::Take();
    Require(sample.transitionProfilesDropped==3 && sample.transitionOverflow.find("\"capacityRecords\":1,\"oversizeRecords\":2")!=std::string::npos,
        "C4 overflow silently truncated or merged names/metadata");
    Require(sample.transitionOverflow.find("\"readCalls\":2,\"readBytes\":168")!=std::string::npos,"C4 overflow lost transfer counters");
    // Force all bounded rows to the wire's maximum decimal/escaped fields.
    const auto maximum=std::numeric_limits<uint64_t>::max();
    work::SetEnabled(true);
    for(int i=0;i<64;++i) Row(("worst"+std::to_string(i)).c_str());
    {
        std::lock_guard<std::mutex> lock(work::mutex);
        for(auto& row:work::profile.transitions) {
            std::string requested(31,char(1));requested+='a';
            std::string effective(31,char(2));effective+='b';
            std::string reason(21,char(3));reason+='c';
            std::string metadata(85,char(4));metadata+='d';
            Require(work::TransitionString(row.requested,sizeof(row.requested),requested.c_str(),191),"worst requested string rejected");
            Require(work::TransitionString(row.effective,sizeof(row.effective),effective.c_str(),191),"worst provider string rejected");
            Require(work::TransitionString(row.reason,sizeof(row.reason),reason.c_str(),127),"worst reason string rejected");
            Require(work::TransitionString(row.metadata,sizeof(row.metadata),metadata.c_str(),511),"worst metadata string rejected");
            for(auto& dim:row.dimensions) dim=std::numeric_limits<int>::min();
            row.firstTick=row.lastTick=maximum;
            auto& m=row.metrics;
            m.frames=m.gpuCalls=m.cpuCalls=m.passthroughCalls=m.pixels=m.parameterUploads=m.parameterBytes=maximum;
            m.readCalls=m.readBytes=m.readWallNS=m.readWaitNS=m.uploadCalls=m.uploadBytes=m.uploadWallNS=m.uploadWaitNS=maximum;
        }
    }
    const auto worst=work::Take();
    Require(worst.transitionProfiles.size()<work::TransitionJSONCapacity,"C4 worst-case rows exceed C wire buffer");
    Require(worst.transitionProfiles.find(std::to_string(maximum))!=std::string::npos,"C4 max-width counters were truncated");
}
}
void C4ProfileTests() {
    AttributionAndRouting();ScopesAndEpochs();CapacityAndWireBounds();work::SetEnabled(false);
    std::cout<<"PASS C4 transition diagnostics: provider names, partition frames, routing, transfer detail, epochs and bounded wire rows\n";
}
