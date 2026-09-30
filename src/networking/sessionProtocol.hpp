#pragma once

/** @file
 *  Wire format shared by sessionServer and sessionClient. Internal to the engine.
 *
 *  join       REQ -> join port     joinRequest   -> joinReply
 *  update     REQ -> update port   updateRequest -> updateReply   (one port per client)
 *  snapshot   PUB kSnapshotTopic   {tick, payload}
 *
 *  Messages are reliable: the sender keeps each one until the receiver's ackSeq covers
 *  it and resends it on every exchange until then; the receiver accepts only the next
 *  sequence number, so resends are dropped.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "engine/networking/session.hpp"
#include "byteCodec.hpp"

namespace engine::networking::protocol {

    inline const std::string kSnapshotTopic = "snapshot/";
    inline const std::string kPeerTopic = "peer/";

    enum class RequestKind : std::uint8_t { Join = 1, Update = 2, Leave = 3 };
    enum class ReplyStatus : std::uint8_t { Ok = 0, Rejected = 1, UnknownClient = 2 };

    /** Latest-wins state: a client's submitted scene, or the server's snapshot. */
    struct receivedState {
        ClientId from = kServerId;
        std::int64_t tick = 0;              // sender's gameTime tick
        Bytes payload;
    };

    struct wireMessage {
        std::uint32_t seq = 0;
        std::uint16_t type = 0;
        std::int64_t tick = 0;
        Bytes payload;
    };

    struct joinRequest {
        std::uint64_t token = 0;
        Bytes hello;
        std::string peerEndpoint;
    };

    struct joinReply {
        ReplyStatus status = ReplyStatus::Rejected;
        ClientId id = kServerId;
        std::int32_t updatePort = 0;
        std::int32_t snapshotPort = 0;
    };

    struct updateRequest {
        RequestKind kind = RequestKind::Update;
        ClientId id = kServerId;
        std::int64_t tick = 0;
        std::uint32_t ackSeq = 0;           // highest server message received
        std::uint32_t rosterVersion = 0;    // 0 = send the full roster
        bool hasState = false;
        Bytes state;
        std::vector<wireMessage> messages;
    };

    struct updateReply {
        ReplyStatus status = ReplyStatus::Ok;
        std::uint32_t ackSeq = 0;           // highest client message received
        std::uint32_t rosterVersion = 0;
        std::vector<rosterEvent> rosterEvents;
        std::vector<wireMessage> messages;
    };

    // ---- encoding -----------------------------------------------------------------

    inline void putMessages(byteWriter& out, const std::vector<wireMessage>& messages)
    {
        out.put(static_cast<std::uint32_t>(messages.size()));
        for(const auto& m : messages)
        {
            out.put(m.seq);
            out.put(m.type);
            out.put(m.tick);
            out.putBytes(m.payload);
        }
    }

    inline bool getMessages(byteReader& in, std::vector<wireMessage>& messages)
    {
        std::uint32_t count = 0;
        if(!in.get(count)) {return false;}
        messages.clear();
        for(std::uint32_t i = 0; i < count && in.ok(); ++i)
        {
            wireMessage m;
            in.get(m.seq);
            in.get(m.type);
            in.get(m.tick);
            in.getBytes(m.payload);
            messages.push_back(std::move(m));
        }
        return in.ok();
    }

    inline Bytes encode(const joinRequest& r)
    {
        Bytes out;
        byteWriter w(out);
        w.put(RequestKind::Join);
        w.put(r.token);
        w.putBytes(r.hello);
        w.putString(r.peerEndpoint);
        return out;
    }

    inline bool decode(ByteView data, joinRequest& r)
    {
        byteReader in(data);
        RequestKind kind{};
        in.get(kind);
        in.get(r.token);
        in.getBytes(r.hello);
        in.getString(r.peerEndpoint);
        return in.ok() && in.atEnd() && kind == RequestKind::Join;
    }

    inline Bytes encode(const joinReply& r)
    {
        Bytes out;
        byteWriter w(out);
        w.put(r.status);
        w.put(r.id);
        w.put(r.updatePort);
        w.put(r.snapshotPort);
        return out;
    }

    inline bool decode(ByteView data, joinReply& r)
    {
        byteReader in(data);
        in.get(r.status);
        in.get(r.id);
        in.get(r.updatePort);
        in.get(r.snapshotPort);
        return in.ok() && in.atEnd();
    }

    inline Bytes encode(const updateRequest& r)
    {
        Bytes out;
        byteWriter w(out);
        w.put(r.kind);
        w.put(r.id);
        w.put(r.tick);
        w.put(r.ackSeq);
        w.put(r.rosterVersion);
        w.put(static_cast<std::uint8_t>(r.hasState));
        if(r.hasState) {w.putBytes(r.state);}
        putMessages(w, r.messages);
        return out;
    }

    inline bool decode(ByteView data, updateRequest& r)
    {
        byteReader in(data);
        std::uint8_t hasState = 0;
        in.get(r.kind);
        in.get(r.id);
        in.get(r.tick);
        in.get(r.ackSeq);
        in.get(r.rosterVersion);
        in.get(hasState);
        r.hasState = hasState != 0;
        if(r.hasState) {in.getBytes(r.state);}
        getMessages(in, r.messages);
        return in.ok() && in.atEnd()
            && (r.kind == RequestKind::Update || r.kind == RequestKind::Leave);
    }

    inline Bytes encode(const updateReply& r)
    {
        Bytes out;
        byteWriter w(out);
        w.put(r.status);
        w.put(r.ackSeq);
        w.put(r.rosterVersion);
        w.put(static_cast<std::uint32_t>(r.rosterEvents.size()));
        for(const auto& e : r.rosterEvents)
        {
            w.put(e.change);
            w.put(e.client.id);
            w.putBytes(e.client.hello);
            w.putString(e.client.peerEndpoint);
        }
        putMessages(w, r.messages);
        return out;
    }

    inline bool decode(ByteView data, updateReply& r)
    {
        byteReader in(data);
        std::uint32_t eventCount = 0;
        in.get(r.status);
        in.get(r.ackSeq);
        in.get(r.rosterVersion);
        in.get(eventCount);
        r.rosterEvents.clear();
        for(std::uint32_t i = 0; i < eventCount && in.ok(); ++i)
        {
            rosterEvent e;
            in.get(e.change);
            in.get(e.client.id);
            in.getBytes(e.client.hello);
            in.getString(e.client.peerEndpoint);
            r.rosterEvents.push_back(std::move(e));
        }
        getMessages(in, r.messages);
        return in.ok() && in.atEnd();
    }

    inline Bytes encodeSnapshot(std::int64_t tick, ByteView payload)
    {
        Bytes out;
        byteWriter w(out);
        w.put(tick);
        w.putBytes(payload);
        return out;
    }

    inline bool decodeSnapshot(ByteView data, receivedState& state)
    {
        byteReader in(data);
        state.from = kServerId;
        in.get(state.tick);
        in.getBytes(state.payload);
        return in.ok() && in.atEnd();
    }

}
