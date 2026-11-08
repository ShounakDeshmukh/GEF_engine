#include "engine/replay/replayRecorder.hpp"
#include "engine/entity.hpp"
#include "networking/byteCodec.hpp"
#include "networking/snapshotDelta.hpp"

#include <utility>

namespace engine::replay {

    using networking::Bytes;

    namespace {
        // tracker_ owns the recorded NetIds; player_ needs a different id or apply()
        // would skip them as its own
        constexpr ClientId kRecorderId = kServerId;
        constexpr ClientId kPlayerId = 1;

        enum : std::uint8_t {
            kHasTexture   = 1 << 0,
            kHasAnimation = 1 << 1,
        };

        /** What the snapshot format leaves out: process-local texture and sprite-sheet ids. */
        struct looks {
            std::uint8_t flags = 0;
            TextureId texture = 0;
            SpriteSheetId sheet = 0;
            std::uint32_t frame = 0;
        };

        Bytes encodeLooks(const Scene& scene, EntityId entity)
        {
            looks l;
            if(const Shape* shape = scene.getShape(entity); shape && shape->texture)
            {
                l.flags |= kHasTexture;
                l.texture = *shape->texture;
            }
            if(const SpriteAnimation* anim = scene.getSpriteAnimation(entity);
               anim && anim->currentFrame < anim->frames.size())
            {
                l.flags |= kHasAnimation;
                l.sheet = anim->sheet;
                l.frame = anim->frames[anim->currentFrame].index;
            }

            // field by field: struct padding would make identical looks compare unequal in deltas
            Bytes out;
            networking::byteWriter w(out);
            w.put(l.flags);
            w.put(l.texture);
            w.put(l.sheet);
            w.put(l.frame);
            return out;
        }

        void applyLooks(Scene& scene, EntityId entity, const Bytes* bytes)
        {
            looks l;
            if(bytes)
            {
                networking::byteReader in(*bytes);
                in.get(l.flags);
                in.get(l.texture);
                in.get(l.sheet);
                in.get(l.frame);
                if(!in.ok()) {l = {};}
            }

            if(Shape* shape = scene.getShape(entity))
            {
                shape->texture = (l.flags & kHasTexture) ? std::optional<TextureId>(l.texture) : std::nullopt;
            }
            if(l.flags & kHasAnimation)
            {
                // a single held frame: playback shows what was recorded, it doesn't animate
                SpriteAnimation anim;
                anim.sheet = l.sheet;
                anim.frames = {AnimationFrame{l.frame}};
                anim.loop = false;
                scene.addSpriteAnimation(entity, std::move(anim));
            }
            else {scene.removeSpriteAnimation(entity);}
        }
    }

    replayRecorder::replayRecorder(std::int64_t keyframeInterval, std::size_t maxKeyframes)
        : keyframeInterval_(keyframeInterval > 0 ? keyframeInterval : 1),
          maxKeyframes_(maxKeyframes > 0 ? maxKeyframes : 1),
          tracker_(kRecorderId),
          player_(kPlayerId)
    {}

    void replayRecorder::record(const Scene& scene, std::int64_t tick)
    {
        if(playing_) {return;}

        // EntityIds are never reused, so a tracked id missing from the scene is gone for good
        std::erase_if(tracked_, [&](EntityId entity) {
            if(scene.hasEntity(entity)) {return false;}
            tracker_.untrack(entity);
            return true;
        });
        for(const auto& [entity, transform] : scene.transforms())
        {
            tracked_.insert(entity);
            tracker_.setExtra(tracker_.track(entity), encodeLooks(scene, entity));
        }

        Bytes snapshot = tracker_.encodeOwned(scene);
        table current;
        networking::splitSnapshot(snapshot, current);

        if(chunks_.empty() || tick - chunks_.back().tick >= keyframeInterval_)
        {
            chunks_.push_back(chunk{tick, std::move(snapshot), {}});
            // whole chunks only: a delta is meaningless without the keyframe before it
            if(chunks_.size() > maxKeyframes_) {chunks_.pop_front();}
        }
        else
        {
            chunks_.back().deltas.push_back(delta{tick, networking::encodeDelta(last_, current)});
        }
        last_ = std::move(current);
    }

    void replayRecorder::clear()
    {
        chunks_.clear();
        last_.clear();
        playing_ = false;
    }

    std::int64_t replayRecorder::firstTick() const
    {
        return chunks_.empty() ? 0 : chunks_.front().tick;
    }

    std::int64_t replayRecorder::lastTick() const
    {
        if(chunks_.empty()) {return 0;}
        const chunk& newest = chunks_.back();
        return newest.deltas.empty() ? newest.tick : newest.deltas.back().tick;
    }

    bool replayRecorder::startPlayback(std::int64_t fromTick)
    {
        if(chunks_.empty()) {return false;}

        chunkAt_ = 0;
        for(std::size_t i = 0; i < chunks_.size(); ++i)
        {
            if(chunks_[i].tick <= fromTick) {chunkAt_ = i;}
        }
        deltaAt_ = 0;
        networking::splitSnapshot(chunks_[chunkAt_].keyframe, playTable_);
        playTick_ = chunks_[chunkAt_].tick;
        while(playTick_ < fromTick && advance()) {}

        player_ = networking::sceneReplicator(kPlayerId);   // the caller passes a fresh Scene
        playing_ = true;
        pendingFirst_ = true;
        return true;
    }

    bool replayRecorder::step(Scene& scene)
    {
        if(!playing_) {return false;}

        if(pendingFirst_) {pendingFirst_ = false;}
        else if(!advance())
        {
            playing_ = false;
            return false;
        }
        write(scene);
        return true;
    }

    bool replayRecorder::advance()
    {
        const chunk& current = chunks_[chunkAt_];
        if(deltaAt_ < current.deltas.size())
        {
            const delta& next = current.deltas[deltaAt_++];
            networking::applyDelta(playTable_, next.bytes);
            playTick_ = next.tick;
            return true;
        }
        if(chunkAt_ + 1 < chunks_.size())
        {
            ++chunkAt_;
            deltaAt_ = 0;
            networking::splitSnapshot(chunks_[chunkAt_].keyframe, playTable_);
            playTick_ = chunks_[chunkAt_].tick;
            return true;
        }
        return false;
    }

    void replayRecorder::write(Scene& scene)
    {
        player_.apply(scene, networking::joinSnapshot(playTable_), kRecorderId);
        for(const auto& [id, record] : playTable_)
        {
            if(auto entity = player_.entityOf(id)) {applyLooks(scene, *entity, player_.extraOf(id));}
        }
    }

}
