// Minimal services for compiling the unmodified production cache/lifecycle code.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ttstr = std::string;
using tjs_int = int;
using tjs_uint32 = uint32_t;
#define TJS_N(value) value
constexpr int TVP_COMPACT_LEVEL_DEACTIVATE = 10;
constexpr int TVP_COMPACT_LEVEL_MAX = 100;
void TVPAddLog(const char*) {}

struct tTVPCompactEventCallbackIntf {
    virtual void OnCompact(tjs_int) = 0;
    virtual ~tTVPCompactEventCallbackIntf() = default;
};
std::vector<tTVPCompactEventCallbackIntf*> TVPCompactEventVector;
std::vector<tTVPCompactEventCallbackIntf*> TVPPersistentCompactEventVector;
void TVPAddCompactEventHook(tTVPCompactEventCallbackIntf*, bool persistent = false);

// Unrelated event queues and native objects are empty in this isolated test.
std::vector<int> TVPWinUpdateEventQueue, TVPContinuousEventVector, NativeClassNames;
bool TVPContinuousEventProcessing = false, TVPProcessContinuousHandlerEventFlag = false;
bool TVPExclusiveEventPosted = false, TVPEventDisabled = false, TVPEventInterrupting = false;
int TVPEventSequenceNumber = 0, TVPEventSequenceNumberToProcess = 0;
void TVPDestroyEventQueue() {}
void TVPDestroyContinuousHandlerVector() {}
void TVPEndContinuousEvent() {}
struct tTJSDispatch {};
struct EmptyRegisterPool {
    template<class Callback> void forEach(Callback) {}
    void clear() {}
} _allTJSRegister;
bool _tjsForceShutdown = false;
using _TJSStaticClearFun = void(void*);
std::vector<std::pair<_TJSStaticClearFun*, void*>> _allTJSStaticClearFun;

std::map<std::string, std::string> gameFiles;
int fileReads = 0, liveScenarios = 0;
struct Scenario {
    int refs = 1;
    std::string contents;
    Scenario(const ttstr& name, bool isString) {
        contents = isString ? name : gameFiles.at(name);
        if (!isString) ++fileReads;
        ++liveScenarios;
    }
    ~Scenario() { --liveScenarios; }
    void AddRef() { ++refs; }
    void Release() { if (--refs == 0) delete this; }
};
using tTVPScenarioCacheItem = Scenario;
using tTVPScenarioCacheItemEX = Scenario;
using tExtTVPScenarioCacheItem = Scenario;
template<class T> class tTJSRefHolder {
    T* value;
public:
    explicit tTJSRefHolder(T* p) : value(p) { value->AddRef(); }
    tTJSRefHolder(const tTJSRefHolder& other) : value(other.value) { value->AddRef(); }
    ~tTJSRefHolder() { value->Release(); }
    T* GetObject() { value->AddRef(); return value; }
};
template<class T> struct tTJSHashFunc {};
template<class K, class V, class H, int Capacity> class tTJSHashCache {
    std::map<K, V> entries;
public:
    explicit tTJSHashCache(int) {}
    void Clear() { entries.clear(); }
    static tjs_uint32 MakeHash(const K& key) { return std::hash<K>{}(key); }
    V* FindAndTouchWithHash(const K& key, tjs_uint32) {
        auto found = entries.find(key);
        return found == entries.end() ? nullptr : &found->second;
    }
    void AddWithHash(const K& key, tjs_uint32, const V& value) { entries.emplace(key, value); }
};
namespace TJSEX {
using ::tTJSHashFunc;
using ::tTJSHashCache;
}
