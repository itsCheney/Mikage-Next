#include "backend/StaticPresentation.h"
#include "AsyncLayerReadback.h"
#include <stdexcept>
#include <thread>

void StaticPresentationTests() {
    using namespace krkrsdl3::static_presentation;
    const auto require=[](bool ok) { if(!ok) throw std::runtime_error("static presentation policy"); };
    {
        using krkrsdl3::PresentationFailure;
        using krkrsdl3::PresentationCommand;
        krkrsdl3::AsyncLayerPresentation ticket(1);
        ticket.MarkFailed(PresentationFailure::NotDisplayed);
        ticket.MarkCommandCompleted(false);
        ticket.MarkFailed(PresentationFailure::NoDrawable);
        ticket.MarkCommandCompleted(true);
        require(ticket.failed.load() && !ticket.presented.load() &&
            ticket.failureReason.load()==PresentationFailure::CommandFailed &&
            ticket.command.load()==PresentationCommand::Failed);
        krkrsdl3::AsyncLayerPresentation concurrent(2);
        std::thread display([&]{concurrent.MarkFailed(PresentationFailure::NotDisplayed);});
        std::thread command([&]{concurrent.MarkCommandCompleted(false);});
        display.join();command.join();
        require(concurrent.failureReason.load()==PresentationFailure::CommandFailed &&
            concurrent.command.load()==PresentationCommand::Failed);
        krkrsdl3::AsyncLayerReadback read;
        read.presentation=std::make_shared<krkrsdl3::AsyncLayerPresentation>(3);
        read.completed=true;read.presentation->MarkCommandCompleted(true);
        require(!read.Ready()); // command completion is never display proof
        read.presentation->presented=true;require(read.Ready());
        read.presentation->MarkFailed(PresentationFailure::NotDisplayed);require(!read.Ready());
    }
    Policy policy;
    Signature a; a.width=320; a.height=240; a.epoch=policy.Epoch();
    a.windows.push_back({NextResourceID(),0,320,240,0,0,320,240});
    const auto skip=[&](const Signature& s,bool ticket=false) { return policy.CanSkip(s,true,true,false,ticket); };
    require(!skip(a));
    auto first=policy.Track(a); require(first!=nullptr);
    first->commandSucceeded=true; require(!skip(a)); // GPU success is not display proof.
    first->displayed=true; require(skip(a));
    require(!skip(a,true)); // A fresh alpha frame always needs an actual present.
    Signature b=a; ++b.windows[0].mutation;
    auto second=policy.Track(b); require(!skip(a)); // confirmed A, pending B, new A
    second->displayed=true; require(!skip(b));
    second->commandSucceeded=true; require(skip(b) && !skip(a));
    Signature changed=b; changed.windows[0].resource=NextResourceID(); require(!skip(changed));
    changed=b; changed.windows[0].x=1; require(!skip(changed));
    changed=b; ++changed.width; require(!skip(changed));
    changed=b; changed.windows.push_back(a.windows[0]); require(!skip(changed));
    std::swap(changed.windows[0],changed.windows[1]); require(!skip(changed));
    auto failed=policy.Track(b); failed->failed=true; require(!skip(b));
    auto repair=policy.Track(b); repair->displayed=true; repair->commandSucceeded=true; require(skip(b));
    auto stale=policy.Track(b); policy.Invalidate(); b.epoch=policy.Epoch();
    stale->displayed=true; stale->commandSucceeded=true; require(!skip(b));
    auto refresh=policy.Track(b); refresh->displayed=true; refresh->commandSucceeded=true; require(skip(b));
    require(!policy.CanSkip(b,false,true,false,false));
    require(!policy.CanSkip(b,true,false,false,false));
    require(!policy.CanSkip(b,true,true,true,false));
    changed=b; changed.width=0; require(!skip(changed));
    changed=b; changed.windows[0].width=INFINITY; require(!skip(changed));
    for(int i=0;i<16;++i) require(policy.Track(b)!=nullptr);
    require(policy.PendingCount()==16 && !skip(b));
    require(!policy.Track(b) && policy.PendingCount()==16 && !skip(b));
    policy.Invalidate(); require(policy.PendingCount()==0);
    require(policy.Stats().skipped>=4 && policy.Stats().eligible>policy.Stats().skipped);
    // Destruction of the policy does not invalidate its callback state.
    std::shared_ptr<Record> late;
    { Policy temporary; b.epoch=temporary.Epoch(); late=temporary.Track(b); }
    late->displayed=true; late->commandSucceeded=true;
}
