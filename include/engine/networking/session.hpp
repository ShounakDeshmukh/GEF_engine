#pragma once

/** @file
 *  Types shared by sessionServer and sessionClient.
 */

#include <cstdint>

#include "engine/ids.hpp"
#include "bytes.hpp"

namespace engine::networking {

    inline constexpr int kDefaultHeartbeatMs = 250;
    inline constexpr int kDefaultClientTimeoutMs = 2000;

    struct clientInfo {
        ClientId id = kServerId;
        Bytes hello;                    // game-defined join payload (name, colour, ...)
    };

    enum class RosterChange { Joined, Left };

    struct rosterEvent {
        RosterChange change;
        clientInfo client;
    };

    /** Latest-wins state: a client's submitted state, or the server's snapshot. */
    struct receivedState {
        ClientId from = kServerId;
        std::int64_t tick = 0;          // sender's gameTime tick
        Bytes payload;
    };

    /** Reliable, ordered, one-off message. type is game-defined; the engine never
     *  interprets it and reserves no values. */
    struct receivedMessage {
        ClientId from = kServerId;
        std::uint16_t type = 0;
        std::int64_t tick = 0;
        Bytes payload;
    };

}
