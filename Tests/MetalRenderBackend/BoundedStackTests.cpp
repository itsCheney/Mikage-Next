#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using tjs_int=int;using tjs_uint=unsigned;using tjs_char=char16_t;
#define TJS_N(s) u##s
namespace {
struct Block {
    std::u16string name;
    const tjs_char* GetName() const {return name.c_str();}
    int SrcPosToLine(int position) const {return position;}
};
struct FakeContext {
    Block block;
    Block* GetBlock(){return &block;}
    int CodePosToSrcPos(int position) const {return position;}
    const tjs_char* GetName() const {return u"nested";}
};
struct Record {FakeContext* Context;const int* CodeBase;int* const* CodePtr;bool InTry=false;
    int SavedPosition=0;bool HasSavedPosition=false;};
struct Tracer {
    std::vector<Record> Stack;
    #include "ProductionBoundedStack.inc"
};
}
void BoundedStackTests() {
    const auto check=[](bool v){if(!v)throw std::runtime_error("production bounded stack");};
    FakeContext c;c.block.name=u"scene.ks";int instructions[8]{};int* current=instructions+3;
    Tracer tracer;
    for(int i=0;i<8;++i)tracer.Stack.push_back({&c,instructions,&current});
    char out[514];std::memset(out,0x7f,sizeof(out));
    check(tracer.GetTraceUTF8(out,513,100));std::string value(out);
    check(value.find("scene.ks:4[nested]")!=std::string::npos);
    size_t count=0,pos=0;while((pos=value.find("scene.ks",pos))!=std::string::npos){++count;pos+=8;}
    check(count==4 && out[513]==0x7f);
    tracer.FreezeCodePointer();check(!tracer.Stack.back().CodePtr && tracer.Stack.back().SavedPosition==3);
    current=instructions;check(tracer.GetTraceUTF8(out,513,1));check(std::string(out).find(":4[")!=std::string::npos);
    c.block.name=std::u16string(1024,u'\u8857')+u"\U0001f600";
    check(tracer.GetTraceUTF8(out,513,4) && std::strlen(out)<=512 && out[513]==0x7f);
    char tiny[2]={1,2};check(tracer.GetTraceUTF8(tiny,1,4) && tiny[0]==0 && tiny[1]==2);
    check(!tracer.GetTraceUTF8(nullptr,0,4));
}
