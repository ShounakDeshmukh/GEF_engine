#pragma once

/** @file
 *  Types shared by sessionServer and sessionClient.
 */

#include <cstdint>
#include <string>

#include "engine/ids.hpp"
#include "bytes.hpp"

namespace engine::networking {

    /** Default for sessionClient::setHeartbeat(). */
    inline constexpr int kDefaultHeartbeatMs = 250;
    /** Default for sessionServer::setClientTimeout(); also how long a client waits for
     *  the server before reporting !connected(). */
    inline constexpr int kDefaultClientTimeoutMs = 2000;

    struct clientInfo {
        ClientId id = kServerId;
        Bytes hello;                    // game-defined join payload (name, colour, ...)
        std::string peerEndpoint;       // this client's peer publisher; empty if it has none
    };

    enum class RosterChange { Joined, Left };

    struct rosterEvent {
        RosterChange change;
        clientInfo client;
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
