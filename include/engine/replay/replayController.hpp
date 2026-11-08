#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "engine/entity.hpp"
#include "engine/timeline.hpp"
#include "replayRecorder.hpp"

namespace engine { class SimulationThread; }
namespace engine::events { class eventManager; }

namespace engine::replay {

    /** Runs replays from the common replay events (engine/events/commonEvents.hpp):
     *  replayRecordStart / replayRecordStop, replayPlayStart / replayPlayStop, and the
     *  playback controls replayPause / replayResume / replaySpeed.
     *
     *  It subscribes the handlers for those types, so the game raises them but must not
     *  subscribe to them itself. During playback it shows its own replay Scene through
     *  SimulationThread::showScene(); the live Scene is never modified.
     *
     *  Playback runs on a replay Timeline: a clock that advances with the game ticks
     *  passed to update(), with a Timeline on top of it whose pause and speed are the
     *  playback controls. A Stepper on that Timeline decides how many recorded ticks each
     *  game tick plays: two at speed 2, one every other game tick at 0.5, none while paused.
     *  Pausing the game stops update() calls, so it pauses playback too.
     *
     *  In onTick, call update() after events.run(), and skip the game's own simulation while
     *  playing() is true:
     *
     *      events.run(ctx.scene, ctx.tick);
     *      replays.update(ctx.scene, ctx.tick);
     *      if(replays.playing()) {return;}
     *
     *  @thread_safety Not threadsafe. Construct before SimulationThread::start(); afterwards
     *  use it only from the sim thread. It must outlive sim.stop(). */
    class replayController {
        public:
        /** The fastest playback: replaySpeed above it plays at this speed instead. */
        static constexpr float kMaxPlaybackSpeed = 8.f;

        /** ticksPerSecond is the game's tick rate (gameTime's), so speed 1 plays back in
         *  real time. */
        replayController(events::eventManager& events, SimulationThread& sim,
                         std::int64_t ticksPerSecond = 60, std::int64_t keyframeInterval = 60,
                         std::size_t maxKeyframes = 120);
        replayController(const replayController&) = delete;
        replayController& operator=(const replayController&) = delete;

        /** Records live as tick while recording; plays the recorded ticks due by tick while
         *  playing, raising replayPlayStop when the recording runs out. */
        void update(const Scene& live, std::int64_t tick);

        bool recording() const {return recording_;}
        bool playing() const {return recorder_.playing();}
        bool playbackPaused() const {return playTime_.paused();}
        /** The speed playback runs at, after clamping to kMaxPlaybackSpeed. */
        float playbackSpeed() const {return playTime_.speedMultiplier();}
        const replayRecorder& recorder() const {return recorder_;}

        private:
        events::eventManager& events_;
        SimulationThread& sim_;
        replayRecorder recorder_;
        Scene replayScene_;
        bool recording_ = false;

        // the replay clock: gameTicks_ is the last tick update() saw
        std::int64_t gameTicks_ = 0;
        Timeline gameClock_;
        Timeline playTime_;                 // pause and speed live here
        std::optional<Stepper> stepper_;    // made at each playback start, so no backlog
    };

}
