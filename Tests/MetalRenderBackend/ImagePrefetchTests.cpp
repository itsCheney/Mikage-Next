#include "ImagePrefetchPolicy.h"
#include <stdexcept>
#include <string>
void ImagePrefetchTests() {
    using namespace krkrsdl3::image_prefetch;
    const auto check=[](bool value){if(!value)throw std::runtime_error("image prefetch policy");};
    Policy<std::string> policy;
    auto budget=std::make_shared<Budget>(1024,1000);
    check(policy.CanAdmit(budget,1));
    auto first=policy.Enqueue("a.png",budget,1);check(first.id && policy.Current(first,2));
    check(!policy.Enqueue("a.png",budget,2).id);
    check(policy.Reserve(first,512,2));check(!policy.Reserve(first,513,3));
    check(!policy.Current(first,1000));
    Cancel();check(!policy.Current(first,4));check(!policy.Reserve(first,1,4));
    // Stale commands retain their slot until drained, including across resets.
    for(int i=0;i<31;++i)check(policy.Enqueue(std::to_string(i),budget,4).id);
    check(!policy.Enqueue("overflow",budget,4).id);
    check(!policy.CanAdmit(budget,4));
    policy.Finish(first);auto next=policy.Enqueue("a.png",budget,4);check(next.id && next.id!=first.id);
    policy.Finish(first);check(!policy.Enqueue("a.png",budget,4).id);
    check(policy.Reserve(next,512,5));check(!policy.Enqueue("empty",budget,6).id);
}
