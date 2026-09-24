#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "psbfile/PSBData.h"
#include <chrono>
#include <iomanip>
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

// The fields and methods used by the unmodified production reader. Loading,
// animation trees, textures and decryption are outside this isolated test.
namespace emoteplayer {
class emotefile {
public:
    tTJSBinaryStream* filePtr = nullptr;
    PSB::PSBHeader _header{};
    std::vector<uint32_t> stringsOffset;
    std::vector<std::string> namesCache;
    tTJSVariant root();
    tTJSVariant readVariableFrameList(const ttstr& name);
    tTJSVariant readAllObjs(const ttstr& key, tjs_uint32 offset);
    uint32_t readListInfo(std::vector<uint32_t>* target);
    void refreshListInfo(std::vector<uint32_t>* offsets, std::vector<uint32_t>* names);
    bool parseObject(std::map<std::string, uint32_t>& output, uint32_t offset);
    bool parseList(std::vector<uint32_t>& output, uint32_t offset);
};
#include "ProductionReader.inc"
}

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
using Bytes = std::vector<uint8_t>;
void append(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }
void integer(Bytes& data, uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) data.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
Bytes packed(const std::vector<uint32_t>& values)
{
    Bytes data{PSB::ArrayN4};
    integer(data, values.size(), 4);
    data.push_back(PSB::ArrayN4);
    for (auto value : values) integer(data, value, 4);
    return data;
}

struct Node {
    enum Kind { Scalar, String, List, Object } kind = Scalar;
    Bytes scalar;
    std::string string;
    std::vector<Node> items;
    std::vector<std::pair<std::string, Node>> members;
    static Node raw(Bytes value) { Node n; n.scalar = std::move(value); return n; }
    static Node number(int64_t value, unsigned width = 8) {
        Bytes bytes{static_cast<uint8_t>(PSB::NumberN0 + width)};
        integer(bytes, static_cast<uint64_t>(value), width);
        return raw(std::move(bytes));
    }
    static Node text(std::string value) { Node n; n.kind = String; n.string = std::move(value); return n; }
    static Node list(std::vector<Node> values) { Node n; n.kind = List; n.items = std::move(values); return n; }
    static Node object(std::vector<std::pair<std::string, Node>> values) {
        Node n; n.kind = Object; n.members = std::move(values); return n;
    }
};

class TrackingStream : public tTJSBinaryStream {
public:
    Bytes data;
    size_t position = 0, readBytes = 0, reads = 0;
    size_t forbiddenBegin = 0, forbiddenEnd = 0;
    explicit TrackingStream(Bytes value) : data(std::move(value)) {}
    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override {
        int64_t next = offset;
        if (whence == TJS_BS_SEEK_CUR) next += position;
        else if (whence == TJS_BS_SEEK_END) next += data.size();
        require(next >= 0 && static_cast<uint64_t>(next) <= data.size(), "PSB seek out of bounds");
        position = static_cast<size_t>(next);
        return position;
    }
    tjs_uint Read(void* buffer, tjs_uint size) override {
        require(size <= data.size() - position, "PSB read out of bounds");
        require(!size || position >= forbiddenEnd || position + size <= forbiddenBegin,
                "metadata query read the unrelated animation subtree");
        std::memcpy(buffer, data.data() + position, size);
        position += size; readBytes += size; ++reads;
        return size;
    }
    tjs_uint Write(const void*, tjs_uint) override { throw std::runtime_error("unexpected write"); }
    bool Flush() override { return true; }
    tjs_uint64 GetSize() override { return data.size(); }
    void resetReads() { readBytes = reads = 0; }
};

