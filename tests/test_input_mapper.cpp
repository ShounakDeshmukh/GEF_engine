#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <engine/entity.hpp>
#include <engine/events/commonEvents.hpp>
#include <engine/events/eventManager.hpp>
#include <engine/keyboardState.hpp>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

using engine::KeyboardState;
using engine::Scene;
namespace ev = engine::events;
namespace SC = engine::SC;

namespace {

constexpr auto kShift = SC::SDL_SCANCODE_LSHIFT;
constexpr auto kCtrl = SC::SDL_SCANCODE_LCTRL;
constexpr auto kW = SC::SDL_SCANCODE_W;
constexpr auto kA = SC::SDL_SCANCODE_A;
constexpr auto kD = SC::SDL_SCANCODE_D;

/** Feeds keyboard changes one tick at a time and logs the events they raise, in run order:
 *  "C<id>+" / "C<id>-" for chords, "<key>+" / "<key>-" for keys, with a trailing "c" when
 *  the inputEvent is flagged chorded. */
struct inputRig {
    ev::eventManager events;
    ev::inputMapper keys;
    Scene scene;
    KeyboardState previous;
    KeyboardState now;
    std::int64_t tick = 0;
    std::vector<std::string> log;

    inputRig() {
        events.subscribe<ev::inputChordEvent>([this](const ev::inputChordEvent& e, Scene&) {
            log.push_back("C" + std::to_string(e.chord) + (e.pressed ? "+" : "-"));
        });
        events.subscribe<ev::inputEvent>([this](const ev::inputEvent& e, Scene&) {
            log.push_back(name(e.key) + (e.pressed ? "+" : "-") + (e.chorded ? "c" : ""));
        });
    }

    static std::string name(SC::SDL_Scancode key) {
        switch (key) {
        case kShift: return "Sh";
        case kCtrl: return "Ct";
        case kW: return "W";
        case kA: return "A";
        case kD: return "D";
        default: return std::to_string(int(key));
        }
    }

    std::string step(std::initializer_list<std::pair<SC::SDL_Scancode, bool>> changes = {}) {
        log.clear();
        previous = now;
        for (auto [key, pressed] : changes) {
            now.setKey(key, pressed);
        }
        ++tick;
        keys.raise(events, now, previous, tick);
        events.run(scene, tick);
        std::string out;
        for (const auto& s : log) {
            out += (out.empty() ? "" : " ") + s;
        }
        return out;
    }
};

} // namespace

TEST_CASE("raiseInputEvents raises one inputEvent per changed key", "[events][input]") {
    ev::eventManager events;
    Scene scene;
    std::vector<std::pair<SC::SDL_Scancode, bool>> got;
    events.subscribe<ev::inputEvent>(
        [&](const ev::inputEvent& e, Scene&) { got.emplace_back(e.key, e.pressed); });
    KeyboardState previous;
    KeyboardState now;
    now.setKey(kW, true);
    now.setKey(kA, true);
    ev::raiseInputEvents(events, now, previous, 1);
    events.run(scene, 1);
    CHECK(got.size() == 2);

    got.clear();
    previous = now;
    now.setKey(kW, false);
    ev::raiseInputEvents(events, now, previous, 2);
    events.run(scene, 2);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == std::pair{kW, false});

    got.clear();
    ev::raiseInputEvents(events, now, now, 3); // no change, no events
    events.run(scene, 3);
    CHECK(got.empty());
}

TEST_CASE("A modifier chord completes on the last press and ends on any release",
          "[events][input]") {
    inputRig rig;
    const int sprint = rig.keys.addChord({kShift, kW});
    REQUIRE(sprint == 0);

    CHECK(rig.step({{kShift, true}}) == "Sh+");
    CHECK(rig.step({{kW, true}}) == "C0+ W+c");     // inputChordEvent first, at priority -1
    CHECK(rig.step() == "");                         // held: nothing new
    CHECK(rig.step({{kW, false}}) == "C0- W-c");
    CHECK(rig.step({{kW, true}}) == "C0+ W+c");      // re-press completes it again
    CHECK(rig.step({{kShift, false}}) == "C0- Sh-");  // Shift's press was plain
    CHECK(rig.step({{kW, false}}) == "W-c");          // W's press was chorded
}

TEST_CASE("A chord pressed within one tick completes whatever the scancode order",
          "[events][input]") {
    inputRig rig;
    rig.keys.addChord({kShift, kW});
    // W's scancode is lower than LShift's, but the chord still sees both keys down
    CHECK(rig.step({{kW, true}, {kShift, true}}) == "C0+ W+c Sh+c");
}

