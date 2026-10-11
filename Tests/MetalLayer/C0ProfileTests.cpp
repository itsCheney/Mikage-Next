#include "LayerWorkDiagnostics.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
namespace work=krkrsdl3::layer_work;
using Metrics=std::array<uint64_t,4>;
using Totals=std::array<Metrics,2>;
void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
Metrics ParseMetrics(const std::string& text) {
    Metrics result{};size_t begin=0;
    for(size_t i=0;i<result.size();++i) {
        const auto end=text.find('/',begin);
        Require((i==3)==(end==std::string::npos),"C0 malformed four-metric tuple");
        result[i]=std::stoull(text.substr(begin,end-begin));begin=end+1;
    }
    return result;
}
void Add(Metrics& destination,const Metrics& value) {
    for(size_t i=0;i<destination.size();++i) destination[i]+=value[i];
}
Totals ViewTotals(const std::string& text) {
    Totals result{};size_t begin=0;
    while(begin<text.size()) {
        const bool upload=text.compare(begin,7,"upload:")==0;
        Require(upload || text.compare(begin,5,"read:")==0,"C0 unexpected transfer direction");
        // The legacy view has a comma inside (size,lease), before its equals.
        const auto equals=text.find('=',begin);
        const auto actualEquals=equals!=std::string::npos && equals>=6 && text.compare(equals-6,7,",lease=")==0
            ? text.find('=',equals+1) : equals;
        Require(actualEquals!=std::string::npos,"C0 missing transfer tuple");
        const auto end=text.find(',',actualEquals);
        Add(result[upload?1:0],ParseMetrics(text.substr(actualEquals+1,end-actualEquals-1)));
        if(end==std::string::npos) break;
        begin=end+1;
    }
    return result;
}
void CheckTotals(const work::Summary& summary) {
    auto origins=ViewTotals(summary.transferOrigins);
    const auto upload=summary.originOverflow.find(",upload=");
    const auto records=summary.originOverflow.find(",capacityRecords=");
    Require(summary.originOverflow.compare(0,5,"read=")==0 && upload!=std::string::npos && records!=std::string::npos,
            "C0 overflow wire grammar changed");
    Add(origins[0],ParseMetrics(summary.originOverflow.substr(5,upload-5)));
    Add(origins[1],ParseMetrics(summary.originOverflow.substr(upload+8,records-upload-8)));
    Require(origins==ViewTotals(summary.transfers),"C0 origin+overflow totals differ from legacy texture view");
}
void AggregationAndCapacity() {
    work::SetEnabled(true);
    for(uint64_t i=0;i<100;++i) {
        work::Record(false,i+1,9,7,i+1,2*(i+1),3*(i+1),i%2!=0,"bitmap.lock");
        work::Record(true,i+1,9,7,2*(i+1),4*(i+1),5*(i+1),i%2==0,"bitmap.lock");
    }
    const auto summary=work::Take();
    Require(summary.transferOrigins=="upload:bitmap.lock=100/10100/20200/25250,read:bitmap.lock=100/5050/10100/15150",
            "C0 origin grouping depends on texture/lease or lost one of four metrics");
    Require(summary.originOverflow=="read=0/0/0/0,upload=0/0/0/0,capacityRecords=0,oversizeRecords=0",
            "C0 texture-capacity pressure leaked into origin overflow");
    CheckTotals(summary);
    work::SetEnabled(true);
    static_assert(work::protectedOrigins.size()==20,"C0 protects rawPointer and legacy rawWrite separately");
    constexpr auto dynamicCount=work::OriginCapacity-work::protectedOrigins.size()*2;
    static_assert(dynamicCount==24,"C0 expected 40 reserved and 24 dynamic slots");
    for(size_t i=0;i<dynamicCount;++i) {
        const auto name="dynamic."+std::to_string(i);
        work::Record(false,i+1,1,1,1,2,3,false,name.c_str());
    }
    work::Record(false,300,1,1,11,12,13,false,"overflow.read");
    work::Record(true,301,1,1,21,22,23,false,"overflow.upload");
    for(const auto* name:work::protectedOrigins) for(bool upload:{false,true})
        work::Record(upload,400,1,1,4,5,6,false,name);
    const auto full=work::Take();
    Require(std::count(full.transferOrigins.begin(),full.transferOrigins.end(),',')==63,
            "C0 protected origins disappeared after dynamic capacity exhaustion");
    Require(full.originOverflow=="read=1/11/12/13,upload=1/21/22/23,capacityRecords=2,oversizeRecords=0",
            "C0 direction-specific capacity overflow lost totals or record counts");
    CheckTotals(full);
    work::SetEnabled(true);
    work::Record(false,1,1,1,100,1,1,false,"big");
    work::Record(true,2,1,1,1,1,900,false,"smallWait");
    work::Record(true,3,1,1,10,1,5,false,"a");
    work::Record(false,4,1,1,10,1,5,false,"a");
    work::Record(false,5,1,1,10,1,5,false,"b");
    const auto sorted=work::Take();
    Require(sorted.transferOrigins=="upload:smallWait=1/1/1/900,read:a=1/10/1/5,upload:a=1/10/1/5,read:b=1/10/1/5,read:big=1/100/1/1",
            "C0 origin order is not wait/bytes/name/direction stable");
    CheckTotals(sorted);
}
void EncodingAndBounds() {
    work::SetEnabled(true);
    const std::string name="a,b=c/@% "+std::string("\xE4\xB8\xAD")+std::string(35,'z');
    Require(name.size()==47,"C0 name-boundary fixture is not 47 bytes");
    work::Record(false,1,1,1,7,8,9,false,name.c_str());
    const std::string longA=name+"a",longB=name+"b";
    work::Record(false,2,1,1,11,12,13,false,longA.c_str());
    work::Record(true,3,1,1,21,22,23,false,longB.c_str());
    work::Record(true,4,1,1,31,32,33,false,"mixed",work::RecordEpochSentinel,true);
    const auto escaped=work::Take();
    Require(escaped.transferOrigins=="read:a%2Cb%3Dc%2F%40%25%20%E4%B8%AD"+std::string(35,'z')+"=1/7/8/9",
            "C0 delimiters/UTF8 were not percent escaped or long names merged with 47-byte prefix");
    Require(escaped.originOverflow=="read=1/11/12/13,upload=2/52/54/56,capacityRecords=0,oversizeRecords=3",
            "C0 oversize source or explicit deferred oversize flag lost direction metrics");
    work::SetEnabled(true);
    for(const auto* protectedName:work::protectedOrigins) for(bool upload:{false,true})
        work::Record(upload,1,1,1,1,1,1,false,protectedName);
    for(size_t i=0;i<work::OriginCapacity-work::protectedOrigins.size()*2;++i) {
        const auto unsafe=std::string(46,'%')+char(0x80+i);
        work::Record(false,2+i,1,1,1,1,1,false,unsafe.c_str());
    }
    {
        std::lock_guard<std::mutex> lock(work::mutex);
        for(auto& origin:work::profile.origins)
            origin.calls=origin.bytes=origin.ns=origin.waitNS=std::numeric_limits<uint64_t>::max();
    }
    const auto worst=work::Take();
    Require(std::count(worst.transferOrigins.begin(),worst.transferOrigins.end(),',')==63 && worst.transferOrigins.size()<16384,
            "C0 full 64-row/max-counter/47-byte escape output exceeds the bridge");
    Require(worst.transferOrigins.find("18446744073709551615/18446744073709551615/18446744073709551615/18446744073709551615")!=std::string::npos,
            "C0 maximum-width metric fixture was truncated");
}
void ScopeAndEpochs() {
    work::SetEnabled(true);
    const std::string previous=work::source;
    {
        work::SourceScope outer("outer");work::Record(false,1,1,1,1);
        try {work::SourceScope inner("inner");work::Record(false,2,1,1,2);throw std::runtime_error("unwind");}
        catch(const std::runtime_error&) {}
        Require(std::strcmp(work::source,"outer")==0,"C0 nested scope did not restore after exception");
        work::Record(false,3,1,1,3);
    }
    Require(previous==work::source,"C0 SourceScope leaked an attribution tag");
    const auto scope=work::Take();
    Require(scope.transferOrigins=="read:outer=2/4/0/0,read:inner=1/2/0/0","C0 nested scopes lost original tag metrics");
    CheckTotals(scope);
    const auto old=work::CaptureGeneration();
    Require(old>0 && old!=work::RecordEpochSentinel,"C0 enabled generation is not a usable epoch");
    work::SetEnabled(false);
    Require(work::CaptureGeneration()==0,"C0 disabled CaptureGeneration returned an epoch");
    work::Record(false,1,1,1,1,0,0,false,"off");work::RecordFrame(100,100);
    const auto disabled=work::Take();const auto disabledAgain=work::Take();
    Require(disabled.intervalNS==0 && disabledAgain.intervalNS==0 && disabled.transfers.empty() &&
            disabled.transferOrigins.empty() && disabled.frameSampleCount==0,"C0 disabled take restarted an interval or kept samples");
    work::SetEnabled(true);
    Require(work::CaptureGeneration()!=old,"C0 enable transition did not invalidate old work");
    work::Record(false,1,1,1,1,0,0,false,"stale",old);
    work::Record(true,2,1,1,2,0,0,false,"zero",0);
    const auto rejected=work::Take();
    Require(rejected.transfers.empty() && rejected.transferOrigins.empty() &&
            rejected.originOverflow=="read=0/0/0/0,upload=0/0/0/0,capacityRecords=0,oversizeRecords=0",
            "C0 stale/zero generations updated one of the transfer views");
    work::Record(false,3,1,1,3,4,5,false,"fresh");
    CheckTotals(work::Take());
}
void Frames() {
    work::SetEnabled(true);
    work::RecordFrame(100,7);work::RecordFrame(130,11);
    const auto first=work::Take();
    Require(first.frameSampleCount==2 && first.frameIntervalNS[0]==0 && first.frameIntervalNS[1]==30 &&
            first.frameCpuWallNS[0]==7 && first.frameCpuWallNS[1]==11 && first.frameSamplesDropped==0,
            "C0 frame sample does not preserve interval/CPU wall nanoseconds");
    work::RecordFrame(170,13);const auto next=work::Take();
    Require(next.frameSampleCount==1 && next.frameIntervalNS[0]==40 && next.frameCpuWallNS[0]==13,
            "C0 Take broke continuity between frame windows");
    work::RecordFrame(160,17);work::RecordFrame(180,19);const auto backward=work::Take();
    Require(backward.frameSampleCount==2 && backward.frameIntervalNS[0]==0 && backward.frameIntervalNS[1]==20,
            "C0 backward clock underflowed or poisoned following interval");
    work::RecordFrame(200,23);work::ResetFrameHistory();work::RecordFrame(500,29);
    const auto reset=work::Take();
    Require(reset.frameSampleCount==1 && reset.frameIntervalNS[0]==0 && reset.frameCpuWallNS[0]==29,
            "C0 ResetFrameHistory retained old samples or frame cadence");
    work::RecordFrame(510,31);work::SetEnabled(false);work::RecordFrame(900,37);
    Require(work::Take().frameSampleCount==0,"C0 disable retained or recorded frame samples");
    work::SetEnabled(true);work::RecordFrame(1000,41);const auto session=work::Take();
    Require(session.frameSampleCount==1 && session.frameIntervalNS[0]==0,"C0 generation/session change retained prior frame timestamp");
    work::ResetFrameHistory();
    for(uint64_t i=0;i<work::MaxFrameSamples+3;++i) work::RecordFrame(2000+i*10,i+1);
    const auto full=work::Take();
    Require(full.frameSampleCount==2048 && full.frameSamplesDropped==3 && full.frameIntervalNS[0]==0 &&
            full.frameIntervalNS[2047]==10 && full.frameCpuWallNS[2047]==2048,"C0 frame window exceeded bound or lost dropped count");
    work::RecordFrame(2000+(work::MaxFrameSamples+3)*10,99);const auto afterDrop=work::Take();
    Require(afterDrop.frameSampleCount==1 && afterDrop.frameIntervalNS[0]==10 && afterDrop.frameSamplesDropped==0,
            "C0 dropped frames did not advance last timestamp or drop count leaked across Take");
    std::thread worker([] {work::RecordFrame(999999,100);});worker.join();
    Require(work::Take().frameSampleCount==0,"C0 worker thread recorded runtime frame samples");
    work::RecordFrame(0,3);work::RecordFrame(10,5);const auto zero=work::Take();
    Require(zero.frameSampleCount==2 && zero.frameIntervalNS[0]==0 && zero.frameIntervalNS[1]==10,
            "C0 zero/backward timestamp was treated as uninitialized on the next frame");
}
}

void C0ProfileTests() {
    AggregationAndCapacity();EncodingAndBounds();ScopeAndEpochs();Frames();work::SetEnabled(false);
    std::cout<<"PASS C0 collector: origin/overflow totals, protected capacity, escaping, epochs and runtime frame windows\n";
}
