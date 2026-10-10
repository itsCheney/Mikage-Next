#include "CPUFrameDiagnostics.h"
int main() {
    using namespace krkrsdl3::cpu_frame;
    static uint64_t now=1;clockNS=[](){return now;};rawLine=[](const char* line){std::puts(line);};
    captureStack=[](char* out,size_t,unsigned){std::strcpy(out,"scene.ks:4[hiddenSkip]");return true;};
    SetEnabled(true,1,1);
    {StepScope step;CallScope parent;{EventScope scene(Kind::KAGRead);now+=17000000;}now+=17000000;}
    TakeWindow(1,2);SetEnabled(false,2,3);
}