struct Fixture {
    std::vector<std::string> names, strings;
    std::vector<uint32_t> stringOffsets;
    uint32_t index(std::vector<std::string>& table, const std::string& value) {
        auto found = std::find(table.begin(), table.end(), value);
        if (found != table.end()) return static_cast<uint32_t>(found - table.begin());
        table.push_back(value);
        return static_cast<uint32_t>(table.size() - 1);
    }
    Bytes encode(const Node& node) {
        if (node.kind == Node::Scalar) return node.scalar;
        if (node.kind == Node::String) {
            Bytes bytes{PSB::StringN4}; integer(bytes, index(strings, node.string), 4); return bytes;
        }
        std::vector<uint32_t> offsets, keys;
        Bytes payload;
        if (node.kind == Node::List) {
            for (const auto& item : node.items) {
                offsets.push_back(static_cast<uint32_t>(payload.size()));
                append(payload, encode(item));
            }
        } else {
            for (const auto& member : node.members) {
                keys.push_back(index(names, member.first));
                offsets.push_back(static_cast<uint32_t>(payload.size()));
                append(payload, encode(member.second));
            }
        }
        Bytes bytes{static_cast<uint8_t>(node.kind == Node::List ? PSB::List : PSB::Objects)};
        if (node.kind == Node::Object) append(bytes, packed(keys));
        append(bytes, packed(offsets));
        append(bytes, payload);
        return bytes;
    }
    TrackingStream build(const Node& root, unsigned version = 3) {
        Bytes entries = encode(root);
        PSB::PSBHeader header{};
        std::memcpy(header.signature, "PSB", 4);
        header.version = version;
        header.offsetNames = header.offsetEntries = PSB::PSBHeader::MAX_LENGTH;
        header.offsetStringsData = header.offsetEntries + static_cast<uint32_t>(entries.size());
        Bytes bytes(PSB::PSBHeader::MAX_LENGTH);
        std::memcpy(bytes.data(), &header, sizeof header);
        append(bytes, entries);
        for (const auto& value : strings) {
            stringOffsets.push_back(static_cast<uint32_t>(bytes.size()) - header.offsetStringsData);
            bytes.insert(bytes.end(), value.begin(), value.end()); bytes.push_back(0);
        }
        return TrackingStream(std::move(bytes));
    }
    void attach(emoteplayer::emotefile& file, TrackingStream& stream) {
        file.filePtr = &stream;
        require(file._header.parsePSBHeader(&stream), "PSB header rejected fixture");
        file.namesCache = names; file.stringsOffset = stringOffsets;
    }
};

tTJSVariant property(const tTJSVariant& value, const tjs_char* name)
{
    tTJSVariant result;
    if (value.Type() != tvtObject || !value.AsObjectNoAddRef()) return result;
    auto* object = value.AsObjectNoAddRef();
    object->PropGet(0, name, nullptr, &result, object);
    return result;
}
tTJSVariant item(const tTJSVariant& value, tjs_int number)
{
    tTJSVariant result;
    auto* object = value.AsObjectNoAddRef();
    require(TJS_SUCCEEDED(object->PropGetByNum(TJS_MEMBERMUSTEXIST, number, &result, object)),
            "array element missing");
    return result;
}
std::string canonical(const tTJSVariant& value);
class Members : public tTJSDispatch {
public:
    std::map<std::string, std::string> values;
    tjs_error FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant* result,
                      tjs_int count, tTJSVariant** args, iTJSDispatch2*) override {
        require(count == 3, "invalid dictionary enumeration");
        if (!(args[1]->AsInteger() & TJS_HIDDENMEMBER))
            values[ttstr(*args[0]).AsStdString()] = canonical(*args[2]);
        if (result) *result = 1;
        return TJS_S_OK;
    }
};
std::string canonical(const tTJSVariant& value)
{
    switch (value.Type()) {
        case tvtVoid: return "void";
        case tvtInteger: return "i:" + std::to_string(value.AsInteger());
        case tvtReal: {
            std::ostringstream out; out << "r:" << std::setprecision(17) << value.AsReal(); return out.str();
        }
        case tvtString: {
            auto text = ttstr(value).AsStdString(); return "s:" + std::to_string(text.size()) + ":" + text;
        }
        case tvtObject: {
            auto* object = value.AsObjectNoAddRef();
            require(object, "unexpected null object");
            if (object->IsInstanceOf(0, nullptr, nullptr, TJS_N("Array"), object) == TJS_S_TRUE) {
                std::string output = "[";
                auto count = property(value, TJS_N("count")).AsInteger();
                for (tjs_int i = 0; i < count; ++i) output += canonical(item(value, i)) + ";";
                return output + "]";
            }
            Members collector;
            tTJSVariantClosure callback(&collector, nullptr);
            require(TJS_SUCCEEDED(object->EnumMembers(TJS_IGNOREPROP, &callback, object)), "enum failed");
            std::string output = "{";
            for (const auto& member : collector.values)
                output += std::to_string(member.first.size()) + ":" + member.first + "=" + member.second + ";";
            return output + "}";
        }
        default: throw std::runtime_error("unexpected PSB variant type");
    }
}
tTJSVariant oldFullRootQuery(emoteplayer::emotefile& file, const ttstr& label)
{
    auto root = file.root();
    auto variables = property(property(root, TJS_N("metadata")), TJS_N("variableList"));
    if (variables.Type() != tvtObject) return {};
    const auto count = property(variables, TJS_N("count")).AsInteger();
    for (tjs_int i = 0; i < count; ++i) {
        auto entry = item(variables, i);
        auto name = property(entry, TJS_N("label"));
        if (name.Type() == tvtString && ttstr(name) == label) {
            auto* object = entry.AsObjectNoAddRef();
            tTJSVariant result;
            if (TJS_SUCCEEDED(object->PropGet(0, TJS_N("frameList"), nullptr, &result, object))) return result;
        }
    }
    return {};
}

