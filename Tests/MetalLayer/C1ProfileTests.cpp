#include "LayerWorkDiagnostics.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
namespace work=krkrsdl3::layer_work;
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void RoutingAndTransfer() {
    work::SetEnabled(true);
    work::Record(false,1,1,1,4,99,98); // Outside a shrink call must not pollute quantiles.
    for(int i=0;i<2;++i) {
        work::ShrinkScope scope("shrinkCopy",i ? "{\"sourceROI\":[2,3,4,5]}" : "{}","distinct");
        scope.PrepCPU(11);
        work::RecordShrinkResult(true,"applied",35);
        work::RecordShrinkParameters(2,144,512);
        work::Record(false,100+i,7,5,140,13,i ? 7 : 0,false,"shrinkCopy.read");
        work::Record(true,200+i,7,5,28,3,1,false,"shrinkCopy.write");
    }
    {work::ShrinkScope scope("shrinkCopyFast");work::RecordShrinkResult(false,"noop",0);}
    const auto sample=work::Take();
    Require(sample.workProfileVersion==2 && sample.transitionProfileVersion==1 && sample.shrinkProfileVersion==1,"C1 changed legacy versions");
    Require(sample.shrinkProfiles.find("\"gpuCalls\":2,\"cpuCalls\":0,\"noopCalls\":0,\"pixels\":70")!=std::string::npos,"C1 geometry/texture split aggregate rows");
    Require(sample.shrinkProfiles.find("sourceROI")!=std::string::npos,"C1 did not retain latest geometry");
    Require(sample.shrinkProfiles.find("\"parameterUploads\":4,\"parameterBytes\":288,\"temporaryBytes\":1024")!=std::string::npos,"C1 parameter/temp bytes lost");
    Require(sample.shrinkProfiles.find("\"readCalls\":2,\"readBytes\":280,\"readWallNS\":26,\"readWaitNS\":7")!=std::string::npos,"C1 transfer subset wrong");
    Require(sample.transferOrigins.find("read:shrinkCopy.read=2/280/26/7")!=std::string::npos,"C1 double counted C0 totals");
    Require(sample.shrinkReadWaitSampleCount==2 && sample.shrinkReadWaitSamplesNS[0]==0 && sample.shrinkReadWaitSamplesNS[1]==7,"C1 lost zero wait or sampled outside scope");
    Require(sample.shrinkProfiles.find("\"gpuCalls\":0,\"cpuCalls\":0,\"noopCalls\":1")!=std::string::npos,"C1 noop became CPU fallback");
}
void ScopesAndSamples() {
    work::SetEnabled(true);
    {
        work::ShrinkScope outer("outer");work::Record(false,1,1,1,4,5,6);
        try {work::ShrinkScope inner("inner");work::Record(false,1,1,1,8,9,10);throw 1;} catch(int) {}
        work::Record(true,1,1,1,12,13,14);
    }
    auto sample=work::Take();
    Require(sample.shrinkProfiles.find("\"method\":\"inner\"")!=std::string::npos && sample.shrinkProfiles.find("\"uploadCalls\":1,\"uploadBytes\":12")!=std::string::npos,"C1 nested/unwind attribution failed");
    {
        work::ShrinkScope old("old");const auto epoch=work::CaptureGeneration();
        work::SetEnabled(false);work::SetEnabled(true);
        work::Record(false,1,1,1,4,5,6,false,nullptr,epoch);
        work::Record(false,1,1,1,4,5,6); // New-generation read with an old scope is not a C1 sample.
        work::RecordShrinkResult(true,"applied",1);
    }
    sample=work::Take();Require(sample.shrinkProfiles=="[]" && sample.shrinkReadWaitSampleCount==0,"C1 epoch leaked into new session");
    work::SetEnabled(false);
    {work::ShrinkScope inactive("disabled");work::SetEnabled(true);work::Record(false,1,1,1,4,5,6);}
    sample=work::Take();Require(sample.shrinkProfiles=="[]" && !sample.shrinkReadWaitSampleCount,"C1 inactive scope retroactively recorded");
    {
        work::ShrinkScope scope("sampleCapacity");
        for(uint64_t i=0;i<work::MaxReadWaitSamples+3;++i) work::Record(false,1,1,1,4,5,i);
    }
    sample=work::Take();Require(sample.shrinkReadWaitSampleCount==2048 && sample.shrinkReadWaitSamplesNS[2047]==2047 && sample.shrinkReadWaitSamplesDropped==3,"C1 bounded raw wait sample overflow failed");
    Require(work::Take().shrinkReadWaitSampleCount==0,"C1 wait samples not reset at window boundary");
}
void CapacityAndWire() {
    work::SetEnabled(true);
    for(size_t i=0;i<work::ShrinkCapacity;++i) {work::ShrinkScope scope(("row"+std::to_string(i)).c_str());}
    {work::ShrinkScope scope("capacity.extra");}
    {work::ShrinkScope scope(std::string(48,'x').c_str());}
    {work::ShrinkScope scope("metadata.extra");scope.SetMetadata(std::string(256,'x').c_str());}
    auto sample=work::Take();
    Require(sample.shrinkProfilesDropped==3 && sample.shrinkOverflow.find("\"capacityRecords\":1,\"oversizeRecords\":2")!=std::string::npos,"C1 row/string overflow silently merged");
    work::SetEnabled(true);
    const auto maximum=std::numeric_limits<uint64_t>::max();
    {
        std::lock_guard<std::mutex> lock(work::mutex);work::profile.shrinkSize=work::ShrinkCapacity;
        for(auto& r:work::profile.shrinks) {
            Require(work::TransitionString(r.method,sizeof(r.method),std::string(15,char(1)).c_str(),95),"method bound rejected");
            Require(work::TransitionString(r.reason,sizeof(r.reason),std::string(21,char(2)).c_str(),127),"reason bound rejected");
            Require(work::TransitionString(r.aliasClass,sizeof(r.aliasClass),std::string(15,char(3)).c_str(),95),"alias bound rejected");
            Require(work::TransitionString(r.metadata,sizeof(r.metadata),std::string(85,char(4)).c_str(),511),"metadata bound rejected");
            auto& m=r.metrics;
            m.gpuCalls=m.cpuCalls=m.noopCalls=m.pixels=m.wallNS=m.prepCPUNS=m.parameterUploads=m.parameterBytes=m.temporaryBytes=maximum;
            m.readCalls=m.readBytes=m.readWallNS=m.readWaitNS=m.uploadCalls=m.uploadBytes=m.uploadWallNS=m.uploadWaitNS=maximum;
        }
    }
    sample=work::Take();Require(sample.shrinkProfiles.size()<work::ShrinkJSONCapacity && sample.shrinkProfiles.find(std::to_string(maximum))!=std::string::npos,"C1 maximum escaped numeric rows exceed bridge");
}
}
void C1ProfileTests() {
    RoutingAndTransfer();ScopesAndSamples();CapacityAndWire();work::SetEnabled(false);
    std::cout<<"PASS C1 shrink diagnostics: entrypoint/routing/alias grouping, transfer subsets, raw waits, epochs and bounded wire rows\n";
}
