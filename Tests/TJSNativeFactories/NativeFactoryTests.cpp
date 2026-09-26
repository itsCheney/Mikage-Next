#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include <chrono>
#include <iostream>
#include <map>
#include <utility>

using namespace TJS;
void TVPConsoleLog(const tjs_char*, ...) {}
tjs_uint64 TVPGetRoughTickCount()
{
    return static_cast<tjs_uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
struct Ref {
    iTJSDispatch2* value;
    explicit Ref(iTJSDispatch2* p) : value(p) {}
    Ref(const Ref&) = delete;
    ~Ref() { if (value) value->Release(); }
    iTJSDispatch2* operator->() const { return value; }
};
tTJSVariant get(iTJSDispatch2* object, const char* name, tjs_uint32 flags = 0)
{
    tTJSVariant result;
    require(TJS_SUCCEEDED(object->PropGet(flags, name, nullptr, &result, object)), "property lookup failed");
    return result;
}
void put(iTJSDispatch2* object, const char* name, const tTJSVariant& value, tjs_uint32 flags = 0)
{
    require(TJS_SUCCEEDED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | flags, name, nullptr, &value, object)),
            "property write failed");
}

class ProbeMember : public tTJSDispatch {
public:
    unsigned propertyCalls = 0, functionCalls = 0;
    iTJSDispatch2* lastThis = nullptr;
    tjs_error PropGet(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant*, iTJSDispatch2*) override {
        ++propertyCalls;
        return TJS_E_FAIL;
    }
    tjs_error FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant* result,
                      tjs_int, tTJSVariant**, iTJSDispatch2* objthis) override {
        ++functionCalls; lastThis = objthis;
        if (result) *result = 7;
        return TJS_S_OK;
    }
};
class CopyProbeClass : public tTJSNativeClass {
public:
    unsigned enumerations = 0;
    explicit CopyProbeClass(bool direct) : tTJSNativeClass(TJS_N("CopyProbe")) {
        SetClassID(TJSRegisterNativeClass(TJS_N("CopyProbe")));
        UseDirectMemberCopy = direct;
    }
    tjs_error EnumMembers(tjs_uint32 flags, tTJSVariantClosure* callback, iTJSDispatch2* objthis) override {
        ++enumerations;
        return tTJSCustomObject::EnumMembers(flags, callback, objthis);
    }
};
class VirtualEnumerationClass : public tTJSNativeClass {
public:
    VirtualEnumerationClass() : tTJSNativeClass(TJS_N("VirtualEnumeration")) {
        SetClassID(TJSRegisterNativeClass(TJS_N("VirtualEnumeration")));
    }
    tjs_error EnumMembers(tjs_uint32, tTJSVariantClosure* callback, iTJSDispatch2*) override {
        tTJSVariant name("fromOverride"), flags(0), value(77), result;
        tTJSVariant* args[]{&name, &flags, &value};
        return callback->FuncCall(0, nullptr, nullptr, &result, 3, args, nullptr);
    }
};
class RecordingObject : public tTJSCustomObject {
public:
    struct Write { std::string name; tjs_uint32 flags; bool fallback; bool operator==(const Write& v) const {
        return name == v.name && flags == v.flags && fallback == v.fallback;
    }};
    std::vector<Write> writes;
    iTJSDispatch2* sourceToMutate = nullptr;
    bool throwOnWrite = false;
    tjs_error PropSetByVS(tjs_uint32 flags, tTJSVariantString* name, const tTJSVariant* value,
                         iTJSDispatch2* objthis) override {
        const auto text = ttstr(name).AsStdString();
        writes.push_back({text, flags, false});
        if (throwOnWrite) throw std::runtime_error("expected write exception");
        if (text == "fallback") return TJS_E_NOTIMPL;
        if (text == "failure") return TJS_E_FAIL;
        if (text == "mutateFallback" && sourceToMutate) {
            sourceToMutate->DeleteMember(0, text.c_str(), nullptr, sourceToMutate);
            return TJS_E_NOTIMPL;
        }
        return tTJSCustomObject::PropSetByVS(flags, name, value, objthis);
    }
    tjs_error PropSet(tjs_uint32 flags, const tjs_char* name, tjs_uint32* hint,
                      const tTJSVariant* value, iTJSDispatch2* objthis) override {
        writes.push_back({name, flags, true});
        return tTJSCustomObject::PropSet(flags, name, hint, value, objthis);
    }
};
void copyOrderFlagsAndFallback()
{
    Ref slow(new CopyProbeClass(false)), fast(new CopyProbeClass(true));
    Ref member(new ProbeMember), bound(new tTJSCustomObject);
    for (auto* source : {slow.value, fast.value}) {
        for (int i = 0; i < 40; ++i) put(source, ("key" + std::to_string(i)).c_str(), tTJSVariant(i));
        put(source, "fallback", tTJSVariant(17), TJS_HIDDENMEMBER);
        put(source, "failure", tTJSVariant(21));
        put(source, "staticOnly", tTJSVariant(member.value), TJS_STATICMEMBER | TJS_HIDDENMEMBER);
        put(source, "unbound", tTJSVariant(member.value));
        put(source, "bound", tTJSVariant(member.value, bound.value));
        put(source, "nullClosure", tTJSVariant(static_cast<iTJSDispatch2*>(nullptr)));
    }
    Ref a(new RecordingObject), b(new RecordingObject);
    require(slow->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, a.value) == TJS_S_OK &&
            fast->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, b.value) == TJS_S_OK,
            "copy changed ignored-setter-failure semantics");
    require(static_cast<RecordingObject*>(a.value)->writes == static_cast<RecordingObject*>(b.value)->writes,
            "member copy order, flags or fallback differed from enumeration path");
    require(static_cast<CopyProbeClass*>(slow.value)->enumerations == 1 &&
            static_cast<CopyProbeClass*>(fast.value)->enumerations == 0,
            "non-opt-in native class lost virtual enumeration");
    require(static_cast<ProbeMember*>(member.value)->propertyCalls == 0, "class member property was evaluated during copy");
    require(get(b.value, "unbound", TJS_IGNOREPROP).AsObjectThisNoAddRef() == b.value,
            "unbound member closure did not bind to the new instance");
    require(get(b.value, "bound", TJS_IGNOREPROP).AsObjectThisNoAddRef() == bound.value,
            "prebound closure was rebound");
    require(get(b.value, "nullClosure", TJS_IGNOREPROP).AsObjectThisNoAddRef() == b.value,
            "null-object closure binding changed");
    tTJSVariant absent;
    require(b->PropGet(TJS_MEMBERMUSTEXIST, "staticOnly", nullptr, &absent, b.value) == TJS_E_MEMBERNOTFOUND,
            "static class member copied into instance");
    Ref virtualClass(new VirtualEnumerationClass), virtualTarget(new tTJSCustomObject);
    require(virtualClass->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, virtualTarget.value) == TJS_S_OK &&
            get(virtualTarget.value, "fromOverride").AsInteger() == 77,
            "non-opt-in virtual enumeration content was bypassed");

    for (bool direct : {false, true}) {
        Ref source(new CopyProbeClass(direct)), target(new RecordingObject);
        put(source.value, "mutateFallback", tTJSVariant(42));
        static_cast<RecordingObject*>(target.value)->sourceToMutate = source.value;
        require(source->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, target.value) == TJS_S_OK &&
                get(target.value, "mutateFallback").AsInteger() == 42,
                "class mutation during fallback invalidated name/value lifetime");
        Ref throwing(new RecordingObject);
        static_cast<RecordingObject*>(throwing.value)->throwOnWrite = true;
        put(source.value, "throwHere", tTJSVariant(1));
        bool threw = false;
        try { source->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, throwing.value); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw, "member copy swallowed destination exception");
    }
}

