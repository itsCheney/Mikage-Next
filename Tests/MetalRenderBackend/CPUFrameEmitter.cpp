#include "CPUFrameDiagnostics.h"
int main(int argc,char**) {
    using namespace krkrsdl3::cpu_frame;
    static uint64_t now=1;clockNS=[](){return now;};rawLine=[](const char* line){std::puts(line);};
    captureStack=[](char* out,size_t,unsigned){std::strcpy(out,"scene.ks:4[hiddenSkip]");return true;};
    SetEnabled(true,1,1);
    const int frames=argc>1 ? 3 : 1;
    for(int frame=0;frame<frames;++frame) {
        StepScope step;CallScope parent;
        {CallScope child;{EventScope phase(Kind::PSBMaterialize);phase.SetWorkBytes(2048);now+=17000000;}now+=17000000;}
        if(frame==2)now+=900000000;
    }
    TakeWindow(1,2);SetEnabled(false,2,3);
}
