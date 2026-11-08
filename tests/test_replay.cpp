#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <engine/entity.hpp>
#include <engine/events/commonEvents.hpp>
#include <engine/events/eventManager.hpp>
#include <engine/replay/replayController.hpp>
#include <engine/replay/replayRecorder.hpp>
#include <engine/simulationThread.hpp>
#include <engine/timeline.hpp>
#include <map>
#include <optional>
#include <vector>

using engine::EntityId;
using engine::Scene;
using engine::replay::replayController;
using engine::replay::replayRecorder;
namespace ev = engine::events;

namespace {

/** Every Transform position in scene, sorted by x then y, so Scenes with different
 *  EntityIds compare. */
std::vector<glm::vec2> positions(const Scene& scene) {
    std::vector<glm::vec2> out;
    for (const auto& [id, t] : scene.transforms()) {
        out.push_back(t.position);
    }
    std::sort(out.begin(), out.end(),
              [](glm::vec2 a, glm::vec2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    return out;
}

/** A live Scene with a mover (x = tick), a textured entity, one that is destroyed at
 *  destroyAt, and one created at createAt; records ticks [0, count) into recorder and
 *  returns the expected positions per tick. */
struct recordedRun {
    Scene live;
    EntityId mover{};
    std::map<std::int64_t, std::vector<glm::vec2>> expected;

    recordedRun(replayRecorder& recorder, std::int64_t count, std::int64_t destroyAt,
                std::int64_t createAt) {
        mover = live.createEntity();
        engine::Shape textured;
        textured.texture = 77;
        live.addShape(mover, textured);
        const EntityId doomed = live.createEntity();
        live.transform(doomed).position = {-100.f, 5.f};
        for (std::int64_t t = 0; t < count; ++t) {
            live.transform(mover).position = {float(t), 1.f};
            if (t == destroyAt) {
                live.destroyEntity(doomed);
            }
            if (t == createAt) {
                const EntityId late = live.createEntity();
                live.transform(late).position = {500.f, 2.f};
            }
            recorder.record(live, t);
            expected[t] = positions(live);
        }
    }
};

} // namespace

// ---- recorder ---------------------------------------------------------------------------------

TEST_CASE("Replay recorder starts empty", "[replay]") {
    replayRecorder recorder(10, 3);
    CHECK(recorder.empty());
    CHECK(recorder.firstTick() == 0);
    CHECK(recorder.lastTick() == 0);
    CHECK_FALSE(recorder.startPlayback(0));
    CHECK_FALSE(recorder.playing());
}

TEST_CASE("Replay recorder evicts whole keyframe chunks", "[replay]") {
    replayRecorder recorder(10, 3); // a keyframe every 10 ticks, keep 3
    Scene live;
    const EntityId e = live.createEntity();
    for (std::int64_t t = 0; t < 30; ++t) {
        live.transform(e).position.x = float(t);
        recorder.record(live, t);
    }
    CHECK(recorder.firstTick() == 0);
    CHECK(recorder.lastTick() == 29);

    // tick 30 starts a fourth chunk, so the oldest whole chunk (0..9) goes
    live.transform(e).position.x = 30.f;
    recorder.record(live, 30);
    CHECK(recorder.firstTick() == 10);
    CHECK(recorder.lastTick() == 30);

    // further deltas don't evict until the next keyframe
    for (std::int64_t t = 31; t < 40; ++t) {
        recorder.record(live, t);
    }
    CHECK(recorder.firstTick() == 10);
    recorder.record(live, 40);
    CHECK(recorder.firstTick() == 20);

    recorder.clear();
    CHECK(recorder.empty());
}

TEST_CASE("Replay playback reproduces every recorded tick", "[replay]") {
    replayRecorder recorder(10, 3);
    recordedRun run(recorder, 100, 85, 90);
    REQUIRE(recorder.firstTick() == 70);
    REQUIRE(recorder.lastTick() == 99);

    REQUIRE(recorder.startPlayback(0)); // older than the buffer: starts at the oldest tick
    Scene replayScene;
    std::int64_t expectedTick = 70;
    while (recorder.step(replayScene)) {
        INFO("tick " << expectedTick);
        REQUIRE(recorder.playbackTick() == expectedTick);
        CHECK(positions(replayScene) == run.expected[expectedTick]);

        bool textured = false;
        for (const auto& [id, t] : replayScene.transforms()) {
            if (const engine::Shape* s = replayScene.getShape(id); s && s->texture == 77) {
                textured = true;
            }
        }
        CHECK(textured);
        ++expectedTick;
    }
    CHECK(expectedTick == 100);
    CHECK_FALSE(recorder.playing());
    CHECK_FALSE(recorder.step(replayScene)); // stays stopped
}

TEST_CASE("Replay playback shows despawns and late spawns", "[replay]") {
    replayRecorder recorder(10, 3);
    recordedRun run(recorder, 100, 85, 90);
    REQUIRE(recorder.startPlayback(84));
    Scene replayScene;
    std::map<std::int64_t, std::size_t> counts;
    while (recorder.step(replayScene)) {
        counts[recorder.playbackTick()] = replayScene.transforms().size();
    }
    CHECK(counts[84] == 2); // mover + doomed
    CHECK(counts[85] == 1); // doomed destroyed
    CHECK(counts[90] == 2); // late created
    CHECK(counts[99] == 2);
}

TEST_CASE("Replay playback can seek into the middle of a chunk", "[replay]") {
    replayRecorder recorder(10, 3);
    recordedRun run(recorder, 100, 85, 90);
    for (std::int64_t from : {70, 73, 79, 80, 87, 99}) {
        INFO("from " << from);
        Scene replayScene;
        REQUIRE(recorder.startPlayback(from));
        REQUIRE(recorder.step(replayScene));
        CHECK(recorder.playbackTick() == from);
        CHECK(positions(replayScene) == run.expected[from]);
        if (from < 99) {
            REQUIRE(recorder.step(replayScene));
            CHECK(positions(replayScene) == run.expected[from + 1]);
        }
        recorder.stopPlayback();
    }
    Scene past;
    REQUIRE(recorder.startPlayback(500)); // past the end: clamps to the newest tick
    REQUIRE(recorder.step(past));
    CHECK(recorder.playbackTick() == 99);
    CHECK(positions(past) == run.expected[99]);
    CHECK_FALSE(recorder.step(past));
}

TEST_CASE("Replay playback leaves the live Scene alone and ignores recording", "[replay]") {
    replayRecorder recorder(10, 3);
    recordedRun run(recorder, 30, -1, -1);
    const auto before = positions(run.live);

    REQUIRE(recorder.startPlayback(0));
    Scene replayScene;
    REQUIRE(recorder.step(replayScene));
    recorder.record(run.live, 1000); // ignored while playing
    while (recorder.step(replayScene)) {
    }
    CHECK(positions(run.live) == before);
    CHECK(recorder.lastTick() == 29);
}

// ---- controller -------------------------------------------------------------------------------

namespace {

/** A replayController on a SimulationThread that is never started: the test plays the sim
 *  thread, calling run() and update() once per game tick. */
struct controllerRig {
    engine::Timeline gameTime;
    engine::SimulationThread sim{Scene{}, gameTime, [](const engine::TickContext&) {}};
    ev::eventManager events;
    replayController replays{events, sim, 60, 10, 10};
    Scene live;
    EntityId mover = live.createEntity();
    std::int64_t tick = 0;

    void step() {
        ++tick;
        events.run(live, tick);
        replays.update(live, tick);
        if (!replays.playing()) {
            live.transform(mover).position.x = float(tick);
        }
    }
    void raiseAndStep(auto event) {
        events.raise(event, tick + 1);
        step();
    }
};

} // namespace

TEST_CASE("Replay events record and play back through replayController", "[replay][events]") {
    controllerRig rig;
    rig.raiseAndStep(ev::replayRecordStart{});
    CHECK(rig.replays.recording());
    for (int i = 0; i < 29; ++i) {
        rig.step();
    }
    rig.raiseAndStep(ev::replayRecordStop{});
    CHECK_FALSE(rig.replays.recording());
    const auto first = rig.replays.recorder().firstTick();
    const auto last = rig.replays.recorder().lastTick();
    CHECK(last - first == 29);

    rig.raiseAndStep(ev::replayPlayStart{});
    REQUIRE(rig.replays.playing());
    CHECK(rig.replays.recorder().playbackTick() == first); // the first frame is shown
    const float liveX = rig.live.transform(rig.mover).position.x;

    // speed 1: one recorded tick per game tick
    for (int i = 0; i < 5; ++i) {
        rig.step();
    }
    CHECK(rig.replays.recorder().playbackTick() == first + 5);
    CHECK(rig.live.transform(rig.mover).position.x == liveX); // the game is held while playing

    // paused: no progress
    rig.raiseAndStep(ev::replayPause{});
    CHECK(rig.replays.playbackPaused());
    const auto pausedAt = rig.replays.recorder().playbackTick();
    for (int i = 0; i < 5; ++i) {
        rig.step();
    }
    CHECK(rig.replays.recorder().playbackTick() == pausedAt);

    // resumed at 2x: two recorded ticks per game tick
    rig.raiseAndStep(ev::replaySpeed{{}, 2.f});
    rig.raiseAndStep(ev::replayResume{});
    CHECK_FALSE(rig.replays.playbackPaused());
    CHECK(rig.replays.playbackSpeed() == 2.f);
    const auto resumedAt = rig.replays.recorder().playbackTick();
    for (int i = 0; i < 4; ++i) {
        rig.step();
    }
    CHECK(rig.replays.recorder().playbackTick() == resumedAt + 8);

    // runs out: replayPlayStop is raised and handled on the next tick
    for (int i = 0; i < 20 && rig.replays.playing(); ++i) {
        rig.step();
    }
    CHECK_FALSE(rig.replays.playing());
    CHECK(rig.replays.recorder().playbackTick() == last);
}

TEST_CASE("replayPlayStop ends playback early and recording can restart", "[replay][events]") {
    controllerRig rig;
    rig.raiseAndStep(ev::replayRecordStart{});
    for (int i = 0; i < 20; ++i) {
        rig.step();
    }
    rig.raiseAndStep(ev::replayPlayStart{});
    CHECK_FALSE(rig.replays.recording()); // playing stops recording
    rig.step();
    rig.raiseAndStep(ev::replayPlayStop{});
    CHECK_FALSE(rig.replays.playing());

    rig.raiseAndStep(ev::replayRecordStart{});
    CHECK(rig.replays.recording());
    for (int i = 0; i < 4; ++i) {
        rig.step();
    }
    CHECK(rig.replays.recorder().lastTick() - rig.replays.recorder().firstTick() == 4);
}

TEST_CASE("replaySpeed above the maximum is clamped and delivered", "[replay][events]") {
    controllerRig rig;
    rig.raiseAndStep(ev::replayRecordStart{});
    for (int i = 0; i < 100; ++i) {
        rig.step();
    }
    rig.raiseAndStep(ev::replayPlayStart{});
    REQUIRE(rig.replays.playing());

    SECTION("above the maximum") {
        rig.raiseAndStep(ev::replaySpeed{{}, 16.f});
        CHECK(rig.replays.playbackSpeed() == replayController::kMaxPlaybackSpeed);
        const auto from = rig.replays.recorder().playbackTick();
        for (int i = 0; i < 4; ++i) {
            rig.step();
        }
        CHECK(rig.replays.recorder().playbackTick() == from + 4 * 8);
    }
    SECTION("within range") {
        rig.raiseAndStep(ev::replaySpeed{{}, 3.f});
        CHECK(rig.replays.playbackSpeed() == 3.f);
        const auto from = rig.replays.recorder().playbackTick();
        for (int i = 0; i < 4; ++i) {
            rig.step();
        }
        CHECK(rig.replays.recorder().playbackTick() == from + 4 * 3);
    }
    SECTION("invalid speeds keep the current one") {
        rig.raiseAndStep(ev::replaySpeed{{}, 2.f});
        rig.raiseAndStep(ev::replaySpeed{{}, 0.f});
        rig.raiseAndStep(ev::replaySpeed{{}, -4.f});
        CHECK(rig.replays.playbackSpeed() == 2.f);
    }
}

TEST_CASE("replayPlayStart with nothing recorded does not start playback", "[replay][events]") {
    controllerRig rig;
    rig.raiseAndStep(ev::replayPlayStart{});
    CHECK_FALSE(rig.replays.playing());
    rig.raiseAndStep(ev::replayPause{}); // ignored when not playing
    CHECK_FALSE(rig.replays.playbackPaused());
}
