#pragma once

/** @file
 *  Event types most games need, and helpers that raise the engine-detected ones.
 *
 *  Entity ids: in a local game, fill entity fields with EntityIds. An event sent over the
 *  network must carry NetIds instead, since EntityIds are process-local; both are
 *  std::uint32_t, so the same field holds either.
 *
 *  collisionEvent, deathEvent and spawnEvent are networked events. Their message types are
 *  in the range the engine reserves for its own events (kEngineEventTypeFirst and up), so
 *  game event kTypes must stay below it. inputEvent and the replay events are local only.
 */

#include <array>
#include <bitset>
#include <cstdint>
#include <glm/vec2.hpp>
#include <optional>
#include <vector>

#include "engine/ids.hpp"
#include "engine/inputScancodes.hpp"
#include "simpleEvent.hpp"

namespace engine {
    class KeyboardState;
    class PhysicsSystem;
    class Scene;
}

namespace engine::events {

    class eventManager;

    /** Message types from here up belong to the engine's networked events. */
    inline constexpr std::uint16_t kEngineEventTypeFirst = 0xFF00;

    // ---- gameplay -------------------------------------------------------------------------

    /** A key changed state. Raised by raiseInputEvents() or inputMapper::raise(), one per
     *  changed key. */
    struct inputEvent : simpleEvent {
        SC::SDL_Scancode key = SC::SDL_SCANCODE_UNKNOWN;
        bool pressed = false;
        /** Set by inputMapper on a press that completed a chord, and on the release that
         *  ends that same press. A handler for the plain key can skip chorded events and
         *  still see every press it handled released. */
        bool chorded = false;
    };

    /** A chord registered with inputMapper::addChord() completed (pressed) or ended because
     *  one of its keys was released. Raised at priority -1, before the tick's inputEvents. */
    struct inputChordEvent : simpleEvent {
        int chord = 0;
        bool pressed = false;
    };

    /** Two colliders overlap. Raised by raiseCollisions() every tick they overlap. */
    struct collisionEvent : simpleEvent {
        static constexpr std::uint16_t kType = kEngineEventTypeFirst + 1;
        std::uint32_t a = 0;
        std::uint32_t b = 0;

        networking::Bytes encode() const;
        static std::optional<collisionEvent> decode(networking::ByteView in);
    };

    /** entity died; cause is what killed it, or 0. */
    struct deathEvent : simpleEvent {
        static constexpr std::uint16_t kType = kEngineEventTypeFirst + 2;
        std::uint32_t entity = 0;
        std::uint32_t cause = 0;

        networking::Bytes encode() const;
        static std::optional<deathEvent> decode(networking::ByteView in);
    };

    /** entity (re)appears at position. owner is the client it belongs to, if any. */
    struct spawnEvent : simpleEvent {
        static constexpr std::uint16_t kType = kEngineEventTypeFirst + 3;
        std::uint32_t entity = 0;
        ClientId owner = kServerId;
        glm::vec2 position{0.f, 0.f};

        networking::Bytes encode() const;
        static std::optional<spawnEvent> decode(networking::ByteView in);
    };

    // ---- replay (handled by replay::replayController) -------------------------------------

    /** Clears the recording and starts a new one. */
    struct replayRecordStart : simpleEvent {};
    /** Stops recording; what was recorded is kept for playback. */
    struct replayRecordStop : simpleEvent {};
    /** Plays the recording from the first recorded tick at or after fromTick (0: from the
     *  start). Stops recording first. */
    struct replayPlayStart : simpleEvent {
        std::int64_t fromTick = 0;
    };
    /** Stops playback and shows the live Scene again. Also raised when a recording runs out. */
    struct replayPlayStop : simpleEvent {};
    /** Freezes playback on the current frame. */
    struct replayPause : simpleEvent {};
    /** Continues paused playback. */
    struct replayResume : simpleEvent {};
    /** Sets playback speed relative to the recording: 0.5 half, 2.0 double, at most
     *  replayController::kMaxPlaybackSpeed (faster is clamped). Kept across
     *  playbacks. */
    struct replaySpeed : simpleEvent {
        float multiplier = 1.f;
    };

    // ---- helpers ----------------------------------------------------------------------------

    /** Raises an inputEvent for tick for every key that differs between now and previous
     *  (in onTick: ctx.keyboard and ctx.previousKeyboard). */
    void raiseInputEvents(eventManager& events, const KeyboardState& now,
                          const KeyboardState& previous, std::int64_t tick);

    /** Raises inputChordEvents for registered key combinations, and inputEvents like
     *  raiseInputEvents(). Use it in place of raiseInputEvents() when a game has chords.
     *
     *  A chord completes on the tick the last of its keys goes down, provided the presses
     *  fall within its window, and ends when any of its keys is released. It only completes
     *  on a press: releasing and re-pressing one of its keys completes it again. When chords
     *  share keys, the longest one that completes on a tick claims the keys pressed on that
     *  tick, so Shift+Ctrl+W wins over Shift+W.
     *
     *  Keys are sampled once per frame, so two keys pressed within one frame count as the
     *  same tick.
     *
     *  @thread_safety Not threadsafe. Use it on the sim thread with its eventManager. */
    class inputMapper {
        public:
        /** Registers keys as a chord and returns its id for inputChordEvent::chord, or -1 if keys
         *  is empty. windowTicks bounds the ticks between the first and last press:
         *  -1 any (modifier style: hold Shift, then press W), 0 the same tick, n within n
         *  ticks (6 is 100 ms at 60 ticks per second). */
        int addChord(std::vector<SC::SDL_Scancode> keys, std::int64_t windowTicks = -1);

        /** Raises the tick's inputChordEvents, then an inputEvent for every key that differs
         *  between now and previous (in onTick: ctx.keyboard and ctx.previousKeyboard). */
        void raise(eventManager& events, const KeyboardState& now, const KeyboardState& previous,
                   std::int64_t tick);

        private:
        struct chord {
            std::vector<SC::SDL_Scancode> keys;
            std::int64_t window;
            bool active = false;
        };

        std::vector<chord> chords_;                 // indexed by id
        std::vector<int> longestFirst_;             // ids, longest chord first
        std::array<std::int64_t, SC::SDL_SCANCODE_COUNT> pressedAt_{};  // tick each key last went down
        std::bitset<SC::SDL_SCANCODE_COUNT> pressChorded_;              // held keys whose press was chorded
    };

    /** Raises a collisionEvent for tick, with EntityIds, for every pair of entities with
     *  Colliders that overlap. Checks every pair, so it suits small scenes. */
    void raiseCollisions(eventManager& events, const PhysicsSystem& physics, Scene& scene,
                         std::int64_t tick);

}
