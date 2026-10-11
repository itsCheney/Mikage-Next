// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikage contributors. See the runtime's AETHER-NOTICE.md.
#include "emoteanimation.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
using namespace emoteplayer::animation;
static int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) { check(std::abs(actual - expected) <= 1e-9, message); }
Timeline ramp(const std::string& name, const std::string& variable, double end, bool loop = true, bool diff = false) {
    Timeline result; result.label = name; result.loopBegin = loop ? 0 : -1;
    result.loopEnd = loop ? end + 1 : -1; result.lastTime = end + 1; result.difference = diff;
    result.tracks.push_back({variable, {{0, end, 0, true}, {end + 1, 0, 0, false}}, false});
    return result;
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--trajectory") {
            const double fps=std::stod(argv[2]), seconds=std::stod(argv[3]);
            check(std::isfinite(fps) && fps>=1 && fps<=240 && std::isfinite(seconds) && seconds>0 && seconds<=300,"trajectory duration/rate bounds");
            Runtime replay; Timeline idle; idle.label="idle"; idle.loopBegin=0; idle.loopEnd=60; idle.lastTime=60;
            idle.tracks.push_back({"body_UD", {{0,10,0,true},{30,-10,0,true},{60,0,0,false}}, false});
            replay.reset({idle},{{"body_UD",0}}); replay.play("idle",0);
            std::cout << std::setprecision(17) << "sample,seconds,dtFrames,timelineTime,body_UD\n";
            for (int sample=0; sample<=static_cast<int>(fps*seconds); ++sample) {
                if(sample) replay.advance(60/fps);
                std::cout << sample << ',' << sample/fps << ',' << (sample?60/fps:0) << ',' << replay.state("idle")->time << ',' << replay.variable("body_UD") << '\n';
            }
            return 0;
        }
        Animator a; a.assign(0); a.schedule(10, 10, 1); a.advance(5);
        near(a.value, 2.5, "positive easing uses p squared"); a.advance(5); near(a.value, 10, "easing endpoint");
        a.assign(0); a.schedule(10, 10, -1); a.advance(2.5); near(a.value, 5, "negative easing uses sqrt p");
        a.assign(0); a.schedule(10, 10, 0); a.advance(5); a.schedule(15, 10, 0); a.advance(5);
        near(a.value, 10, "interruption starts at current value");
        a.assign(0); a.schedule(10, 1, 0); a.schedule(20, 1, 0, true); a.advance(1.5);
        near(a.value, 15, "queue consumes overshoot"); a.advance(.5); near(a.value, 20, "queue endpoint");
        near(millisecondsToFrames(1000.0 / 60, 20), 1, "D3D one frame advances one authored frame");
        near(millisecondsToFrames(1000, 40), 30, "public speed divisor scaling");
        near(millisecondsToFrames(10, 0), 0, "zero speed pauses");

        Runtime independent; independent.reset({ramp("A", "x", 60), ramp("B", "y", 180)}, {{"x", 0}, {"y", 0}});
        check(independent.play("A", 1), "start A"); independent.advance(5);
        near(independent.variable("x"), 5, "initial ramp"); independent.play("B", 1); independent.advance(1);
        near(independent.variable("x"), 6, "starting B does not reset A"); independent.stop("B"); independent.advance(1);
        near(independent.variable("x"), 7, "stopping B does not reset A"); independent.play("B", 1); independent.advance(62);
        near(independent.state("A")->time, 8, "loop retains overshoot"); near(independent.state("B")->time, 62, "short loop does not reset long loop");
        independent.play("A", 1); check(independent.states().size() == 2, "replay does not duplicate state");
        check(!independent.play("unknown", 0) && independent.playing("B"), "unknown timeline does not stop others");
        independent.play("A", 0); check(!independent.playing("B"), "nonparallel replaces playing timelines");
        independent.stop(); check(!independent.playing(), "stop all");
        Runtime interrupted; interrupted.reset({ramp("A", "x", 60)}, {{"x", 0}});
        interrupted.play("A", 1); interrupted.advance(5); interrupted.setVariable("x", 20, 10);
        near(interrupted.variable("x"), 5, "script transition starts at primary controller's current value");
        interrupted.advance(5); near(interrupted.variable("x"), 12.5, "script write interrupts primary timeline animator");

        Runtime oneShot; oneShot.reset({ramp("once", "x", 3, false)}, {{"x", 0}}); oneShot.play("once", 0);
        near(oneShot.variable("x"), 0, "nonloop starts at time zero"); oneShot.advance(4);
        check(!oneShot.playing("once"), "nonloop finishes"); near(oneShot.variable("x"), 3, "nonloop retains final pose");
        Runtime diff; diff.reset({ramp("breath", "x", 10, true, true)}, {{"x", 100}}); diff.play("breath", 3); diff.advance(10);
        near(diff.variable("x"), 110, "difference adds to base"); diff.setBlend("breath", .5, 0, 0);
        near(diff.variable("x"), 105, "difference ratio weights contribution");
        diff.fadeOut("breath", 2, 0); check(diff.playing("breath"), "fade-out remains playing during transition");
        diff.advance(2); check(!diff.playing("breath"), "fade-out auto-stops"); near(diff.variable("x"), 100, "stopped difference removes contribution");
        diff.fadeIn("breath", 2, 0); near(diff.state("breath")->blend.value, 0, "fade-in starts transparent");
        diff.advance(1); near(diff.state("breath")->blend.value, .5, "fade-in midpoint");
        Runtime fade; fade.reset({ramp("breath", "x", 100, true, true)}, {{"x", 0}}); fade.play("breath", 3);
        fade.fadeOut("breath", 2, 0); fade.advance(10); near(fade.state("breath")->time, 2, "large dt stops at fade boundary");

        Runtime prefix; auto intro = ramp("intro", "x", 20); intro.loopBegin = 5; prefix.reset({intro}, {{"x", 0}});
        prefix.play("intro", 1); prefix.advance(2); near(prefix.state("intro")->time, 2, "nonzero loop begin keeps intro");
        prefix.advance(100); near(prefix.state("intro")->time, 6, "multiple wraps preserve remainder");
        Runtime chunked, whole; auto def = ramp("idle", "x", 60);
        chunked.reset({def}, {{"x", 0}}); whole.reset({def}, {{"x", 0}}); chunked.play("idle", 0); whole.play("idle", 0);
        whole.advance(123.25); for (int i = 0; i < 493; ++i) chunked.advance(.25);
        near(chunked.variable("x"), whole.variable("x"), "fractional and large steps agree");
        near(chunked.state("idle")->time, whole.state("idle")->time, "timeline time is step invariant");
        whole.advance(1e9); check(whole.state("idle")->time < 61, "huge dt uses bounded cycle fast path");
        Runtime changingWhole, changingChunks;
        changingWhole.reset({def}, {{"x", 0}}); changingChunks.reset({def}, {{"x", 0}});
        changingWhole.play("idle", 0); changingChunks.play("idle", 0);
        changingWhole.setVariable("x", 200, 200); changingChunks.setVariable("x", 200, 200);
        changingWhole.advance(130); for (int i=0; i<520; ++i) changingChunks.advance(.25);
        near(changingWhole.variable("x"), changingChunks.variable("x"), "changing base controller is step invariant across loops");
        Runtime sparse; Timeline idle; idle.label="idle"; idle.loopBegin=0; idle.loopEnd=60; idle.lastTime=60;
        idle.tracks.push_back({"body_UD", {{0,10,0,true},{30,-10,0,true},{60,0,0,false}}, false});
        sparse.reset({idle}, {{"body_UD",0}}); sparse.play("idle",0); sparse.advance(10);
        near(sparse.variable("body_UD"),10.0/29*10,"Aether sparse-variable reference uses the authored one-frame offset");
        sparse.advance(50); near(sparse.variable("body_UD"),-10,"loop seek preserves the previous evaluated value");
        sparse.advance(1); near(sparse.variable("body_UD"),-10+20.0/29,"next cycle transitions continuously from its previous endpoint");
        Runtime instant; Timeline discrete; discrete.label = "selector"; discrete.lastTime = 3;
        discrete.tracks.push_back({"choice", {{0, 4, 0, true}, {2, 7, 0, true}, {3, 0, 0, false}}, true});
        instant.reset({discrete}, {{"choice", 0}}); instant.play("selector", 1); near(instant.variable("choice"), 4, "instant key assigns immediately");
        instant.advance(2); near(instant.variable("choice"), 7, "instant crossing");

        Runtime original; original.reset({def}, {{"x", 0}}); original.play("idle", 0); original.advance(5);
        original.setVariable("manual", 20, 10, 1); original.advance(2);
        Runtime clone = original; const auto saved = original.serialize(); original.advance(1);
        near(clone.state("idle")->time, 7, "clone clock independent"); clone.setVariable("manual", -10); check(original.variable("manual") != clone.variable("manual"), "clone variables independent");
        Runtime restored; restored.reset({def}, {{"x", 0}}); check(restored.restore(saved), "snapshot restores"); restored.advance(1);
        near(restored.variable("manual"), original.variable("manual"), "snapshot keeps pending easing");
        near(restored.variable("x"), original.variable("x"), "snapshot keeps timeline track");
        const auto intact = restored.serialize(); check(!restored.restore(saved + "garbage"), "trailing snapshot data rejected");
        check(restored.serialize() == intact, "invalid restore is transactional");
        check(!restored.restore("EMOTE1 999999999 0"), "snapshot size bound");
        Runtime differentResource; auto changed = def; changed.tracks[0].frames[0].value += 1;
        differentResource.reset({changed}, {{"x", 0}});
        check(!differentResource.restore(saved), "snapshot from different authored resource rejected");
        Runtime nul; std::string name("角色"); name += '\0'; auto named = ramp(name, name, 10);
        nul.reset({named}, {{name, 0}}); nul.play(name, 1); nul.advance(2); auto encoded = nul.serialize();
        check(encoded.find('\0') == std::string::npos, "TJS snapshot contains no embedded NUL");
        Runtime decoded; decoded.reset({named}, {{name, 0}}); check(decoded.restore(encoded), "UTF8 NUL labels roundtrip"); near(decoded.variable(name), 2, "UTF8 value roundtrip");
        Runtime seek; seek.reset({def}, {{"x", 0}}); seek.play("idle", 0); seek.advance(20); seek.seek(5);
        near(seek.variable("x"), 5, "backward seek reconstructs track"); seek.seek(65); near(seek.state("idle")->time, 4, "seek wraps authored clock");
        auto unchanged = seek.serialize(); seek.advance(-1); seek.advance(std::numeric_limits<double>::infinity()); seek.advance(std::numeric_limits<double>::quiet_NaN());
        check(seek.serialize() == unchanged, "invalid dt does not modify state");
        Runtime invalid; auto degenerate = ramp("bad", "x", 2); degenerate.loopEnd = degenerate.loopBegin;
        invalid.reset({degenerate}, {{"x", 0}}); invalid.play("bad", 0); invalid.advance(100);
        check(!invalid.playing(), "degenerate loop terminates without spinning");
        std::cout << "Passed " << checks << " production animation checks.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
