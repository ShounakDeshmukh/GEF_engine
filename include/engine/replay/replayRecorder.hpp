#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_set>
#include <vector>

#include "engine/ids.hpp"
#include "engine/networking/bytes.hpp"
#include "engine/networking/sceneReplicator.hpp"

namespace engine { class Scene; }

namespace engine::replay {

    /** Records a Scene every tick into a rolling buffer and plays it back into another
     *  Scene.
     *
     *  Each recorded tick is a keyframe (a full snapshot, every keyframeInterval ticks)
     *  or a delta against the tick before. The buffer keeps the newest maxKeyframes
     *  keyframes with their deltas, so it spans about maxKeyframes * keyframeInterval
     *  ticks.
     *
     *  Captured per entity: Transform, RigidBody, Collider, Shape (with its texture) and
     *  the current SpriteAnimation frame. Text and game-defined components are not.
     *  TextureId and SpriteSheetId are process-local, so a recording plays back only in
     *  the process that made it.
     *
     *  Playback writes into a Scene the caller keeps for it (show it with
     *  SimulationThread::showScene()); it never touches the recorded Scene. Played-back
     *  animations hold their recorded frame rather than advancing.
     *
     *  @thread_safety Not threadsafe. Use from the thread that owns the Scenes. */
    class replayRecorder {
        public:
        explicit replayRecorder(std::int64_t keyframeInterval = 60, std::size_t maxKeyframes = 15);

        /** Captures scene as tick. Call once per tick, after the tick's changes. Ignored
         *  while playing. */
        void record(const Scene& scene, std::int64_t tick);
        /** Drops everything recorded. */
        void clear();

        bool empty() const {return chunks_.empty();}
        /** Oldest and newest recorded ticks; 0 when empty. */
        std::int64_t firstTick() const;
        std::int64_t lastTick() const;

        /** Starts playback at the first recorded tick at or after fromTick (the oldest
         *  one if fromTick is older). Pass a fresh, empty Scene to step() from here on.
         *  Returns false if nothing is recorded. */
        bool startPlayback(std::int64_t fromTick);
        /** Writes the next recorded tick into scene. Returns false, writing nothing,
         *  once the recording is exhausted; playback then stops. */
        bool step(Scene& scene);
        void stopPlayback() {playing_ = false;}
        bool playing() const {return playing_;}
        /** The tick step() last wrote. */
        std::int64_t playbackTick() const {return playTick_;}

        private:
        using table = std::map<NetId, networking::Bytes>;

        struct delta {
            std::int64_t tick;
            networking::Bytes bytes;
        };

        /** A keyframe and the deltas recorded after it, up to the next keyframe. */
        struct chunk {
            std::int64_t tick;
            networking::Bytes keyframe;
            std::vector<delta> deltas;
        };

        bool advance();
        void write(Scene& scene);

        std::int64_t keyframeInterval_;
        std::size_t maxKeyframes_;

        // recording
        networking::sceneReplicator tracker_;
        std::unordered_set<EntityId> tracked_;
        std::deque<chunk> chunks_;
        table last_;

        // playback
        networking::sceneReplicator player_;
        bool playing_ = false;
        bool pendingFirst_ = false;     // step() writes the seek position before advancing
        std::size_t chunkAt_ = 0;
        std::size_t deltaAt_ = 0;       // deltas of chunkAt_ already applied
        table playTable_;
        std::int64_t playTick_ = 0;
    };

}
