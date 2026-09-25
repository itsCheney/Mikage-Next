#!/usr/bin/env python3
"""Compile and exercise the production immutable Emote resource LRU component."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    args = parser.parse_args()
    cxx = args.cxx or shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        parser.error("a C++17 compiler is required")
    production = ROOT / "Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer"
    tests = r'''
#include "emoteresourcecache.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

struct DecodedResource {
    static int alive;
    const int tag;
    const std::vector<int> samples;
    explicit DecodedResource(int id) : tag(id), samples{1, 2, 3, id} {
        if (id < 0) throw std::runtime_error("decode failure");
        ++alive;
    }
    ~DecodedResource() { --alive; }
};
int DecodedResource::alive = 0;
using Key = std::pair<std::string, int>;
using Cache = emoteplayer::EmoteResourceCache<Key, DecodedResource>;
static_assert(std::is_const_v<typename Cache::Handle::element_type>);
static_assert(std::is_same_v<decltype(std::declval<Cache&>().Find(std::declval<const Key&>())),
                             std::shared_ptr<const DecodedResource>>);
static_assert(!std::is_copy_constructible_v<Cache>);
Cache::Handle makeResource(int tag) { return std::make_shared<const DecodedResource>(tag); }

struct ThrowingKey {
    static bool failCopy;
    int tag;
    explicit ThrowingKey(int value) : tag(value) {}
    ThrowingKey(const ThrowingKey& other) : tag(other.tag) {
        if (failCopy) throw std::runtime_error("key copy failure");
    }
    bool operator<(const ThrowingKey& other) const { return tag < other.tag; }
};
bool ThrowingKey::failCopy = false;

int main() {
    const Key a{"canonical/a.psb", 11}, b{"canonical/b.psb", 11};
    const Key c{"canonical/c.psb", 11}, otherSeed{"canonical/a.psb", 12};
    {
        Cache cache(10);
        assert(cache.BudgetBytes() == 10);
        assert(!cache.Find(a));
        assert(cache.Insert(a, makeResource(1), 4));
        assert(cache.Insert(b, makeResource(2), 4));
        auto aHandle = cache.Find(a); // promote A; B is now least recently used
        assert(aHandle && aHandle->tag == 1);
        assert(cache.Insert(c, makeResource(3), 4));
        assert(!cache.Find(b));
        assert(cache.Find(a) == aHandle);
        assert(cache.Find(c)->tag == 3);
        auto stats = cache.GetStats();
        assert(stats.entries == 2 && stats.retainedBytes == 8 && stats.evictions == 1);
        assert(stats.hits == 3 && stats.misses == 2);

        // Oversized/null/zero-cost insertion must not evict useful resources,
        // including an existing resource with the same key.
        assert(!cache.Insert(a, makeResource(99), 11));
        assert(!cache.Insert(b, makeResource(99), 11));
        assert(!cache.Insert(b, {}, 4));
        assert(!cache.Insert(b, makeResource(99), 0));
        assert(cache.Find(a) == aHandle);
        assert(cache.GetStats().retainedBytes == 8 && cache.GetStats().evictions == 1);

        // Replacement adjusts retained cost and releases only the cache's old
        // reference. A player's previously returned immutable handle stays valid.
        assert(cache.Insert(a, makeResource(4), 7));
        assert(aHandle->tag == 1 && aHandle->samples.back() == 1);
        assert(cache.Find(a)->tag == 4 && !cache.Find(c));
        stats = cache.GetStats();
        assert(stats.entries == 1 && stats.retainedBytes == 7 && stats.evictions == 2);
        assert(cache.Insert(a, makeResource(5), 2));
        assert(cache.GetStats().retainedBytes == 2 && cache.GetStats().entries == 1);
        assert(cache.Insert(otherSeed, makeResource(6), 2));
        assert(cache.Find(a)->tag == 5 && cache.Find(otherSeed)->tag == 6);
        assert(!cache.Find(Key{"another/a.psb", 11}));

        // A failed decoder cannot publish a partially constructed resource or
        // disturb the useful cache: construction occurs before Insert.
        stats = cache.GetStats();
        bool threw = false;
        try { cache.Insert(b, makeResource(-1), 2); }
        catch (const std::runtime_error&) { threw = true; }
        assert(threw && cache.GetStats().entries == stats.entries);
        assert(cache.GetStats().retainedBytes == stats.retainedBytes);

        auto active = cache.Find(a);
        const auto beforeClear = cache.GetStats();
        const auto generation = cache.Generation();
        cache.Clear();
        stats = cache.GetStats();
        assert(stats.entries == 0 && stats.retainedBytes == 0);
        assert(stats.generation == generation + 1 && cache.Generation() == stats.generation);
        assert(stats.hits == beforeClear.hits && stats.misses == beforeClear.misses);
        assert(stats.evictions == beforeClear.evictions);
        assert(active->tag == 5 && aHandle->tag == 1);
        // An in-flight result carrying the old generation cannot revive data.
        assert(!cache.Insert(a, makeResource(7), 2, generation));
        assert(cache.GetStats().entries == 0);
        assert(cache.Insert(a, makeResource(8), 2, cache.Generation()));
        assert(cache.Find(a)->tag == 8);
        assert(cache.Insert(otherSeed, makeResource(9), 2));
        assert(cache.Insert(b, makeResource(10), 2));
        auto invalidatedHandle = cache.Find(otherSeed);
        const auto generationBeforeErase = cache.Generation();
        const auto evictionsBeforeErase = cache.GetStats().evictions;
        assert(cache.EraseIf([&](const Key& key) { return key.first == a.first; }) == 2);
        assert(cache.Generation() == generationBeforeErase + 1);
        assert(cache.GetStats().entries == 1 && cache.GetStats().retainedBytes == 2);
        assert(cache.GetStats().evictions == evictionsBeforeErase);
        assert(invalidatedHandle->tag == 9 && cache.Find(b)->tag == 10);
        const auto generationBeforeMissingErase = cache.Generation();
        assert(cache.EraseIf([](const Key& key) { return key.first == "not-resident.psb"; }) == 0);
        assert(cache.Generation() == generationBeforeMissingErase + 1);
        assert(!cache.Insert(c, makeResource(11), 2, generationBeforeMissingErase));
        assert(cache.GetStats().entries == 1);
    }
    assert(DecodedResource::alive == 0);

    Cache::Handle surviving;
    {
        Cache cache(4);
        assert(cache.Insert(a, makeResource(20), 4));
        surviving = cache.Find(a);
        assert(cache.Insert(b, makeResource(21), 4));
        assert(!cache.Find(a));
        assert(surviving->tag == 20); // survives LRU eviction
    }
    assert(surviving->tag == 20); // also survives cache destruction
    surviving.reset();
    assert(DecodedResource::alive == 0);
    {
        Cache disabled(0);
        assert(!disabled.Insert(a, makeResource(1), 1));
        assert(!disabled.Insert(a, makeResource(1), 0));
        assert(disabled.GetStats().entries == 0);
        Cache huge(std::numeric_limits<std::size_t>::max());
        assert(huge.Insert(a, makeResource(1), std::numeric_limits<std::size_t>::max()));
        assert(huge.Insert(b, makeResource(2), 1));
        assert(huge.GetStats().retainedBytes == 1 && huge.GetStats().entries == 1);
        assert(huge.GetStats().evictions == 1); // addition did not wrap
    }
    assert(DecodedResource::alive == 0);
    {
        emoteplayer::EmoteResourceCache<ThrowingKey, DecodedResource> cache(1);
        const ThrowingKey resident(1), incoming(2);
        assert(cache.Insert(resident, makeResource(1), 1));
        ThrowingKey::failCopy = true;
        bool threw = false;
        try { cache.Insert(incoming, makeResource(2), 1); }
        catch (const std::runtime_error&) { threw = true; }
        ThrowingKey::failCopy = false;
        assert(threw && cache.Find(resident)->tag == 1);
        assert(cache.GetStats().entries == 1 && cache.GetStats().retainedBytes == 1);
        assert(cache.GetStats().evictions == 0 && DecodedResource::alive == 1);
    }
    assert(DecodedResource::alive == 0);
    {
        // Exercise the component's synchronization independently of decoders.
        emoteplayer::EmoteResourceCache<int, int> cache(8);
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 4; ++worker) {
            workers.emplace_back([&, worker] {
                for (int i = 0; i < 100; ++i) {
                    const int key = worker * 100 + i;
                    assert(cache.Insert(key, std::make_shared<const int>(key), 1));
                    const auto resource = cache.Find(key);
                    if (resource) assert(*resource == key);
                    const auto stats = cache.GetStats();
                    assert(stats.entries <= 8 && stats.retainedBytes == stats.entries);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        const auto stats = cache.GetStats();
        assert(stats.hits + stats.misses == 400);
        assert(stats.entries == 8 && stats.retainedBytes == 8 && stats.evictions == 392);
    }
    std::cout << "PASS: immutable Emote resource LRU budget, promotion, replacement, ownership, failure, generation and key isolation\n";
}
'''
    with tempfile.TemporaryDirectory(prefix="mikage-emote-shared-cache-") as directory:
        unit = Path(directory) / "emote-shared-cache.cpp"
        exe = Path(directory) / ("emote-shared-cache.exe" if os.name == "nt" else "emote-shared-cache")
        unit.write_text(tests, encoding="utf-8")
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-I", str(production), str(unit), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        archive = (production.parent.parent / "core/archive/XP3Archive.cpp").read_text(encoding="utf-8")
        filters = r'''
#include <cassert>
using tTVPXP3ArchiveExtractionFilter = void (*)();
using tTVPXP3ArchiveContentFilter = void (*)();
static tTVPXP3ArchiveExtractionFilter TVPXP3ArchiveExtractionFilter = nullptr;
static tTVPXP3ArchiveContentFilter TVPXP3ArchiveContentFilter = nullptr;
int clears = 0;
namespace emoteplayer { void ClearSharedEmoteResourceCache() { ++clears; } }
'''
        filters += "\n".join(function(archive, signature) for signature in [
            "void TVPSetXP3ArchiveExtractionFilter(", "void TVPSetXP3ArchiveContentFilter(",
            "bool TVPHasXP3ArchiveFilters(",
        ])
        filters += r'''
void callback() {}
int main() {
    assert(!TVPHasXP3ArchiveFilters());
    TVPSetXP3ArchiveExtractionFilter(callback);
    assert(TVPHasXP3ArchiveFilters() && clears == 1);
    TVPSetXP3ArchiveExtractionFilter(callback);
    assert(TVPHasXP3ArchiveFilters() && clears == 2);
    TVPSetXP3ArchiveExtractionFilter(nullptr);
    assert(!TVPHasXP3ArchiveFilters() && clears == 3);
    TVPSetXP3ArchiveContentFilter(callback);
    assert(TVPHasXP3ArchiveFilters() && clears == 4);
    TVPSetXP3ArchiveContentFilter(callback);
    assert(TVPHasXP3ArchiveFilters() && clears == 5);
    TVPSetXP3ArchiveContentFilter(nullptr);
    assert(!TVPHasXP3ArchiveFilters() && clears == 6);
}
'''
        unit.write_text(filters, encoding="utf-8")
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(unit), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print("PASS: XP3 filter cache bypass detection and invalidation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
