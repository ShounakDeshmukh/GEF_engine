// event_replay_demo: a square moved with WASD, with every action going through the
// eventManager, including recording and playing back a replay.
//
// Controls:
//   W A S D   move
//   L-Shift   hold with a move key to sprint (a chord)
//   R         start recording / stop recording
//   P         play the recording (P again stops it early)
//   Space     pause / resume playback
//   1 2 3 4   playback speed 0.5x, 1x, 2x, 4x
//
// The square in the top-left corner shows the state: grey idle, red recording, green playing,
// yellow paused; the bar beside it shows playback speed. main draws both from values the sim
// sets, so they are not part of the Scene and never recorded.
//
// Events, all from engine/events/commonEvents.hpp:
//   inputEvent          raised by inputMapper, one per key change
//   inputChordEvent          raised by inputMapper when Shift + a move key goes down or up
//   replayRecordStart   R while not recording
//   replayRecordStop    R while recording
//   replayPlayStart     P while not playing (stops a recording in progress first)
//   replayPlayStop      P while playing; replayController raises it when the recording ends
//   replayPause         Space while playing
//   replayResume        Space while paused
//   replaySpeed         1 2 3 4
//
// The game only raises the replay events; replay::replayController handles them, recording
// the live Scene and showing playback in a Scene of its own, so the live game is never
// modified by a replay.

#include <algorithm>
#include <atomic>
#include <engine/engine.hpp>
#include <optional>

namespace {

namespace SC = engine::SC;
namespace ev = engine::events;

constexpr float worldWidth = 1280.f;
constexpr float worldHeight = 720.f;
constexpr float playerSize = 48.f;
constexpr float playerSpeed = 360.f; // pixels per second

constexpr engine::Color idleColor{120, 120, 120, 255};
constexpr engine::Color recordingColor{220, 50, 50, 255};
constexpr engine::Color playingColor{60, 200, 90, 255};
constexpr engine::Color pausedColor{230, 200, 50, 255};

} // namespace