Node variable(const std::string& label, Node frames)
{
    return Node::object({{"label", Node::text(label)}, {"frameList", std::move(frames)}});
}
Node metadata(Node variables)
{
    return Node::object({{"metadata", Node::object({{"variableList", std::move(variables)}})}});
}

void missingAndDuplicateCases()
{
    const std::vector<Node> roots = {
        Node::object({}), Node::object({{"metadata", Node::raw({PSB::Null})}}),
        Node::object({{"metadata", Node::object({})}}), metadata(Node::raw({PSB::Null})),
        metadata(Node::list({})), metadata(Node::list({variable("other", Node::list({}))})),
        metadata(Node::list({Node::object({{"label", Node::number(123)}, {"frameList", Node::list({})}})})),
        metadata(Node::list({Node::object({{"label", Node::text("wanted")}})})),
    };
    for (const auto& root : roots) {
        Fixture fixture; auto stream = fixture.build(root); emoteplayer::emotefile file;
        fixture.attach(file, stream);
        require(file.readVariableFrameList(TJS_N("wanted")).Type() == tvtVoid, "missing query must return void");
    }
    Fixture fixture;
    auto stream = fixture.build(metadata(Node::list({
        variable("wanted", Node::list({Node::number(11)})),
        variable("wanted", Node::list({Node::number(22)})),
    })));
    emoteplayer::emotefile file; fixture.attach(file, stream);
    auto result = file.readVariableFrameList(TJS_N("wanted"));
    require(item(result, 0).AsInteger() == 11, "must return first matching entry");
    require(canonical(result) == canonical(oldFullRootQuery(file, TJS_N("wanted"))), "duplicate-label parity failed");
    Fixture missingFrameFixture;
    auto missingFrameStream = missingFrameFixture.build(metadata(Node::list({
        Node::object({{"label", Node::text("wanted")}}),
        variable("wanted", Node::list({Node::number(22)})),
    })));
    emoteplayer::emotefile missingFrameFile; missingFrameFixture.attach(missingFrameFile, missingFrameStream);
    require(missingFrameFile.readVariableFrameList(TJS_N("wanted")).Type() == tvtVoid,
            "missing frameList on first matching label must return void");
    require(canonical(missingFrameFile.readVariableFrameList(TJS_N("wanted"))) ==
            canonical(oldFullRootQuery(missingFrameFile, TJS_N("wanted"))), "missing frameList parity failed");
    emoteplayer::emotefile unloaded;
    require(unloaded.readVariableFrameList(TJS_N("wanted")).Type() == tvtVoid, "unloaded query must return void");
}

