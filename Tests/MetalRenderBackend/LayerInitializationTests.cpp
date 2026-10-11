#include "backend/LayerInitialization.h"
#include <cmath>
#include <stdexcept>

void LayerInitializationTests() {
    using namespace krkrsdl3::layer_initialization;
    const auto require=[](bool value) { if(!value) throw std::runtime_error("Layer logical-zero initialization contract"); };
    State generic;
    require(!generic.Pending() && !generic.NeedsClearBeforeWrite(false));
    State pending(true);
    require(pending.Pending() && pending.NeedsClearBeforeWrite(false));
    require(!pending.NeedsClearBeforeWrite(true) && pending.Pending());
    // Planning, rejection and partial writes cannot discharge initialization.
    pending.EncodedWrite(false);
    require(pending.Pending());
    pending.EncodedWrite(true);
    require(!pending.Pending() && !pending.NeedsClearBeforeWrite(false));
    pending=State(true); pending.EncodedClear();
    require(!pending.Pending());
    require(FullSurface(7,5,0,0,7,5));
    require(!FullSurface(7,5,1,0,7,5) && !FullSurface(7,5,0,0,8,5));
    require(!FullSurface(0,5,0,0,0,5));
    require(CanLoadClearFill(true,0,true,false,7,5,0,0,7,5));
    require(!CanLoadClearFill(false,0,true,false,7,5,0,0,7,5));
    require(!CanLoadClearFill(true,1,true,false,7,5,0,0,7,5));
    require(!CanLoadClearFill(true,0,false,false,7,5,0,0,7,5));
    require(!CanLoadClearFill(true,0,true,true,7,5,0,0,7,5));
    require(!CanLoadClearFill(true,0,true,false,7,5,-1,0,7,5));
    for(unsigned alpha=0;alpha<256;++alpha) {
        const uint32_t color=(alpha<<24)|((255-alpha)<<16)|((alpha^0x5a)<<8)|alpha;
        for(unsigned channel=0;channel<4;++channel)
            require(std::lround(ClearChannel(color,channel)*255.0)==long((color>>(channel*8))&255));
    }
}
