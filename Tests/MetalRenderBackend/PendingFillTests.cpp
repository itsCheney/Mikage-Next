#include "backend/PendingLayerFill.h"
#include <stdexcept>
void PendingFillTests() {
    using namespace krkrsdl3::pending_fill;
    const auto check=[](bool value){if(!value)throw std::runtime_error("pending Fill coverage policy");};
    TVPLayerOperation fill;fill.kind=TVPLayerOperationKind::Fill;
    const TVPLayerRect full{0,0,8,8},part{2,2,6,6},edge{0,0,3,3};
    check(CanOmit(fill,full,part,8,8,false));
    check(!CanOmit(fill,part,full,8,8,false));
    check(!CanOmit(fill,edge,part,8,8,false));
    fill.flags=1;check(!CanOmit(fill,full,part,8,8,false));fill.flags=0;
    check(!CanOmit(fill,full,part,8,8,true));
    fill.kind=TVPLayerOperationKind::Copy;
    check(CanOmit(fill,full,full,8,8,false));
    check(!CanOmit(fill,part,part,8,8,false));
    check(!CanOmit(fill,full,full,8,8,true));
    fill.kind=TVPLayerOperationKind::CopyOpaque;check(!CanOmit(fill,full,part,8,8,false));
    Slot<int> slot;check(!slot);slot.target=&slot;slot.parameters=42;slot.logicalID=123;
    check(bool(slot));slot.Reset();check(!slot && !slot.logicalID && slot.parameters==42);
}
