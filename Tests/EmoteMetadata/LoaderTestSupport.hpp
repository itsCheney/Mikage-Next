// Host boundaries for the production loader/cache integration test. PSB parsing,
// container decoding, shared-cache policy and read-only streams stay production.
std::map<std::string, std::vector<uint8_t>> loaderFiles;
std::map<std::string, unsigned> loaderOpens;
std::map<std::string, std::string> loaderAliases;
unsigned loaderLiveStreams = 0;
unsigned loaderTreesCreated = 0;
unsigned loaderLiveTrees = 0;
bool loaderFailTree = false;
bool loaderArchiveFilters = false;
bool TVPHasXP3ArchiveFilters() { return loaderArchiveFilters; }
constexpr char TVPArchiveDelimiter = '>';

[[noreturn]] void TVPThrowExceptionMessage(const tjs_char* message)
{
    throw std::runtime_error(message);
}

class tTVPMemoryStream : public tTJSBinaryStream {
    std::vector<uint8_t> bytes_;
    size_t cursor_ = 0;
public:
    tTVPMemoryStream() { ++loaderLiveStreams; }
    tTVPMemoryStream(const void* block, tjs_uint size) : bytes_(size) {
        if (block && size) std::memcpy(bytes_.data(), block, size);
        ++loaderLiveStreams;
    }
    ~tTVPMemoryStream() override { --loaderLiveStreams; }
    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override {
        const int64_t base = whence == TJS_BS_SEEK_CUR ? cursor_ :
            whence == TJS_BS_SEEK_END ? bytes_.size() : 0;
        const int64_t next = base + offset;
        if (next >= 0 && static_cast<uint64_t>(next) <= bytes_.size()) cursor_ = static_cast<size_t>(next);
        return cursor_;
    }
    tjs_uint Read(void* destination, tjs_uint count) override {
        count = static_cast<tjs_uint>(std::min<size_t>(count, bytes_.size() - cursor_));
        if (count) std::memcpy(destination, bytes_.data() + cursor_, count);
        cursor_ += count;
        return count;
    }
    tjs_uint Write(const void* source, tjs_uint count) override {
        if (cursor_ + count > bytes_.size()) bytes_.resize(cursor_ + count);
        if (count) std::memcpy(bytes_.data() + cursor_, source, count);
        cursor_ += count;
        return count;
    }
    void SetEndOfStorage() override { bytes_.resize(cursor_); }
    bool Flush() override { return true; }
    tjs_uint64 GetSize() override { return bytes_.size(); }
    void* GetInternalBuffer() { return bytes_.data(); }
};

ttstr TVPGetPlacedPath(const ttstr& path)
{
    auto alias = loaderAliases.find(path.AsStdString());
    return alias == loaderAliases.end() ? path : ttstr(alias->second);
}
tTJSBinaryStream* TVPCreateStream(const ttstr& path)
{
    const auto canonical = TVPGetPlacedPath(path).AsStdString();
    ++loaderOpens[canonical];
    auto found = loaderFiles.find(canonical);
    if (found == loaderFiles.end()) throw std::runtime_error("fixture resource not found");
    return new tTVPMemoryStream(found->second.data(), static_cast<tjs_uint>(found->second.size()));
}

// Custom decrypt callbacks are real TJS closures; only the storage-facing byte
// accessor is reduced to its buffer/length for these identity-decrypt tests.
class CBinaryAccessor : public tTJSDispatch {
public:
    unsigned char* data;
    unsigned int length;
    CBinaryAccessor(unsigned char* bytes, unsigned int size) : data(bytes), length(size) {}
};
