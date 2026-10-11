// SPDX-License-Identifier: GPL-3.0-or-later
// These resource/host fixtures do not copy the production animation or binding logic.
#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "ncbind/ncbind.hpp"
#include "emoteanimation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
using namespace TJS;
static tTJS* vm = nullptr;
static bool integrated = true;
using Uint64 = std::uint64_t;
const char* SDL_GetHint(const char*) { return integrated ? "integrated" : "legacy"; }
bool SDL_GetHintBoolean(const char*, bool) { return false; }
Uint64 SDL_GetTicks() { return 0; }
Uint64 SDL_GetTicksNS() { return 0; }
void TVPConsoleLog(const char*, ...) {}
void TVPAddLog(const ttstr&) {}
iTJSDispatch2* TVPGetScriptDispatch() { auto* global = vm->GetGlobalNoAddRef(); global->AddRef(); return global; }
void TVPThrowExceptionMessage(const tjs_char* message) { throw std::runtime_error(ttstr(message).AsStdString()); }
void TVPExecuteExpression(const ttstr& expression, tTJSVariant* result) { vm->EvalExpression(expression, result); }
tjs_uint64 TVPGetRoughTickCount() { return 0; }
namespace krkrsdl3 { struct iTVPRenderBackend { void* CreateTarget(int,int) { return nullptr; } }; void TVPRecordEmoteProgress(Uint64) {} }

