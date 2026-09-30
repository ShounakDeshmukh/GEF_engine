#pragma once

/** @file
 *  Umbrella header exposing the complete public networking API:
 *  - Low-level byte buffers (bytes.hpp) and status codes (networkShared.hpp)
 *  - Wire protocol packets, serialization, and codecs (protocol.hpp)
 *  - Transport wrappers: PUB/SUB (publisher.hpp, subscriber.hpp) and REQ/REP (requestHandler.hpp, responseHandler.hpp)
 *  - High-level session management: Client (client.hpp) and Server (server.hpp)
 */

// Core types & status
#include "bytes.hpp"
#include "networkShared.hpp"

// Wire protocol & serialization
#include "protocol.hpp"

// Messaging primitives (ZeroMQ wrappers)
#include "publisher.hpp"
#include "requestHandler.hpp"
#include "responseHandler.hpp"
#include "subscriber.hpp"

// High-level client/server sessions
#include "client.hpp"
#include "server.hpp"