int main() {
    engine::log::init();

    engine::Window window("event_replay_demo", static_cast<int>(worldWidth),
                          static_cast<int>(worldHeight));
    engine::Renderer renderer(window);
    engine::InputHandler input;

    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);

    engine::Scene scene;
    const engine::EntityId player = scene.createEntity();
    scene.transform(player).position = {worldWidth / 2.f, worldHeight / 2.f};
    scene.addShape(player, {.size = {playerSize, playerSize}, .color = {240, 240, 255, 255}});

    // Everything below is captured by onTick and belongs to the sim thread once sim.start()
    // runs, until sim.stop() returns.
    ev::eventManager events;
    std::optional<engine::replay::replayController> replays; // needs the sim; built below
    bool up = false, left = false, down = false, right = false;
    int sprintChords = 0; // Shift + move-key chords currently held

    // Shift + each move key is a chord with no time window: hold Shift, then press the key, or
    // press Shift while already moving.
    ev::inputMapper keys;
    for (const SC::SDL_Scancode key : {SC::SDL_SCANCODE_W, SC::SDL_SCANCODE_A,
                                       SC::SDL_SCANCODE_S, SC::SDL_SCANCODE_D}) {
        keys.addChord({SC::SDL_SCANCODE_LSHIFT, key});
    }
    // The only state main reads while the sim runs: 0 idle, 1 recording, 2 playing,
    // 3 paused; and the playback speed.
    std::atomic<int> mode{0};
    std::atomic<float> speed{1.f};

    // Sprinting is a modifier on top of walking, so the input handler below still tracks move
    // keys even when inputEvent::chorded is set; a chord that replaced a key's own action
    // would skip those.
    events.subscribe<ev::inputChordEvent>([&](const ev::inputChordEvent& e, engine::Scene&) {
        sprintChords += e.pressed ? 1 : -1;
    });

    // One handler for every key: movement keys update the held state, the others raise the
    // replay events.
    events.subscribe<ev::inputEvent>([&](const ev::inputEvent& e, engine::Scene&) {
        switch (e.key) {
        case SC::SDL_SCANCODE_W: up = e.pressed; break;
        case SC::SDL_SCANCODE_A: left = e.pressed; break;
        case SC::SDL_SCANCODE_S: down = e.pressed; break;
        case SC::SDL_SCANCODE_D: right = e.pressed; break;
        case SC::SDL_SCANCODE_R:
            if (e.pressed && !replays->playing()) {
                if (replays->recording()) {
                    events.raise(ev::replayRecordStop{}, events.now());
                } else {
                    events.raise(ev::replayRecordStart{}, events.now());
                }
            }
            break;
        case SC::SDL_SCANCODE_P:
            if (e.pressed) {
                if (replays->playing()) {
                    events.raise(ev::replayPlayStop{}, events.now());
                } else {
                    events.raise(ev::replayPlayStart{}, events.now());
                }
            }
            break;
        case SC::SDL_SCANCODE_SPACE:
            if (e.pressed && replays->playing()) {
                if (replays->playbackPaused()) {
                    events.raise(ev::replayResume{}, events.now());
                } else {
                    events.raise(ev::replayPause{}, events.now());
                }
            }
            break;
        case SC::SDL_SCANCODE_1:
        case SC::SDL_SCANCODE_2:
        case SC::SDL_SCANCODE_3:
        case SC::SDL_SCANCODE_4:
            if (e.pressed) {
                const float speeds[] = {0.5f, 1.f, 2.f, 4.f};
                events.raise(ev::replaySpeed{{}, speeds[e.key - SC::SDL_SCANCODE_1]}, events.now());
            }
            break;
        default: break;
        }
    });

    auto onTick = [&](const engine::TickContext& ctx) {
        keys.raise(events, ctx.keyboard, ctx.previousKeyboard, ctx.tick);
        events.run(ctx.scene, ctx.tick);

        replays->update(ctx.scene, ctx.tick);
        mode = replays->playing()     ? (replays->playbackPaused() ? 3 : 2)
               : replays->recording() ? 1
                                      : 0;
        speed = replays->playbackSpeed();
        if (replays->playing()) {
            return; // the live game is frozen while the replay shows
        }

        const glm::vec2 direction{(right ? 1.f : 0.f) - (left ? 1.f : 0.f),
                                  (down ? 1.f : 0.f) - (up ? 1.f : 0.f)};
        glm::vec2& position = ctx.scene.transform(player).position;
        position += direction * playerSpeed * (sprintChords > 0 ? 2.f : 1.f) * ctx.dt;
        position.x = std::clamp(position.x, 0.f, worldWidth - playerSize);
        position.y = std::clamp(position.y, 0.f, worldHeight - playerSize);
    };

    engine::SimulationThread sim(std::move(scene), gameTime, onTick);
    // 60 ticks per second like gameTime; a keyframe a second, up to two minutes
    replays.emplace(events, sim, 60, 60, 120);
    sim.start();

    std::optional<engine::RenderFrame> current;
    while (!window.shouldClose()) {
        window.pollEvents();
        sim.submitKeyboard(engine::KeyboardState::capture(input));

        if (auto frame = sim.takeRenderFrame()) {
            current = std::move(*frame);
        }
        if (sim.failure()) {
            engine::log::error("simulation failed; exiting");
            break;
        }

        renderer.clear({25, 25, 35, 255});
        if (current) {
            renderer.drawEntities(current->scene);
        }
        const int shown = mode.load();
        renderer.fillRect({16.f, 16.f}, {24.f, 24.f},
                          shown == 1   ? recordingColor
                          : shown == 2 ? playingColor
                          : shown == 3 ? pausedColor
                                       : idleColor);
        renderer.fillRect({48.f, 22.f}, {40.f * speed.load(), 12.f}, idleColor);
        renderer.present();
    }

    sim.stop();
    return sim.failure() ? 1 : 0;
}
