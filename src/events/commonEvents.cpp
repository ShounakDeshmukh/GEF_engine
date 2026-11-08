#include "engine/events/commonEvents.hpp"
#include "engine/entity.hpp"
#include "engine/events/eventManager.hpp"
#include "engine/keyboardState.hpp"
#include "engine/physics.hpp"
#include "networking/byteCodec.hpp"

#include <algorithm>
#include <bitset>
#include <vector>

namespace engine::events {

    using networking::Bytes;
    using networking::ByteView;
    using networking::byteReader;
    using networking::byteWriter;

    Bytes collisionEvent::encode() const
    {
        Bytes out;
        byteWriter w(out);
        w.put(a);
        w.put(b);
        return out;
    }

    std::optional<collisionEvent> collisionEvent::decode(ByteView in)
    {
        collisionEvent e;
        byteReader r(in);
        r.get(e.a);
        r.get(e.b);
        if(!r.ok() || !r.atEnd()) {return std::nullopt;}
        return e;
    }

    Bytes deathEvent::encode() const
    {
        Bytes out;
        byteWriter w(out);
        w.put(entity);
        w.put(cause);
        return out;
    }

    std::optional<deathEvent> deathEvent::decode(ByteView in)
    {
        deathEvent e;
        byteReader r(in);
        r.get(e.entity);
        r.get(e.cause);
        if(!r.ok() || !r.atEnd()) {return std::nullopt;}
        return e;
    }

    Bytes spawnEvent::encode() const
    {
        Bytes out;
        byteWriter w(out);
        w.put(entity);
        w.put(owner);
        w.put(position);
        return out;
    }

    std::optional<spawnEvent> spawnEvent::decode(ByteView in)
    {
        spawnEvent e;
        byteReader r(in);
        r.get(e.entity);
        r.get(e.owner);
        r.get(e.position);
        if(!r.ok() || !r.atEnd()) {return std::nullopt;}
        return e;
    }

    void raiseInputEvents(eventManager& events, const KeyboardState& now,
                          const KeyboardState& previous, std::int64_t tick)
    {
        for(int code = 0; code < SC::SDL_SCANCODE_COUNT; ++code)
        {
            const auto key = static_cast<SC::SDL_Scancode>(code);
            const bool pressed = now.isKeyPressed(key);
            if(pressed != previous.isKeyPressed(key)) {events.raise(inputEvent{{}, key, pressed}, tick);}
        }
    }

    int inputMapper::addChord(std::vector<SC::SDL_Scancode> keys, std::int64_t windowTicks)
    {
        if(keys.empty())
        {
            log::error("inputMapper: a chord needs at least one key");
            return -1;
        }
        const int id = static_cast<int>(chords_.size());
        chords_.push_back(chord{std::move(keys), windowTicks});

        longestFirst_.push_back(id);
        std::stable_sort(longestFirst_.begin(), longestFirst_.end(), [this](int a, int b) {
            return chords_[a].keys.size() > chords_[b].keys.size();
        });
        return id;
    }

    void inputMapper::raise(eventManager& events, const KeyboardState& now,
                            const KeyboardState& previous, std::int64_t tick)
    {
        std::vector<SC::SDL_Scancode> changed;
        for(int code = 0; code < SC::SDL_SCANCODE_COUNT; ++code)
        {
            const auto key = static_cast<SC::SDL_Scancode>(code);
            const bool pressed = now.isKeyPressed(key);
            if(pressed == previous.isKeyPressed(key)) {continue;}
            changed.push_back(key);
            if(pressed) {pressedAt_[code] = tick;}
        }

        // the whole tick's keyboard is known here, so the order keys are raised in can't
        // make a chord look incomplete
        std::bitset<SC::SDL_SCANCODE_COUNT> chorded;    // presses flagged on inputEvents
        std::bitset<SC::SDL_SCANCODE_COUNT> claimed;    // presses that completed a chord this tick
        for(int id : longestFirst_)
        {
            chord& c = chords_[id];
            bool allDown = true;
            for(auto key : c.keys) {allDown = allDown && now.isKeyPressed(key);}

            if(c.active)
            {
                if(allDown) {continue;}
                c.active = false;
                events.raise(inputChordEvent{{}, id, false}, tick, -1);
                continue;
            }
            if(!allDown) {continue;}

            bool fresh = false;
            bool taken = false;
            std::int64_t first = tick;
            std::int64_t last = 0;
            for(auto key : c.keys)
            {
                const bool pressedNow = pressedAt_[key] == tick && !previous.isKeyPressed(key);
                fresh = fresh || pressedNow;
                taken = taken || (pressedNow && claimed.test(key));
                first = std::min(first, pressedAt_[key]);
                last = std::max(last, pressedAt_[key]);
            }
            if(!fresh || taken || (c.window >= 0 && last - first > c.window)) {continue;}

            c.active = true;
            for(auto key : c.keys)
            {
                if(pressedAt_[key] == tick && !previous.isKeyPressed(key))
                {
                    claimed.set(key);
                    chorded.set(key);
                }
            }
            events.raise(inputChordEvent{{}, id, true}, tick, -1);
        }

        // A release is flagged like the press it ends, so a handler that skips chorded
        // events always sees whole press/release pairs, even when a chord starts or ends
        // while the key is held.
        for(auto key : changed)
        {
            const bool pressed = now.isKeyPressed(key);
            if(pressed) {pressChorded_.set(key, chorded.test(key));}
            events.raise(inputEvent{{}, key, pressed, pressChorded_.test(key)}, tick);
            if(!pressed) {pressChorded_.reset(key);}
        }
    }

    void raiseCollisions(eventManager& events, const PhysicsSystem& physics, Scene& scene,
                         std::int64_t tick)
    {
        std::vector<EntityId> bodies;
        bodies.reserve(scene.colliders().size());
        for(const auto& [entity, collider] : scene.colliders()) {bodies.push_back(entity);}

        for(std::size_t i = 0; i < bodies.size(); ++i)
        {
            for(std::size_t j = i + 1; j < bodies.size(); ++j)
            {
                if(physics.isCollision(scene, bodies[i], bodies[j]))
                {
                    events.raise(collisionEvent{{}, bodies[i], bodies[j]}, tick);
                }
            }
        }
    }

}