TEST_CASE("A chord completes when the modifier is pressed after the key", "[events][input]") {
    inputRig rig;
    rig.keys.addChord({kShift, kW});
    CHECK(rig.step({{kW, true}}) == "W+");
    CHECK(rig.step({{kShift, true}}) == "C0+ Sh+c");
    CHECK(rig.step({{kShift, false}}) == "C0- Sh-c");
}

TEST_CASE("A windowed chord needs its presses within the window", "[events][input]") {
    inputRig rig;
    const int dash = rig.keys.addChord({kA, kD}, 6);
    const std::string completes = "C" + std::to_string(dash) + "+";

    rig.step({{kA, true}});
    rig.step();
    rig.step();
    CHECK(rig.step({{kD, true}}).starts_with(completes)); // 3 ticks apart
    rig.step({{kA, false}, {kD, false}});

    rig.step({{kA, true}});
    for (int i = 0; i < 10; ++i) {
        rig.step();
    }
    CHECK(rig.step({{kD, true}}) == "D+"); // 11 ticks apart: too slow
}

TEST_CASE("A same-tick chord needs both presses on one tick", "[events][input]") {
    inputRig rig;
    rig.keys.addChord({kA, kD}, 0);
    rig.step({{kA, true}});
    CHECK(rig.step({{kD, true}}) == "D+");
    rig.step({{kA, false}, {kD, false}});
    CHECK(rig.step({{kA, true}, {kD, true}}).starts_with("C0+"));
}

TEST_CASE("The longest completing chord claims shared keys", "[events][input]") {
    inputRig rig;
    const int shortChord = rig.keys.addChord({kShift, kW});
    const int longChord = rig.keys.addChord({kShift, kCtrl, kW});
    rig.step({{kShift, true}, {kCtrl, true}});
    const std::string out = rig.step({{kW, true}});
    CHECK(out.find("C" + std::to_string(longChord) + "+") != std::string::npos);
    CHECK(out.find("C" + std::to_string(shortChord) + "+") == std::string::npos);
}

TEST_CASE("A release is flagged like the press it ends", "[events][input]") {
    inputRig rig;
    rig.keys.addChord({kShift, kW});

    SECTION("chord forms while the key is held") {
        CHECK(rig.step({{kW, true}}) == "W+");
        CHECK(rig.step({{kShift, true}}) == "C0+ Sh+c");
        CHECK(rig.step({{kW, false}}) == "C0- W-");    // plain press, plain release
        CHECK(rig.step({{kShift, false}}) == "Sh-c");
    }
    SECTION("chord ends while the key is held") {
        CHECK(rig.step({{kW, true}, {kShift, true}}) == "C0+ W+c Sh+c");
        CHECK(rig.step({{kShift, false}}) == "C0- Sh-c");
        CHECK(rig.step({{kW, false}}) == "W-c");       // chorded press, chorded release
    }
}

TEST_CASE("A handler that skips chorded events never leaves a key held", "[events][input]") {
    ev::eventManager events;
    ev::inputMapper keys;
    keys.addChord({kShift, kW});
    keys.addChord({kShift, kCtrl, kW});
    keys.addChord({kA, kD}, 3);
    Scene scene;
    std::map<SC::SDL_Scancode, int> held; // what a plain-key handler thinks is down
    events.subscribe<ev::inputEvent>([&](const ev::inputEvent& e, Scene&) {
        if (e.chorded) {
            return;
        }
        held[e.key] += e.pressed ? 1 : -1;
        CHECK(held[e.key] >= 0);
        CHECK(held[e.key] <= 1);
    });

    const SC::SDL_Scancode pool[] = {kShift, kCtrl, kW, kA, kD};
    KeyboardState previous;
    KeyboardState now;
    std::uint32_t seed = 12345;
    for (std::int64_t tick = 1; tick <= 2000; ++tick) {
        previous = now;
        for (auto key : pool) {
            seed = seed * 1664525u + 1013904223u; // fixed LCG, so failures reproduce
            if ((seed >> 24) < 40) {
                now.setKey(key, !now.isKeyPressed(key));
            }
        }
        keys.raise(events, now, previous, tick);
        events.run(scene, tick);
        for (auto key : pool) {
            if (!now.isKeyPressed(key)) {
                INFO("tick " << tick << " key " << int(key));
                CHECK(held[key] == 0);
            }
        }
    }
}

TEST_CASE("An empty chord is rejected", "[events][input]") {
    inputRig rig;
    CHECK(rig.keys.addChord({}) == -1);
}
