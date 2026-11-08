#pragma once

#include <concepts>
#include <cstdint>
#include <optional>

#include "engine/ids.hpp"
#include "engine/networking/bytes.hpp"

namespace engine::events {

    /** Base of every event. Derive one struct per event type and put its data in it.
     *
     *  Not polymorphic, so derived events stay aggregates: deathEvent{{}, victim, cause}.
     *  eventManager deletes each event as its real type.
     *
     *  tick and priority are set by eventManager::raise(); remote and from by
     *  eventManager::receive(). */
    class simpleEvent {
        public:
        std::int64_t tick = 0;         // gameTime tick it is dispatched on
        int priority = 0;               // lower runs first within a tick
        bool remote = false;            // decoded from a network message
        ClientId from = kServerId;      // sender, when remote
    };

    template <typename T>
    concept eventType = std::derived_from<T, simpleEvent> && !std::same_as<T, simpleEvent>;

    /** An event that can cross the network. kType is the message type it is sent
     *  under; it must be unique among networked events and agreed by every process.
     *  Payloads use the host's byte order, as snapshots do. */
    template <typename T>
    concept networkedEvent = eventType<T> && requires(const T& e, networking::ByteView in) {
        {T::kType} -> std::convertible_to<std::uint16_t>;
        {e.encode()} -> std::same_as<networking::Bytes>;
        {T::decode(in)} -> std::same_as<std::optional<T>>;
    };

}
