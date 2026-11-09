#include "engine/replay/replayController.hpp"
#include "engine/events/commonEvents.hpp"
#include "engine/events/eventManager.hpp"
#include "engine/log.hpp"
#include "engine/simulationThread.hpp"

namespace engine::replay {

    using namespace engine::events;

    namespace {
        // recorded ticks one game tick may play: enough for kMaxPlaybackSpeed
        constexpr int kMaxStepsPerTick = static_cast<int>(replayController::kMaxPlaybackSpeed);
    }

    replayController::replayController(eventManager& events, SimulationThread& sim,
                                       std::int64_t ticksPerSecond, std::int64_t keyframeInterval,
                                       std::size_t maxKeyframes)
        : events_(events), sim_(sim), recorder_(keyframeInterval, maxKeyframes),
          gameClock_(&gameTicks_, ticksPerSecond), playTime_(gameClock_, ticksPerSecond)
    {
        events_.subscribe<replayRecordStart>([this](const replayRecordStart&, Scene&) {
            if(recorder_.playing())
            {
                log::warn("replayController: can't record during playback");
                return;
            }
            recorder_.clear();
            recording_ = true;
            log::info("replay: recording");
        });

        events_.subscribe<replayRecordStop>([this](const replayRecordStop&, Scene&) {
            if(!recording_) {return;}
            recording_ = false;
            log::info("replay: recorded ticks {}..{}", recorder_.firstTick(), recorder_.lastTick());
        });

        events_.subscribe<replayPlayStart>([this](const replayPlayStart& e, Scene&) {
            recording_ = false;
            if(!recorder_.startPlayback(e.fromTick))
            {
                log::warn("replayController: nothing recorded to play");
                return;
            }
            replayScene_ = Scene{};     // the recorder plays into a fresh Scene each time
            recorder_.step(replayScene_);   // show the first frame at once
            // Start the replay clock at this tick, so update() on this same tick doesn't
            // count it as elapsed playback and skip the first frame.
            if(e.tick > gameTicks_) {gameTicks_ = e.tick;}
            playTime_.unpause();
            stepper_.emplace(playTime_, kMaxStepsPerTick);
            sim_.showScene(&replayScene_);
            log::info("replay: playing at {}x", playTime_.speedMultiplier());
        });

        events_.subscribe<replayPlayStop>([this](const replayPlayStop&, Scene&) {
            recorder_.stopPlayback();
            stepper_.reset();
            sim_.showScene(nullptr);
            log::info("replay: stopped");
        });

        events_.subscribe<replayPause>([this](const replayPause&, Scene&) {
            if(!recorder_.playing()) {return;}
            playTime_.pause();
            log::info("replay: paused at tick {}", recorder_.playbackTick());
        });

        events_.subscribe<replayResume>([this](const replayResume&, Scene&) {
            if(!recorder_.playing()) {return;}
            playTime_.unpause();
            log::info("replay: resumed");
        });

        events_.subscribe<replaySpeed>([this](const replaySpeed& e, Scene&) {
            float multiplier = e.multiplier;
            if(multiplier > kMaxPlaybackSpeed)
            {
                log::warn("replayController: speed {}x is above the {}x maximum; using {}x",
                          multiplier, kMaxPlaybackSpeed, kMaxPlaybackSpeed);
                multiplier = kMaxPlaybackSpeed;
            }
            // Timeline rejects zero, negative and NaN speeds itself, keeping the old one
            playTime_.setSpeedMultiplier(multiplier);
            log::info("replay: speed {}x", playTime_.speedMultiplier());
        });
    }

    void replayController::update(const Scene& live, std::int64_t tick)
    {
        // game ticks never go backwards, as the clock requires
        if(tick > gameTicks_) {gameTicks_ = tick;}

        if(recorder_.playing() && stepper_)
        {
            stepper_->beginFrame();
            while(stepper_->step())
            {
                // step() ends playback when the recording runs out; the stop event then
                // restores the live Scene on the next run()
                if(!recorder_.step(replayScene_))
                {
                    events_.raise(replayPlayStop{}, tick);
                    break;
                }
            }
            return;
        }
        if(recording_) {recorder_.record(live, tick);}
    }

}
