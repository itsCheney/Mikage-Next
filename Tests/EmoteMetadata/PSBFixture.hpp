// Shared synthetic PSB writer and tracked in-memory stream for reader/loader tests.
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
    // A complete v2+ container: unlike build(), this includes the name trie and
    // string/chunk offset tables and can go through emotefile::load unchanged.
    Bytes buildFullFile(const Node& root, unsigned version = 3) {
        require(version >= 2 && version <= 4, "full fixture supports PSB v2-v4");
        Bytes entries = encode(root);
        std::vector<uint32_t> charset(1), namesData(1), nameIndexes;
        std::map<std::pair<uint32_t, uint8_t>, uint32_t> edges;
        uint32_t nextBlock = 256;
        std::vector<uint32_t> leaves;
        for (const auto& name : names) {
            uint32_t parent = 0;
            for (unsigned char ch : name) {
                auto edge = std::make_pair(parent, ch);
                auto found = edges.find(edge);
                if (found != edges.end()) { parent = found->second; continue; }
                if (!charset[parent]) { charset[parent] = nextBlock; nextBlock += 256; }
                const uint32_t child = charset[parent] + ch;
                if (namesData.size() <= child) { namesData.resize(child + 1); charset.resize(child + 1); }
                namesData[child] = parent;
                edges.emplace(edge, child);
                parent = child;
            }
            leaves.push_back(parent);
        }
        for (auto leaf : leaves) {
            nameIndexes.push_back(static_cast<uint32_t>(namesData.size()));
            namesData.push_back(leaf);
        }
        Bytes stringData;
        stringOffsets.clear();
        for (const auto& value : strings) {
            stringOffsets.push_back(static_cast<uint32_t>(stringData.size()));
            stringData.insert(stringData.end(), value.begin(), value.end()); stringData.push_back(0);
        }
        PSB::PSBHeader header{};
        std::memcpy(header.signature, "PSB", 4);
        header.version = version;
        Bytes bytes(PSB::PSBHeader::MAX_LENGTH);
        header.offsetEncrypt = header.offsetNames = static_cast<uint32_t>(bytes.size());
        append(bytes, packed(charset)); append(bytes, packed(namesData)); append(bytes, packed(nameIndexes));
        header.offsetStrings = static_cast<uint32_t>(bytes.size()); append(bytes, packed(stringOffsets));
        header.offsetStringsData = static_cast<uint32_t>(bytes.size()); append(bytes, stringData);
        header.offsetChunkOffsets = static_cast<uint32_t>(bytes.size()); append(bytes, packed({}));
        header.offsetChunkLengths = static_cast<uint32_t>(bytes.size()); append(bytes, packed({}));
        header.offsetChunkData = static_cast<uint32_t>(bytes.size());
        if (version >= 4) {
            header.offsetExtraChunkOffsets = static_cast<uint32_t>(bytes.size()); append(bytes, packed({}));
            header.offsetExtraChunkLengths = static_cast<uint32_t>(bytes.size()); append(bytes, packed({}));
            header.offsetExtraChunkData = static_cast<uint32_t>(bytes.size());
        }
        header.offsetEntries = static_cast<uint32_t>(bytes.size()); append(bytes, entries);
        std::memcpy(bytes.data(), &header, sizeof header);
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