class ConstructorProbe : public tTJSDispatch {
    tTJSVariant original_;
public:
    unsigned calls = 0;
    explicit ConstructorProbe(tTJSVariant original) : original_(std::move(original)) {}
    tjs_error FuncCall(tjs_uint32 flags, const tjs_char* member, tjs_uint32* hint, tTJSVariant* result,
                      tjs_int count, tTJSVariant** args, iTJSDispatch2* objthis) override {
        ++calls;
        auto code = original_.AsObjectClosureNoAddRef().FuncCall(flags, member, hint, result, count, args, objthis);
        if (TJS_SUCCEEDED(code)) put(objthis, "constructorMarker", tTJSVariant(73));
        return code;
    }
};
void actualFactoryMutations(bool array)
{
    iTJSDispatch2* classObject = nullptr;
    Ref initial(array ? TJSCreateArrayObject(&classObject) : TJSCreateDictionaryObject(&classObject));
    Ref type(classObject);
    const char* name = array ? "Array" : "Dictionary";
    const auto original = get(type.value, name, TJS_IGNOREPROP);
    Ref constructor(new ConstructorProbe(original)), function(new ProbeMember);
    const auto ctorFlags = array ? 0 : TJS_STATICMEMBER;
    put(type.value, name, tTJSVariant(constructor.value), ctorFlags);
    put(type.value, "dynamicMarker", tTJSVariant(11));
    put(type.value, "dynamicMethod", tTJSVariant(function.value));
    put(type.value, "dynamicHidden", tTJSVariant(12), TJS_HIDDENMEMBER);
    put(type.value, "dynamicStatic", tTJSVariant(13), TJS_STATICMEMBER);
    {
        Ref first(array ? TJSCreateArrayObject() : TJSCreateDictionaryObject());
        require(first->IsInstanceOf(0, nullptr, nullptr, name, first.value) == TJS_S_TRUE,
                "factory lost script class identity");
        iTJSNativeInstance* native = nullptr;
        require(TJS_SUCCEEDED(first->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                    array ? TJSGetArrayClassID() : TJSGetDictionaryClassID(), &native)) && native,
                "factory lost native class identity");
        require(get(first.value, "dynamicMarker").AsInteger() == 11 &&
                get(first.value, "constructorMarker").AsInteger() == 73,
                "current class fields or overridden constructor were ignored");
        auto closure = get(first.value, "dynamicMethod", TJS_IGNOREPROP);
        require(closure.AsObjectThisNoAddRef() == first.value, "factory instance method closure not bound");
        require(TJS_SUCCEEDED(closure.AsObjectClosureNoAddRef().FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, nullptr)) &&
                static_cast<ProbeMember*>(function.value)->lastThis == first.value,
                "bound instance method executes with wrong this");
        tTJSVariant absent;
        require(first->PropGet(TJS_MEMBERMUSTEXIST, "dynamicStatic", nullptr, &absent, first.value) == TJS_E_MEMBERNOTFOUND,
                "new static member leaked into factory instance");
        put(type.value, "dynamicMarker", tTJSVariant(22));
        put(type.value, "dynamicStatic", tTJSVariant(33)); // static -> nonstatic
        put(type.value, "dynamicHidden", tTJSVariant(44), TJS_STATICMEMBER); // nonstatic -> static
        Ref second(array ? TJSCreateArrayObject() : TJSCreateDictionaryObject());
        require(get(second.value, "dynamicMarker").AsInteger() == 22 &&
                get(second.value, "dynamicStatic").AsInteger() == 33 &&
                get(first.value, "dynamicMarker").AsInteger() == 11,
                "class mutation used a stale/shared instance snapshot");
        require(second->PropGet(TJS_MEMBERMUSTEXIST, "dynamicHidden", nullptr, &absent, second.value) == TJS_E_MEMBERNOTFOUND,
                "changed static flag ignored by new factory instance");
        put(first.value, "dynamicMarker", tTJSVariant(99));
        require(get(second.value, "dynamicMarker").AsInteger() == 22, "instances shared mutable members");
        require(static_cast<ConstructorProbe*>(constructor.value)->calls == 2, "overridden constructor call count changed");
        if (array) {
            tTJSVariant item(5); tTJSVariant* args[]{&item};
            require(TJS_SUCCEEDED(first->FuncCall(0, "add", nullptr, nullptr, 1, args, first.value)), "native Array.add failed");
            require(get(first.value, "count").AsInteger() == 1 && get(first.value, "length").AsInteger() == 1,
                    "Array count/length properties missing");
        }
    }
    put(type.value, name, original, ctorFlags);
    for (const auto* member : {"dynamicMarker", "dynamicMethod", "dynamicHidden", "dynamicStatic"})
        type->DeleteMember(0, member, nullptr, type.value);
}

