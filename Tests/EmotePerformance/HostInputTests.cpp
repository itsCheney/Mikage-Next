#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

using Uint32 = std::uint32_t;
enum : Uint32 {
    SDL_EVENT_FIRST=0, SDL_EVENT_QUIT=0x100, SDL_EVENT_WINDOW=0x200,
    SDL_EVENT_KEY=0x300, SDL_EVENT_MOUSE_MOTION=0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP, SDL_EVENT_MOUSE_WHEEL,
    SDL_EVENT_MOUSE_DEVICE,
    SDL_EVENT_FINGER_DOWN=0x700, SDL_EVENT_FINGER_UP, SDL_EVENT_FINGER_MOTION,
    SDL_EVENT_LAST=0xffff
};
enum { SDL_GETEVENT=1 };
struct SDL_Event { Uint32 type; int sequence; };
static std::deque<SDL_Event> queue;
static bool backpressure=false;
static int polls=0, pumps=0;
bool TVPHasPendingLayerPointerBackpressure() { return backpressure; }
bool SDL_PollEvent(SDL_Event* event) {
    ++polls;
    if(queue.empty()) return false;
    *event=queue.front(); queue.pop_front(); return true;
}
void SDL_PumpEvents() { ++pumps; }
int SDL_PeepEvents(SDL_Event* event,int count,int action,Uint32 first,Uint32 last) {
    if(count!=1 || action!=SDL_GETEVENT) throw std::runtime_error("unexpected SDL queue operation");
    for(auto it=queue.begin();it!=queue.end();++it) if(it->type>=first && it->type<=last) {
        *event=*it; queue.erase(it); return 1;
    }
    return 0;
}
#include "ProductionHostInput.inc"
static int checks=0;
void Require(bool condition,const char* message) {
    ++checks; if(!condition) throw std::runtime_error(message);
}
int main() {
    try {
        SDL_Event event{};
        queue={{SDL_EVENT_MOUSE_BUTTON_DOWN,1},{SDL_EVENT_MOUSE_MOTION,2},
               {SDL_EVENT_KEY,3},{SDL_EVENT_FINGER_DOWN,4},{SDL_EVENT_QUIT,5},
               {SDL_EVENT_MOUSE_BUTTON_UP,6},{SDL_EVENT_MOUSE_DEVICE,7},
               {SDL_EVENT_WINDOW,8},{SDL_EVENT_FINGER_UP,9},{SDL_EVENT_FINGER_MOTION,10},
               {SDL_EVENT_MOUSE_WHEEL,11}};
        const auto original=queue;
        backpressure=true;
        std::vector<int> system;
        while(pollKRKREvent(&event)) system.push_back(event.sequence);
        Require(polls==0 && pumps>0,"pressure does not remove pointer events through PollEvent");
        std::sort(system.begin(),system.end());
        Require(system==std::vector<int>({3,5,7,8}),"quit, window, keyboard and device events remain serviceable");
        std::vector<int> expected;
        for(const auto& item:original)
            if(item.type!=SDL_EVENT_KEY && item.type!=SDL_EVENT_QUIT &&
               item.type!=SDL_EVENT_MOUSE_DEVICE && item.type!=SDL_EVENT_WINDOW)
                expected.push_back(item.sequence);
        backpressure=false;
        std::vector<int> pointer;
        while(pollKRKREvent(&event)) pointer.push_back(event.sequence);
        Require(pointer==expected,"all down/move/up/wheel events resume in original FIFO order");
        Require(queue.empty(),"pressure leaves no duplicated events");
        for(int iteration=0;iteration<128;++iteration) {
            queue={{SDL_EVENT_MOUSE_BUTTON_DOWN,1},{SDL_EVENT_MOUSE_BUTTON_UP,2}};
            backpressure=true;
            Require(!pollKRKREvent(&event) && queue.size()==2,"blocked click chain is retained");
            backpressure=false;
            Require(pollKRKREvent(&event) && event.sequence==1,"down resumes first");
            Require(pollKRKREvent(&event) && event.sequence==2,"up resumes once after down");
        }
        std::cout<<"Passed "<<checks<<" production host input backpressure checks.\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