namespace emoteplayer {
struct emoteRender {}; struct emotelimit { double width = 100, height = 100, originX = 0, originY = 0; };
class emoteengine;
struct emotemotion;
struct emotenoderef;
struct emotemotionref { emotemotion* currentMotion=nullptr; emoteengine* refTop=nullptr; emotenoderef* parent=nullptr; float getTickByIdx(int32_t); };
struct NodeFixture { std::string label; };
struct emotenoderef { emotemotionref* refMtn=nullptr; NodeFixture* currentNode=nullptr; };
#include "ProductionParameter.inc"
struct emotemotion { double lastTime = 5, syncTime = 5, selfSyncTime = 5, loopTime = -1; std::vector<emoteVar*> parameter; std::map<std::string, double> parameterCache; struct emotefile* _filePtr = nullptr; };
struct emoteobject { std::map<std::string, emotemotion*> motion; };
struct eyeControl {
    float beginFrame=0, endFrame=0, lastTick=0, baseVal=0;
    int blinkFrameCount=0, currWaitInterval=-1;
    bool hasStart=false, isBlinking=false;
    std::string label; std::uniform_int_distribution<int> uid;
};
struct Option { std::string label; double offValue=0, onValue=1; };
struct Selector { std::string lable; std::vector<Option> selectItem; };
struct emoteTimeVarFrame { double time=0, value=0, easing=0; bool hasContent=false; int type=0; };
struct emoteTimeVar { std::string label; std::vector<emoteTimeVarFrame*> frameList; };
struct emotetimeline {
    std::string label; int loopBegin=-1, loopEnd=-1, lastTime=-1, diff=0;
    std::vector<emoteTimeVar*> variableList;
};
struct Metadata {
    std::map<std::string,float> _varList;
    std::vector<emotetimeline*> _timelineControl;
    std::vector<Selector*> _selectorControl;
    std::vector<std::string> _instantVariableList;
    std::vector<eyeControl*> _eyeControl;
};
struct emotefile {
    Metadata* _metadata=nullptr; bool isMotion=false;
    std::map<std::string, emoteobject*> _objects;
    void setVariable(const std::string& label, double value) { _metadata->_varList[label] = value; }
    bool getTickByName(const std::string&,float&);
};
#include "ProductionEngineDeclaration.inc"
emoteengine::~emoteengine() = default;
class EmotePlayer {
public:
    emoteengine emtEngine;
    bool _isStop=false, _playing=true, _allplaying=true, isMotion=false;
    int _pipoVal=0; double clockPassed=0, speedRatio=20;
    double currCoordx=0,currCoordy=0,currCoordz=0,currAngle=0,currZx=1,currZy=1;
    emotelimit _limitArea;
    void AddRef() {} void Release() {}
    bool usesIntegratedAnimation() const { return emtEngine.integratedAnimation(); }
    void setVariable(tTJSString, tjs_real, tjs_real=0, tjs_real=0);
    static tjs_error cb_setVariable(tTJSVariant*,tjs_int,tTJSVariant**,EmotePlayer*);
    tjs_real getVariable(tTJSString);
    void progress(tjs_real);
    void playTimeline(tTJSString,tjs_int=0); void stopTimeline(tTJSString);
    bool getTimelinePlaying(tTJSString); bool getLoopTimeline(tTJSString);
    tjs_real getTimelineTotalFrameCount(tTJSString);
    void setTimelineBlendRatio(tTJSString,tjs_real,tjs_real,tjs_real);
    void fadeInTimeline(tTJSString,tjs_real,tjs_real); void fadeOutTimeline(tTJSString,tjs_real,tjs_real);
    tTJSVariant getPlayingTimelineInfoList();
    tTJSVariant serialize(); void unserialize(tTJSVariant);
    void copyAnimationStateFrom(const EmotePlayer&);
    #include "ProductionClockMembers.inc"
    tjs_real get_speed() { return speedRatio; } void set_speed(tjs_real value) { speedRatio=value; }
    tTJSString get_motionKey() { return TJS_N("fixture"); } void set_motionKey(tTJSString) {}
    tTJSString get_motion() { return TJS_N("fixture"); }
    void play(tTJSString,int) { clockPassed=0; _playing=true; }
    tTJSVariant get_variableKeys() { return {}; }
};
#define setprop(dict, name) { tTJSVariant v(name); dict->PropSet(TJS_MEMBERENSURE,TJS_N(#name),nullptr,&v,dict); }
#define getprop_t(dict, name, cast) { tTJSVariant v; if(TJS_SUCCEEDED(dict->PropGet(0,TJS_N(#name),nullptr,&v,dict))) name=cast(v.AsReal()); }
#include "ProductionIntegration.inc"
}
using emoteplayer::EmotePlayer;
#define NCB_MODULE_NAME TJS_N("emoteplayer-test.dll")
#include "ProductionRegistration.inc"
namespace drawdevice {
struct LayerFixture { void* EmoteTarget=nullptr; void* EmoteMaskTarget=nullptr; };
struct DeviceFixture { krkrsdl3::iTVPRenderBackend* GetBackend() { return nullptr; } int GetWidth() { return 100; } int GetHeight() { return 100; } };
struct PlayerImpl { EmotePlayer* Player=nullptr; void* RM=nullptr; };
class D3DEmotePlayer {
public:
    LayerFixture* Layer=nullptr; DeviceFixture* Device=nullptr;
    PlayerImpl* Impl=nullptr; bool ownsPlayer=false;
    bool ShowFlag=false,Smoothing=true,Animating=false;
    double MeshDivisionRatio=1,BustScale=1,HairScale=1,PartsScale=1;
    D3DEmotePlayer() = default;
    D3DEmotePlayer(iTJSDispatch2*,void* rm,EmotePlayer* source) {
        Impl=new PlayerImpl{new EmotePlayer(),rm}; ownsPlayer=true;
        Impl->Player->emtEngine._mainfile=source->emtEngine._mainfile;
        Impl->Player->emtEngine._mainmotion=source->emtEngine._mainmotion;
    }
    ~D3DEmotePlayer() { if(ownsPlayer) { delete Impl->Player; delete Impl; } }
    void setVariable(tTJSString,double,double=0,double=0);
    void progress(double); tTJSVariant clone(iTJSDispatch2*);
    void DrawToTarget(void*,void*) {}
};
#include "ProductionD3D.inc"
}
using drawdevice::D3DEmotePlayer;
#include "ProductionD3DRegistration.inc"
static int checks=0;
void require(bool condition, const char* message) { ++checks; if(!condition) throw std::runtime_error(message); }
void near(double actual,double expected,const char* message) { require(std::abs(actual-expected)<1e-8,message); }
std::string label(const char* text) { std::string result(text); result+='\0'; return result; }
struct Fixture {
    emoteplayer::emoteTimeVarFrame first{0,60,0,true,3}, end{61,0,0,false,0};
    emoteplayer::emoteTimeVar track{label("x"),{&first,&end}};
    emoteplayer::emotetimeline timeline{label("idle"),0,61,61,0,{&track}};
    emoteplayer::Metadata metadata{{{label("x"),0}},{&timeline},{},{},{}};
    emoteplayer::emotefile file{&metadata};
    emoteplayer::emotemotion motion;
    EmotePlayer player;
    Fixture() { player.emtEngine._mainfile=&file; player.emtEngine._mainmotion=&motion; }
};
int main() {
    tTJS engine; vm = &engine;
    try {
        ncbAutoRegister::AllRegist();
        Fixture a; a.player.playTimeline(TJS_N("idle"),1); a.player.progress(1000.0/60);
        near(a.player.getVariable(TJS_N("x")),1,"adapter converts milliseconds to authored frames");
        near(a.metadata._varList.at(label("x")),0,"resource metadata remains immutable");
        emoteplayer::emoteVar parameter; parameter.id=label("x"); parameter.rangeBegin=0; parameter.rangeEnd=10; parameter.division=100;
        a.motion.parameter.push_back(&parameter); a.motion._filePtr=&a.file;
        emoteplayer::emoteobject objectDefinition; objectDefinition.motion[label("model")]=&a.motion;
        a.file._objects[label("object")]=&objectDefinition;
        emoteplayer::emotemotionref node; node.currentMotion=&a.motion; node.refTop=&a.player.emtEngine;
        near(node.getTickByIdx(0),10,"render parameter reads the instance's animated value");
        a.player.setVariable(TJS_N("object/x"),7);
        near(node.getTickByIdx(0),70,"scoped instance variable reaches production node lookup");
        near(a.metadata._varList.at(label("x")),0,"scoped lookup does not mutate shared defaults");
        a.file._objects.clear(); a.motion.parameter.clear();
        a.player.progress(100); require(!a.player._playing,"main motion still finishes");
        const double before=a.player.getVariable(TJS_N("x")); a.player.progress(1000.0/60);
        require(a.player.getVariable(TJS_N("x"))>before,"control timeline continues after main motion ends");
        Fixture b; b.player.copyAnimationStateFrom(a.player); b.player.setVariable(TJS_N("manual"),5);
        near(a.player.getVariable(TJS_N("manual")),0,"clone variables are isolated");
        const auto saved=a.player.serialize(); b.player.unserialize(saved);
        near(b.player.getVariable(TJS_N("x")),a.player.getVariable(TJS_N("x")),"TJS snapshot restores pending timeline");
        const auto pausedAt=b.player.emtEngine._animationClock; b.player.set_playing(false); b.player.progress(100);
        near(b.player.emtEngine._animationClock,pausedAt,"explicit pause stops controls");
        b.player.set_playing(true); b.player.progress(1000.0/60);
        near(b.player.emtEngine._animationClock,pausedAt+1,"explicit resume restores controls");
        b.player.set_tickCount(2); near(b.player.getVariable(TJS_N("x")),2,"public seek reconstructs primary track");
        b.player.set_tickCount(std::numeric_limits<double>::quiet_NaN()); near(b.player.get_tickCount(),2,"invalid seek is rejected");
        emoteplayer::eyeControl eye; eye.hasStart=true; eye.isBlinking=true; eye.lastTick=2; eye.baseVal=10; eye.currWaitInterval=4;
        b.player.emtEngine._animationEyes.push_back(eye);
        const auto eyeSnapshot=b.player.emtEngine.serializeAnimationState(); b.player.emtEngine._animationEyes[0].baseVal=0;
        require(b.player.emtEngine.restoreAnimationState(eyeSnapshot),"eye state snapshot restores");
        near(b.player.emtEngine._animationEyes[0].baseVal,10,"eye base and phase survive save restore");
        require(!b.player.emtEngine.restoreAnimationState(eyeSnapshot+"bad"),"engine snapshot validates trailing data");
        b.player.emtEngine._animationEyes.clear(); b.player.emtEngine._animationEyeRefs.clear();
        a.player.fadeOutTimeline(TJS_N("idle"),5,0);
        require(a.player.getTimelinePlaying(TJS_N("idle")),"wrapper fade is not immediate stop");
        auto info=a.player.getPlayingTimelineInfoList(); tTJSVariant count;
        info.AsObjectNoAddRef()->PropGet(0,TJS_N("count"),nullptr,&count,info.AsObjectNoAddRef());
        require(count.AsInteger()==1,"playing info exposes integrated timelines");
        require(a.player.getLoopTimeline(TJS_N("idle")),"loop query uses bounds");
        a.player.progress(100); require(!a.player.getTimelinePlaying(TJS_N("idle")),"wrapper fade eventually stops");
        // Register the exact production raw-callback line against the real ncbind/TJS VM.
        auto* facade = ncbInstanceAdaptor<EmotePlayer>::CreateAdaptor(new EmotePlayer(),false,true);
        auto* native=ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(facade,true);
        native->emtEngine._mainfile=&b.file;
        tTJSVariant object(facade,facade); facade->Release();
        auto* global=vm->GetGlobalNoAddRef(); global->PropSet(TJS_MEMBERENSURE,TJS_N("p"),nullptr,&object,global);
        vm->ExecScript(TJS_N("p.setVariable('value',10,10,1);")); native->emtEngine._animation.advance(5);
        near(native->getVariable(TJS_N("value")),2.5,"registered four-argument callback forwards easing");
        vm->ExecScript(TJS_N("p.setVariable('value',7);")); near(native->getVariable(TJS_N("value")),7,"two arguments still work");
        vm->ExecScript(TJS_N("p.setVariable('value',9,void,void);")); near(native->getVariable(TJS_N("value")),9,"void optional arguments default to zero");
        vm->ExecScript(TJS_N("p.queuing=true; p.setVariable('value',10,1); p.setVariable('value',20,1);"));
        native->emtEngine._animation.advance(1.5); near(native->getVariable(TJS_N("value")),15,"registered queuing property appends transitions");
        tTJSVariant value(1); tTJSVariant* args[]={&value};
        require(facade->FuncCall(0,TJS_N("setVariable"),nullptr,nullptr,1,args,facade)==TJS_E_BADPARAMCOUNT,"binding rejects missing value");
        // Native GPU/window construction is replaced; parameter forwarding and clone bodies are production code.
        auto* d3dFacade=ncbInstanceAdaptor<D3DEmotePlayer>::CreateAdaptor(new D3DEmotePlayer(),false,true);
        auto* d3d=ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(d3dFacade,true);
        drawdevice::PlayerImpl impl{&b.player,nullptr}; d3d->Impl=&impl;
        tTJSVariant d3dObject(d3dFacade,d3dFacade); d3dFacade->Release(); global->PropSet(TJS_MEMBERENSURE,TJS_N("d"),nullptr,&d3dObject,global);
        vm->ExecScript(TJS_N("d.setVariable('d3d',10,10,1); d.progress(5);"));
        near(b.player.getVariable(TJS_N("d3d")),2.5,"registered D3D forwards transition and frame units");
        integrated=false;
        auto clone=d3d->clone(nullptr); auto* cloned=ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(clone.AsObjectNoAddRef(),true);
        require(cloned->Impl->Player->usesIntegratedAnimation(),"clone inherits source mode despite changed environment");
        integrated=true;
        near(cloned->Impl->Player->getVariable(TJS_N("d3d")),2.5,"D3D clone retains pending transition");
        cloned->progress(1); near(cloned->Impl->Player->getVariable(TJS_N("d3d")),3.6,"D3D clone advances same easing independently");
        near(b.player.getVariable(TJS_N("d3d")),2.5,"source does not advance with D3D clone");
        clone.Clear(); global->DeleteMember(0,TJS_N("d"),nullptr,global); d3dObject.Clear();
        integrated=false; Fixture legacy; legacy.player.setVariable(TJS_N("x"),12,30,1);
        near(legacy.metadata._varList.at(label("x")),12,"legacy mode retains immediate writes");
        legacy.player.playTimeline(TJS_N("idle"),1); require(legacy.player.emtEngine.currTimeline.size()==1,"legacy path remains selectable");
        integrated=true;
        global->DeleteMember(0,TJS_N("p"),nullptr,global); object.Clear(); ncbAutoRegister::AllUnregist();
        std::cout << "Passed " << checks << " production adapter/TJS checks.\n";
    } catch(const eTJS& error) { std::cerr << error.GetMessage().AsStdString()<<'\n'; return 1; }
      catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