void structuredCloneAndFinalization()
{
    iTJSDispatch2* dictionaryClass = nullptr;
    Ref root(TJSCreateDictionaryObject(&dictionaryClass)), type(dictionaryClass), nested(TJSCreateDictionaryObject());
    Ref array(TJSCreateArrayObject()), destination(TJSCreateDictionaryObject());
    put(nested.value, "value", tTJSVariant(4));
    tTJSVariant nestedValue(nested.value, nested.value);
    require(TJS_SUCCEEDED(array->PropSetByNum(TJS_MEMBERENSURE, 0, &nestedValue, array.value)), "fixture array insert failed");
    put(root.value, "items", tTJSVariant(array.value, array.value));
    tTJSVariant input(root.value, root.value); tTJSVariant* args[]{&input};
    require(TJS_SUCCEEDED(type->FuncCall(0, "assignStruct", nullptr, nullptr, 1, args, destination.value)),
            "Dictionary.assignStruct native Owner initialization broke");
    auto copiedArray = get(destination.value, "items");
    require(copiedArray.AsObjectNoAddRef() != array.value, "assignStruct did not create a fresh nested array");
    tTJSVariant copiedNested;
    auto* copied = copiedArray.AsObjectNoAddRef();
    require(TJS_SUCCEEDED(copied->PropGetByNum(0, 0, &copiedNested, copied)), "assignStruct lost nested dictionary");
    put(nested.value, "value", tTJSVariant(9));
    require(get(copiedNested.AsObjectNoAddRef(), "value").AsInteger() == 4, "assignStruct clone shares nested state");
    Ref finalizer(new ProbeMember);
    for (bool isArray : {false, true}) {
        Ref object(isArray ? TJSCreateArrayObject() : TJSCreateDictionaryObject());
        put(object.value, "finalize", tTJSVariant(finalizer.value));
    }
    require(static_cast<ProbeMember*>(finalizer.value)->functionCalls == 0,
            "built-in data containers started invoking script finalize unexpectedly");
    for (bool isArray : {false, true}) {
        Ref object(isArray ? TJSCreateArrayObject() : TJSCreateDictionaryObject());
        put(object.value, "cleanup", tTJSVariant(finalizer.value));
        tTJSVariant cleanup("cleanup");
        require(TJS_SUCCEEDED(object->ClassInstanceInfo(TJS_CII_SET_FINALIZE, 0, &cleanup)), "set finalize name failed");
    }
    require(static_cast<ProbeMember*>(finalizer.value)->functionCalls == 2,
            "explicit ClassInstanceInfo finalizer behavior changed");
}

