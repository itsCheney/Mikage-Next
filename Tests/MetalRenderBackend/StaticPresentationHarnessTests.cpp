#include "backend/StaticPresentation.h"
#include "LayerRenderOperation.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace presentation_fixture {
enum class Mode { Display, NoAcknowledgement, Unsupported, CommandFailure, ProbeFailure, SkipFailure, TicketFailure };
Mode mode=Mode::Display;
unsigned frames=0;uint64_t clockMS=1;
struct FixtureClock {
    static std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(clockMS));
    }
};
struct Ticket {
    std::atomic<bool> presented{false},failed{false};
    uint64_t serial=0;
};
struct SDL_Window{};union SDL_Event {unsigned type;};
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool SDL_GetHintBoolean(const char*,bool fallback){return fallback;}
bool SDL_SetHint(const char*,const char*){return true;}
bool SDL_ShowWindow(SDL_Window*){return true;}bool SDL_HideWindow(SDL_Window*){return true;}
bool SDL_PollEvent(SDL_Event*){return false;}void SDL_Delay(unsigned ms){clockMS+=ms;}
class Backend;
Backend* active=nullptr;
class Backend {
    ::krkrsdl3::static_presentation::Policy policy;
    std::vector<uint8_t> pixels;
    uint64_t mutation=0,nextTicket=0;
    std::shared_ptr<Ticket> ticket;
    struct Delivery {std::shared_ptr<::krkrsdl3::static_presentation::Record> record;std::shared_ptr<Ticket> ticket;};
    std::vector<Delivery> deliveries;
public:
    Backend(){active=this;}virtual ~Backend(){active=nullptr;}
    static Backend* Create(SDL_Window*,bool){return new Backend;}
    void* CreateLayerTexture(int,int,TVPLayerTextureFormat){return this;}
    void DestroyLayerTexture(void*){}
    bool UpdateLayerTexture(void*,const uint8_t* bytes,int,const TVPLayerRect&){pixels.assign(bytes,bytes+64);++mutation;return true;}
    bool ReadLayerTexture(void*,std::vector<uint8_t>& out,int& pitch){out=pixels;pitch=16;return mode!=Mode::CommandFailure;}
    void BeginFrame(int,int){}void DrawWindowTexture(void*,float,float,float,float){}
    auto GetStaticPresentationStats()const{return policy.Stats();}
    std::shared_ptr<Ticket> GetCurrentLayerPresentation(){ticket=std::make_shared<Ticket>();ticket->serial=++nextTicket;return ticket;}
    void EndFrame() {
        ++frames;
        ::krkrsdl3::static_presentation::Signature signature;
        signature.width=signature.height=256;signature.epoch=policy.Epoch();
        if(mode==Mode::SkipFailure && frames>1)++mutation;
        signature.windows.push_back({1,mutation,4,4,0,0,256,256});
        if(!policy.CanSkip(signature,true,mode!=Mode::Unsupported,false,bool(ticket))) {
            auto record=policy.Track(std::move(signature));
            if(record)record->commandSucceeded=true;
            deliveries.push_back({record,ticket});
        }
        ticket.reset();
    }
    void Deliver() {
        if(mode==Mode::NoAcknowledgement || mode==Mode::Unsupported)return;
        for(auto& d:deliveries) {
            const bool fail=mode==Mode::ProbeFailure || (mode==Mode::TicketFailure && d.ticket && d.ticket->serial>1);
            if(d.record){if(fail)d.record->failed=true;else d.record->displayed=true;}
            if(d.ticket){if(fail)d.ticket->failed=true;else d.ticket->presented=true;}
        }
        deliveries.clear();
    }
    void InvalidatePresentation(const char*){policy.Invalidate();}
    bool CaptureFrame(std::vector<uint8_t>& out,int& width,int& height,int& pitch){
        policy.Invalidate();out=pixels;width=height=256;pitch=1024;return true;
    }
};
void SDL_PumpEvents(){if(active)active->Deliver();}
using iTVPRenderBackend=Backend;
namespace krkrsdl3 {
using MetalRenderBackend=Backend;
using AsyncLayerPresentation=Ticket;
namespace static_presentation=::krkrsdl3::static_presentation;
}
#include "ProductionStaticPresentationTest.inc"
}

void StaticPresentationHarnessTests() {
    using namespace presentation_fixture;
    struct CaptureOutput {
        std::ostringstream out,err;
        std::streambuf* savedOut=std::cout.rdbuf(out.rdbuf());
        std::streambuf* savedErr=std::cerr.rdbuf(err.rdbuf());
        ~CaptureOutput(){std::cout.rdbuf(savedOut);std::cerr.rdbuf(savedErr);}
    };
    for(auto testMode:{Mode::Display,Mode::NoAcknowledgement,Mode::Unsupported,Mode::CommandFailure,
                      Mode::ProbeFailure,Mode::SkipFailure,Mode::TicketFailure}) {
        mode=testMode;frames=0;clockMS=1;SDL_Window window;
        bool result=false,failed=false;std::string error;
        // Keep the main test's stdout machine readable while observing all
        // native fixture failures, including failures after the first display.
        {
            CaptureOutput capture;
            try {result=NativeStaticPresentationTests(&window);}
            catch(const std::runtime_error& e){failed=true;error=e.what();}
        }
        if(testMode==Mode::Display)Require(result && !failed,"displayed native fixture must pass");
        else if(testMode==Mode::NoAcknowledgement || testMode==Mode::Unsupported) {
            Require(!result && !failed && frames==1,"unavailable display must skip without flooding presents");
        } else {
            Require(failed && !result,"GPU/probe/post-display regression must remain a failure");
            if(testMode==Mode::SkipFailure)Require(error=="unchanged frame never confirmed/skipped","skip regression misclassified");
            if(testMode==Mode::TicketFailure)Require(error=="fresh ticket lacked display proof","new ticket failure misclassified");
        }
    }
}
