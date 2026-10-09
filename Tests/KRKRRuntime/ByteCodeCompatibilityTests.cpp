#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsInterCodeGen.h"
#include "tjsKnownByteCodeCompatibility.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace TJS;
namespace compat=TJS::known_bytecode;
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
class FixtureStream:public tTJSBinaryStream {
public:
    std::vector<uint8_t> bytes;size_t cursor=0;
    tjs_uint64 Seek(tjs_int64 offset,tjs_int whence) override {
        const auto base=whence==TJS_BS_SEEK_CUR?cursor:whence==TJS_BS_SEEK_END?bytes.size():0;
        const auto next=int64_t(base)+offset;
        Require(next>=0 && uint64_t(next)<=bytes.size(),"fixture seek outside bytes");
        return cursor=size_t(next);
    }
    tjs_uint Read(void* target,tjs_uint count) override {
        count=tjs_uint(std::min<size_t>(count,bytes.size()-cursor));
        if(count) std::memcpy(target,bytes.data()+cursor,count);cursor+=count;return count;
    }
    tjs_uint Write(const void* source,tjs_uint count) override {
        if(cursor+count>bytes.size()) bytes.resize(cursor+count);
        if(count) std::memcpy(bytes.data()+cursor,source,count);cursor+=count;return count;
    }
    bool Flush() override {return true;}
    tjs_uint64 GetSize() override {return bytes.size();}
};
std::string Hex(const compat::Digest& value) {
    constexpr char digits[]="0123456789abcdef";std::string text;
    for(auto byte:value) {text+=digits[byte>>4];text+=digits[byte&15];}return text;
}
void SHA256Vectors() {
    // No Sha256Prepare, hardware probing or VM initialization is involved.
    Require(Hex(compat::SoftwareSHA256(nullptr,0))==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","software SHA256 empty vector failed");
    const std::string abc="abc",longer(1000,'a');
    Require(Hex(compat::SoftwareSHA256(reinterpret_cast<const uint8_t*>(abc.data()),abc.size()))==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","software SHA256 abc vector failed");
    Require(Hex(compat::SoftwareSHA256(reinterpret_cast<const uint8_t*>(longer.data()),longer.size()))==
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3","software SHA256 multi-block vector failed");
}
// Inspect only our compiler-produced fixture to derive its exact rule and the
// fields used in deliberate negative mutations. Production uses its separate
// hardcoded rule and the bounded parser in the compatibility helper.
struct FixtureLayout {
    struct Object {int parent,name,type;size_t metadata,code,data;uint32_t words,constants;};
    const std::vector<uint8_t>& bytes;size_t at=20;
    std::vector<std::string> strings;std::vector<size_t> stringOffsets;
    std::vector<Object> objects;size_t target=0;int direct=-1,button=-1;
    uint16_t U16() {Require(at+2<=bytes.size(),"fixture short read");const auto value=uint16_t(bytes[at])|uint16_t(bytes[at+1])<<8;at+=2;return uint16_t(value);}
    uint32_t U32() {Require(at+4<=bytes.size(),"fixture integer read");uint32_t value=0;for(int i=0;i<4;++i)value|=uint32_t(bytes[at++])<<(i*8);return value;}
    void Skip(size_t count) {Require(at<=bytes.size() && count<=bytes.size()-at,"fixture skip outside bytes");at+=count;}
    void Array(size_t stride,bool align=false) {const size_t size=U32()*stride;Skip(size);if(align)Skip((4-size%4)%4);}
    explicit FixtureLayout(const std::vector<uint8_t>& value):bytes(value) {
        Array(1,true);Array(2,true);Array(4);Array(8);Array(8);
        const auto count=U32();
        for(uint32_t i=0;i<count;++i) {
            const auto length=U32();stringOffsets.push_back(at);std::string text;bool ascii=true;
            for(uint32_t j=0;j<length;++j) {const auto c=U16();if(!c || c>127)ascii=false;if(ascii)text+=char(c);}
            strings.push_back(ascii?text:std::string());Skip((length&1)*2);
            if(text=="direct" && ascii)direct=int(i);if(text=="hsvDirectSysButton" && ascii)button=int(i);
        }
        const auto octets=U32();for(uint32_t i=0;i<octets;++i)Array(1,true);
        Require(U32()==0x534a424f,"fixture object tag missing");U32();U32();const auto objectCount=U32();
        for(uint32_t i=0;i<objectCount;++i) {
            Require(U32()==0x32534a54,"fixture context tag missing");const auto length=U32();const auto begin=at;
            Object object{};object.metadata=at;object.parent=int32_t(U32());object.name=int32_t(U32());object.type=int(U32());
            Skip(9*4);Array(8);object.words=U32();object.code=at;Skip(size_t(object.words)*2+(object.words&1)*2);
            object.constants=U32();object.data=at;Skip(size_t(object.constants)*4);Array(4);Array(8);
            Require(at==begin+length,"fixture object length mismatch");objects.push_back(object);
        }
        bool found=false;
        for(size_t i=0;i<objects.size();++i) {
            const auto& object=objects[i];if(object.name<0 || strings[size_t(object.name)]!="updateColorSelect" || object.parent<0)continue;
            const auto& parent=objects[size_t(object.parent)];
            if(parent.name>=0 && strings[size_t(parent.name)]=="OptionHSVPickerModule") {Require(!found,"fixture duplicate target");target=i;found=true;}
        }
        Require(found && direct>=0 && button>=0,"fixture optional member signatures missing");
    }
    int16_t Word(size_t offset) const {return int16_t(uint16_t(bytes[offset])|uint16_t(bytes[offset+1])<<8);}
    compat::OptionalMemberRule Rule() const {
        const auto& object=objects[target];uint16_t slot=UINT16_MAX;
        for(uint32_t i=0;i<object.constants;++i) if(Word(object.data+i*4)==3 && Word(object.data+i*4+2)==direct)slot=uint16_t(i);
        Require(slot!=UINT16_MAX,"fixture direct local slot missing");
        compat::OptionalMemberRule rule{"hsvcpick.tjs",bytes.size(),compat::SoftwareSHA256(bytes.data(),bytes.size()),
            "OptionHSVPickerModule","updateColorSelect",object.words,object.constants,{}};
        size_t reads=0;
        for(uint32_t ip=0;ip+3<object.words;++ip) if(Word(object.code+ip*2)==VM_GPD && Word(object.code+(ip+2)*2)== -2 &&
            Word(object.code+(ip+3)*2)==slot) {
            Require(reads<2,"fixture unexpected third direct read");
            rule.reads[reads++]={ip,Word(object.code+(ip+1)*2),-2,slot};
        }
        Require(reads==2,"fixture missing two direct reads");return rule;
    }
};
void Write16(std::vector<uint8_t>& bytes,size_t at,uint16_t value) {bytes.at(at)=uint8_t(value);bytes.at(at+1)=uint8_t(value>>8);}
std::vector<uint8_t> CompileFixture() {
    tTJS vm;FixtureStream stream;
    vm.CompileScript(
        "class OptionHSVPickerModule {"
        " var color=-1; var button=void;"
        " property hsvDirectSysButton { getter { return button; } }"
        " function updateColorSelect() {"
        "  if(color<0) { if(direct!==void) direct.visible=0; }"
        "  return color;"
        " }"
        "}", &stream,false,false,false,"original-synthetic.tjs");
    return stream.bytes;
}
bool Missing(tTJS& vm,const char* expression) {
    try {tTJSVariant value;vm.EvalExpression(expression,&value);}
    catch(const eTJS& error) {return error.GetMessage().AsStdString().find("direct")!=std::string::npos;}
    return false;
}
void Behaviour(const std::vector<uint8_t>& bytes,bool patched) {
    tTJS vm;vm.LoadByteCode(bytes.data(),bytes.size(),nullptr,nullptr,"original-synthetic.tjs");
    vm.ExecScript("var module=new OptionHSVPickerModule(); module.button=new Dictionary(); module.button.visible=1;");
    if(!patched) Require(Missing(vm,"module.updateColorSelect()"),"original synthetic missing direct did not throw");
    else {
        tTJSVariant value;vm.EvalExpression("module.updateColorSelect()",&value);
        Require(value.AsInteger()== -1,"patched optional branch changed return");
        vm.EvalExpression("module.button.visible",&value);Require(value.AsInteger()==0,"patched real optional button was not hidden");
        vm.ExecScript("module.button=void;");vm.EvalExpression("module.updateColorSelect()",&value);
        Require(value.AsInteger()== -1,"patched absent button did not preserve void optional branch");
    }
    vm.ExecScript("module.button=new Dictionary();module.button.visible=1;module.color=7;");
    tTJSVariant value;vm.EvalExpression("module.updateColorSelect()",&value);
    Require(value.AsInteger()==7,"nonnegative branch return changed");
    vm.EvalExpression("module.button.visible",&value);Require(value.AsInteger()==1,"nonnegative branch visibility changed");
    Require(Missing(vm,"module.direct") && Missing(vm,"global.direct"),"compatibility added a fake instance/global direct member");
    bool unknown=false;
    try {vm.EvalExpression("module.unrelatedMissingMember",&value);}catch(const eTJS&){unknown=true;}
    Require(unknown,"compatibility changed generic missing-field errors");
}
void Rejections(const std::vector<uint8_t>& original,const FixtureLayout& layout,const compat::OptionalMemberRule& rule) {
    std::vector<uint8_t> copy{1,2,3};
    for(const char* name:{"xhsvcpick.tjs","hsvcpick.tjs.bak","hsvcpick.TJS","",static_cast<const char*>(nullptr)})
        Require(!compat::TryApplyOptionalMemberRule(rule,name,original.data(),original.size(),copy) && copy.empty(),"basename drift accepted or stale patch output retained");
    auto badDigest=rule;badDigest.digest[0]^=1;
    Require(!compat::TryApplyOptionalMemberRule(badDigest,"hsvcpick.tjs",original.data(),original.size(),copy),"digest mismatch accepted");
    Require(!compat::TryApplyOptionalMemberRule(rule,"hsvcpick.tjs",original.data(),original.size()-1,copy),"length mismatch accepted");
    const auto& object=layout.objects[layout.target];const auto& parent=layout.objects[size_t(object.parent)];
    const auto slot=rule.reads[0].localConstant;const auto first=object.code+size_t(rule.reads[0].ip)*2;
    auto reject=[&](std::vector<uint8_t> bytes) {
        auto matchingHash=rule;matchingHash.digest=compat::SoftwareSHA256(bytes.data(),bytes.size());
        copy={1};Require(!compat::TryApplyOptionalMemberRule(matchingHash,"hsvcpick.tjs",bytes.data(),bytes.size(),copy) && copy.empty(),
            "context/opcode/constant drift accepted after independent digest match");
    };
    auto bytes=original;bytes[layout.stringOffsets[size_t(parent.name)]]='X';reject(bytes);
    bytes=original;bytes[layout.stringOffsets[size_t(object.name)]]='X';reject(bytes);
    bytes=original;Write16(bytes,parent.metadata+8,ctFunction);reject(bytes);
    bytes=original;Write16(bytes,object.metadata+8,ctPropertyGetter);reject(bytes);
    bytes=original;Write16(bytes,first,VM_GPDS);reject(bytes);
    bytes=original;Write16(bytes,first+4,uint16_t(-1));reject(bytes);
    bytes=original;Write16(bytes,first+6,uint16_t(slot+1));reject(bytes);
    bytes=original;Write16(bytes,object.data+size_t(slot)*4,8);reject(bytes);
    bytes=original;Write16(bytes,object.data+size_t(slot)*4+2,uint16_t(layout.button));reject(bytes);
    bytes=original;bytes[layout.stringOffsets[size_t(layout.button)]]='X';reject(bytes);
    // A third use, even as a literal rather than a property name, prevents a
    // local-pool replacement from silently changing unrelated method behaviour.
    bool literal=false;
    for(uint32_t ip=0;ip+2<object.words;++ip) if(layout.Word(object.code+ip*2)==VM_CONST) {
        bytes=original;Write16(bytes,object.code+(ip+2)*2,slot);reject(bytes);literal=true;break;
    }
    Require(literal,"fixture missing independent literal for constant-use rejection");
    Require(!compat::TryApplyKnownCompatibility("hsvcpick.tjs",original.data(),original.size(),copy),
        "synthetic rule relaxed the fixed production content fingerprint");
}
}
void RunByteCodeCompatibilityTests() {
    SHA256Vectors();
    const auto original=CompileFixture(),untouched=original;FixtureLayout layout(original);const auto rule=layout.Rule();
    std::vector<uint8_t> patched;
    for(const char* name:{"hsvcpick.tjs","sysscn/hsvcpick.tjs","data.xp3>sysscn/hsvcpick.tjs","C:\\game\\hsvcpick.tjs"}) {
        Require(compat::TryApplyOptionalMemberRule(rule,name,original.data(),original.size(),patched),"verified synthetic mapping was not applied");
        Require(original==untouched && patched.size()==original.size(),"patch changed original storage or bytecode length");
        const auto mapping=layout.objects[layout.target].data+size_t(rule.reads[0].localConstant)*4+2;
        size_t changes=0;for(size_t i=0;i<original.size();++i) if(original[i]!=patched[i]) {
            ++changes;Require(i==mapping || i==mapping+1,"patch changed global pool, opcode, jump or unrelated constant");
        }
        Require(changes>0 && changes<=2,"patch did not make exactly its local index change");
    }
    Rejections(original,layout,rule);
    for(int session=0;session<3;++session) {Behaviour(original,false);Behaviour(patched,true);Behaviour(original,false);}
    Require(original==untouched,"repeated loads contaminated original bytes");
    std::cout<<"PASS bytecode compatibility: software SHA256 vectors, exact name/size/hash/context/read/constant guards, "
        "original missing errors, local optional-button mapping, void/nonnegative branches and repeated-load isolation\n";
}
int RunByteCodeCompatibilityStaticTest(const char* path) {
    std::ifstream input(path,std::ios::binary);Require(bool(input),"static bytecode input could not be opened");
    const std::vector<uint8_t> original((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto before=compat::SoftwareSHA256(original.data(),original.size());std::vector<uint8_t> patched;
    Require(compat::TryApplyKnownCompatibility(path,original.data(),original.size(),patched),"real bytecode did not match fixed production compatibility rule");
    FixtureLayout layout(original);const auto rule=layout.Rule();
    const auto mapping=layout.objects[layout.target].data+size_t(rule.reads[0].localConstant)*4+2;
    Require(patched.size()==original.size(),"real static patch changed size");
    size_t changes=0;for(size_t i=0;i<original.size();++i) if(original[i]!=patched[i]) {
        ++changes;Require(i==mapping || i==mapping+1,"real static patch changed nonlocal bytes");
        std::cout<<"local mapping byte offset="<<i<<" before="<<unsigned(original[i])<<" after="<<unsigned(patched[i])<<'\n';
    }
    Require(changes>0 && changes<=2,"real static patch did not change its local index");
    std::vector<uint8_t> again;
    Require(!compat::TryApplyKnownCompatibility(path,patched.data(),patched.size(),again) && again.empty(),"already patched real bytes accepted as original");
    Require(compat::SoftwareSHA256(original.data(),original.size())==before,"static inspection mutated input");
    std::cout<<"PASS static known bytecode: length="<<original.size()<<" changedBytes="<<changes<<" beforeSHA256="<<Hex(before)
        <<" afterSHA256="<<Hex(compat::SoftwareSHA256(patched.data(),patched.size()))<<"; no VM or TopLevel execution\n";
    return 0;
}