void primitiveAndArrayParity()
{
    float f = 1.25f; double d = -123.5;
    Bytes fb{PSB::Float}, db{PSB::Double};
    auto* fp = reinterpret_cast<uint8_t*>(&f); fb.insert(fb.end(), fp, fp + sizeof f);
    auto* dp = reinterpret_cast<uint8_t*>(&d); db.insert(db.end(), dp, dp + sizeof d);
    std::vector<Node> values{Node::raw({PSB::None}), Node::raw({PSB::Null}),
        Node::raw({PSB::False}), Node::raw({PSB::True}), Node::raw({PSB::NumberN0})};
    std::vector<tTJSVariant> expected{tTJSVariant(), tTJSVariant(), tTJSVariant(0), tTJSVariant(1), tTJSVariant(0)};
    for (unsigned width = 1; width <= 8; ++width) {
        values.push_back(Node::number(-1, width)); expected.emplace_back(static_cast<tjs_int64>(-1));
        const int64_t positive = static_cast<int64_t>(1) << (width * 8 - 2);
        values.push_back(Node::number(positive, width)); expected.emplace_back(positive);
    }
    values.push_back(Node::raw({PSB::Float0})); expected.emplace_back(0.0);
    values.push_back(Node::raw(fb)); expected.emplace_back(static_cast<tjs_real>(f));
    values.push_back(Node::raw(db)); expected.emplace_back(d);
    values.push_back(Node::text(u8"身体・ひねり😀")); expected.emplace_back(u8"身体・ひねり😀");
    values.push_back(Node::raw({PSB::ResourceN1, 0})); expected.emplace_back();
    values.push_back(Node::raw({PSB::ExtraChunkN1, 0})); expected.emplace_back();
    // Trailing void must still extend the array count just as Array.add does.
    Fixture fixture; auto stream = fixture.build(Node::list(values)); emoteplayer::emotefile file;
    fixture.attach(file, stream);
    auto actual = file.root();
    auto* oldArray = TJSCreateArrayObject();
    tTJSVariant legacy(oldArray, oldArray); oldArray->Release();
    for (auto& value : expected) {
        tTJSVariant* args[] = {&value};
        require(TJS_SUCCEEDED(oldArray->FuncCall(0, TJS_N("add"), nullptr, nullptr, 1, args, oldArray)),
                "legacy Array.add failed");
    }
    require(canonical(actual) == canonical(legacy), "primitive/List values changed from legacy Array.add semantics");
    require(property(actual, TJS_N("count")).AsInteger() == static_cast<tjs_int64>(expected.size()),
            "trailing void lost array element");

    Fixture packedFixture; auto packedStream = packedFixture.build(Node::raw(packed({0, 2147483647, 2147483648u, 4294967295u})));
    emoteplayer::emotefile packedFile; packedFixture.attach(packedFile, packedStream);
    auto packedResult = packedFile.root();
    require(property(packedResult, TJS_N("count")).AsInteger() == 4, "packed array length changed");
    require(item(packedResult, 2).AsInteger() == -2147483648LL && item(packedResult, 3).AsInteger() == -1,
            "packed arrays must preserve existing signed int32 conversion");
}

void frameValueParity()
{
    // Existing scripts may retain fields/types beyond the usual frame schema.
    for (const auto& value : std::vector<Node>{Node::raw({PSB::Null}), Node::number(7),
            Node::text("extension"), Node::list({}), Node::object({}),
            Node::raw(packed({0, 1, 4294967295u}))}) {
        Fixture fixture;
        auto stream = fixture.build(metadata(Node::list({variable("wanted", value)})));
        emoteplayer::emotefile file; fixture.attach(file, stream);
        require(canonical(file.readVariableFrameList(TJS_N("wanted"))) ==
                canonical(oldFullRootQuery(file, TJS_N("wanted"))), "frameList value/type parity failed");
    }
}