class TimedArrayClass : public tTJSArrayClass {
public:
    explicit TimedArrayClass(bool direct) { UseDirectMemberCopy = direct; }
};
class TimedDictionaryClass : public tTJSDictionaryClass {
public:
    explicit TimedDictionaryClass(bool direct) { UseDirectMemberCopy = direct; }
};
double timedFactories(iTJSDispatch2* type)
{
    constexpr int count = 20000;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) {
        iTJSDispatch2* instance = nullptr;
        require(TJS_SUCCEEDED(type->CreateNew(0, nullptr, nullptr, &instance, 0, nullptr, type)) && instance,
                "timed factory failed");
        instance->Release();
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void benchmark()
{
    Ref oldArray(new TimedArrayClass(false)), newArray(new TimedArrayClass(true));
    Ref oldDictionary(new TimedDictionaryClass(false)), newDictionary(new TimedDictionaryClass(true));
    const auto arrayOld = timedFactories(oldArray.value), arrayNew = timedFactories(newArray.value);
    const auto dictionaryOld = timedFactories(oldDictionary.value), dictionaryNew = timedFactories(newDictionary.value);
    std::cout << "20,000 create/release operations: Array enum=" << arrayOld << " ms direct=" << arrayNew
              << " ms; Dictionary enum=" << dictionaryOld << " ms direct=" << dictionaryNew << " ms\n";
}
}

int main()
{
    try {
        tTJS vm;
        copyOrderFlagsAndFallback();
        actualFactoryMutations(true);
        actualFactoryMutations(false);
        structuredCloneAndFinalization();
        benchmark();
        std::cout << "PASS: native factories preserve class mutations, constructors, closure binding, ordering/flags, "
                     "fallback/errors, native identity, array properties, structured copies and finalizers\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    } catch (...) {
        std::cerr << "FAIL: unexpected TJS exception\n"; return 1;
    }
}
