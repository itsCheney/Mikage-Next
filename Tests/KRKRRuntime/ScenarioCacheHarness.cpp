// Same short storage names, different game contents. A stale cache can execute
// game B's version.ks/custom.ks in game A despite loading A's TJS classes.
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void finishSession() {
    // Production OnExit resets events before delivering its compact event.
    TVPResetEventState();
    for (auto* hook : TVPCompactEventVector) hook->OnCompact(TVP_COMPACT_LEVEL_MAX);
    TJSClearRegisterHeap();
}

void scenario(const std::string& name, const std::string& expected) {
    auto* item = TVPGetScenario(name, false);
    const auto actual = item->contents;
    item->Release();
    require(actual == expected, "stale " + name + ": expected '" + expected +
            "', got '" + actual + "'");
}

int main() {
    try {
        // Include A->B->A, A->A->B and repeated identical-game sessions; carry
        // static state through all rounds, just like an embedded app process.
        const std::vector<std::string> games = {"A", "B", "A", "A", "A", "B", "B", "B", "B"};
        for (size_t round = 0; round < games.size(); ++round) {
            const auto game = games[round] + "-run" + std::to_string(round + 1);
            gameFiles = {{"version.ks", game + ":revision"},
                         {"custom.ks", game + ":title"}};
            try {
                const int before = fileReads;
                scenario("version.ks", gameFiles.at("version.ks"));
                scenario("custom.ks", gameFiles.at("custom.ks"));
                scenario("version.ks", gameFiles.at("version.ks"));
                require(fileReads == before + 2, "same-session cache did not hit");
                require(_allTJSStaticClearFun.size() <= 1, "session cleanup hook duplicated");
                auto* inlineScript = TVPGetScenario("inline scenario", true);
                require(inlineScript->contents == "inline scenario", "inline scenario changed");
                inlineScript->Release();
                finishSession();
                require(_allTJSStaticClearFun.empty(), "cleanup registry was not consumed");
                // An additional empty teardown must be harmless.
                finishSession();
            } catch (const std::exception& error) {
                throw std::runtime_error("round " + std::to_string(round + 1) + ": " + error.what());
            }
        }
        require(liveScenarios == 0, "scenario survived final session cleanup");
        std::cout << "PASS (9 sessions, cache hits, inline scripts, repeated teardown)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
