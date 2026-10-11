#include "LayerUploadBatch.h"
#include <cstdio>
#include <cstdarg>
#include <cstdint>

static bool enabled=false;
static uint64_t now=1000000000;
uint64_t SDL_GetTicksNS(){return now;}
bool SDL_GetHintBoolean(const char*,bool){return enabled;}
void SDL_Log(const char* format,...) {
    va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::putchar('\n');
}
struct Fixture {
    krkrsdl3::layer_upload::Counters uploadCounters;
    uint64_t commandSerial=1,uploadEpoch=42,nextUploadReportNS=0;
    size_t glyphAtlasBytes=1024*1024;
    #include "ProductionUploadReport.inc"
};
int main() {
    Fixture f;
    f.uploadCounters.glyphCalls=3;f.uploadCounters.glyphApplied=2;
    f.uploadCounters.glyphRejected[unsigned(krkrsdl3::layer_upload::GlyphReject::Capacity)]=1;
    f.uploadCounters.glyphBytes=32;f.uploadCounters.atlasPeakBytes=f.glyphAtlasBytes;
    f.uploadCounters.uploadCalls=32;f.uploadCounters.uploadBatches=1;f.uploadCounters.uploadBytes=1024;
    f.ReportUploads(); // disabled must stay silent and not advance the deadline
    enabled=true;f.ReportUploads();f.ReportUploads(); // bounded once per second
    now+=2000000000;f.commandSerial=2;
    f.uploadCounters.glyphCalls=7;f.uploadCounters.glyphApplied=6;f.uploadCounters.glyphBytes=96;
    f.ReportUploads(true);
}