void subtreeIsolationAndCost(unsigned version)
{
    const std::string label = u8"身体・ひねり😀";
    Node frames = Node::list({Node::object({
        {"time", Node::number(5)}, {"value", Node::number(-9)},
        {"unknown", Node::object({{"深い字段", Node::list({Node::text("keep me"), Node::raw({PSB::Null}), Node::list({})})}})},
    }), Node::raw({PSB::Null})});
    Node giant = Node::list({});
    for (int i = 0; i < 8000; ++i)
        giant.items.push_back(Node::object({{"frame", Node::number(i)}, {"mesh", Node::raw(packed({1,2,3,4,5,6,7,8}))}}));
    Fixture fixture;
    const auto giantBytes = fixture.encode(giant).size();
    Node root = Node::object({{"objects", giant}, {"metadata", Node::object({
        {"variableList", Node::list({variable("other", Node::list({})), variable(label, frames),
                                     variable(label, Node::list({Node::number(99)}))})},
    })}});
    auto stream = fixture.build(root, version); emoteplayer::emotefile file; fixture.attach(file, stream);
    const auto start = std::chrono::steady_clock::now();
    auto full = oldFullRootQuery(file, ttstr(label));
    const auto fullTime = std::chrono::steady_clock::now() - start;
    const size_t fullBytes = stream.readBytes;
    const auto expected = canonical(full);
    std::map<std::string, uint32_t> entries;
    require(file.parseObject(entries, file._header.offsetEntries), "root offset map failed");
    stream.forbiddenBegin = entries.at("objects");
    stream.forbiddenEnd = stream.forbiddenBegin + giantBytes;
    stream.resetReads();
    const auto queryStart = std::chrono::steady_clock::now();
    auto first = file.readVariableFrameList(ttstr(label));
    const auto queryTime = std::chrono::steady_clock::now() - queryStart;
    const size_t queryBytes = stream.readBytes;
    require(canonical(first) == expected, "subtree differs from full-root query");
    require(queryBytes * 100 < fullBytes, "query must avoid reading large unrelated subtree");
    auto second = file.readVariableFrameList(ttstr(label));
    require(first.AsObjectNoAddRef() != second.AsObjectNoAddRef(), "query returned shared mutable array");
    auto firstNested = property(item(first, 0), TJS_N("unknown"));
    auto secondNested = property(item(second, 0), TJS_N("unknown"));
    require(firstNested.AsObjectNoAddRef() != secondNested.AsObjectNoAddRef(), "nested objects were shared");
    tTJSVariant changed(42);
    auto* dictionary = firstNested.AsObjectNoAddRef();
    dictionary->PropSet(TJS_MEMBERENSURE, TJS_N("deepMutation"), nullptr, &changed, dictionary);
    require(canonical(first) != expected, "mutation did not reach returned tree");
    require(canonical(second) == expected, "one caller's mutation affected another result");
    require(canonical(file.readVariableFrameList(ttstr(label))) == expected, "mutation affected later queries");
    require(file.readVariableFrameList(TJS_N("missing")).Type() == tvtVoid, "unknown Unicode label query failed");
    auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    std::cout << "PSB v" << version << ": full root " << fullBytes << " bytes / " << milliseconds(fullTime)
              << " ms; metadata subtree " << queryBytes << " bytes / " << milliseconds(queryTime) << " ms\n";
}
}

int main()
{
    try {
        tTJS vm;
        primitiveAndArrayParity();
        missingAndDuplicateCases();
        frameValueParity();
        for (unsigned version : {2u, 3u, 4u}) subtreeIsolationAndCost(version);
        std::cout << "PASS: production PSB reader, primitive/array parity, Unicode and duplicate labels, "
                     "missing metadata, nested mutation isolation and unrelated-subtree read exclusion\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    } catch (...) {
        std::cerr << "FAIL: unexpected TJS exception\n"; return 1;
    }
}
