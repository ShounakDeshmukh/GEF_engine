#include "engine/networking/sessionClient.hpp"
#include "engine/networking/requestHandler.hpp"
#include "engine/threading/latestValue.hpp"
#include "sessionProtocol.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include "engine/log.hpp"
#include <mutex>
#include <random>
#include <thread>

namespace engine::networking {

    using namespace protocol;

    namespace {
        using clock = std::chrono::steady_clock;

        // pause after a failed update: bounds how long leave() waits for the update thread
        constexpr int kPollMs = 50;

        std::uint64_t randomToken()
        {
            std::random_device rd;
            std::uint64_t token = 0;
            while(token == 0) {token = (static_cast<std::uint64_t>(rd()) << 32) | rd();}
            return token;
        }

        enum class ExchangeResult { Ok, Failed, Dropped };
    }

    struct sessionClient::impl {

        impl(std::string connectionString, Bytes hello, std::string peerEndpoint)
            : endpointPrefix(connectionString.substr(0, connectionString.rfind(':') + 1)),
              hello(std::move(hello)),
              peerEndpoint(std::move(peerEndpoint)),
              joinRequester(std::move(connectionString)),
              token(randomToken())
        {}

        std::string endpointPrefix;             // "tcp://host:", reused for the server's ports
        Bytes hello;
        std::string peerEndpoint;
        requestHandler joinRequester;
        std::uint64_t token;                    // same on every join() retry
        std::atomic<int> replyTimeoutMs{requestHandler::kDefaultReplyTimeoutMs};
        std::atomic<int> heartbeatMs{kDefaultHeartbeatMs};
        int appliedReplyTimeoutMs = requestHandler::kDefaultReplyTimeoutMs;  // on updater

        std::atomic<ClientId> id{kServerId};
        int updatePort = 0;
        std::optional<sceneReplicator> replicator;  // sim thread only; set by join()

        std::unique_ptr<requestHandler> updater;    // update thread, then leave()
        std::thread updateThread;
        std::atomic<bool> stopping{false};
        std::atomic<bool> connected{false};
        bool started = false;
        bool left = false;

        std::mutex mutex;                           // guards everything below
        std::condition_variable wake;
        std::optional<receivedState> pendingState;
        bool pollRequested = false;
        std::int64_t lastTick = 0;
        std::uint32_t nextSendSeq = 1;
        std::deque<wireMessage> outbox;             // sent to the server, not yet acknowledged
        std::uint32_t lastReceivedSeq = 0;          // highest in-order message from the server
        std::uint32_t rosterVersion = 0;
        std::deque<receivedMessage> inbox;
        std::deque<rosterEvent> rosterEvents;

        threading::LatestValue<receivedState> latestSnapshot;

        std::string endpoint(int port) const {return endpointPrefix + std::to_string(port);}

        updateRequest buildRequestLocked(RequestKind kind);
        ExchangeResult exchange(const updateRequest& request);
        void updateLoop();
    };

    updateRequest sessionClient::impl::buildRequestLocked(RequestKind kind)
    {
        updateRequest req;
        req.kind = kind;
        req.id = id.load();
        req.tick = lastTick;
        req.ackSeq = lastReceivedSeq;
        req.rosterVersion = rosterVersion;
        req.messages.assign(outbox.begin(), outbox.end());
        return req;
    }

    ExchangeResult sessionClient::impl::exchange(const updateRequest& request)
    {
        // setReplyTimeout() may be called at any time; apply it between requests
        if(int timeout = replyTimeoutMs.load(); timeout != appliedReplyTimeoutMs)
        {
            updater->setReplyTimeout(timeout);
            appliedReplyTimeoutMs = timeout;
        }

        auto [bytes, valid] = updater->send(encode(request));
        updateReply reply;
        if(!valid || !decode(bytes, reply)) {return ExchangeResult::Failed;}
        if(reply.status == ReplyStatus::UnknownClient) {return ExchangeResult::Dropped;}
        if(reply.status != ReplyStatus::Ok) {return ExchangeResult::Failed;}

        std::lock_guard<std::mutex> lock(mutex);
        while(!outbox.empty() && outbox.front().seq <= reply.ackSeq) {outbox.pop_front();}

        for(auto& m : reply.messages)
        {
            if(m.seq != lastReceivedSeq + 1) {continue;}           // resend of one we have
            inbox.push_back(receivedMessage{kServerId, m.type, m.tick, std::move(m.payload)});
            lastReceivedSeq = m.seq;
        }

        for(auto& e : reply.rosterEvents) {rosterEvents.push_back(std::move(e));}
        rosterVersion = reply.rosterVersion;

        if(reply.hasSnapshot)
        {
            latestSnapshot.publish(receivedState{kServerId, reply.snapshotTick, std::move(reply.snapshot)});
        }
        return ExchangeResult::Ok;
    }

    void sessionClient::impl::updateLoop()
    {
        auto lastSuccess = clock::now();
        bool first = true;      // fetch the roster straight away rather than after a heartbeat

        while(!stopping.load())
        {
            updateRequest req;
            {
                std::unique_lock<std::mutex> lock(mutex);
                // wakes for new state or messages; otherwise times out into a heartbeat
                if(!first)
                {
                    wake.wait_for(lock, std::chrono::milliseconds(heartbeatMs.load()),
                                  [this] { return stopping.load() || pendingState || pollRequested || !outbox.empty(); });
                }
                first = false;
                if(stopping.load()) {break;}

                req = buildRequestLocked(RequestKind::Update);
                pollRequested = false;
                if(pendingState)
                {
                    req.hasState = true;
                    req.tick = pendingState->tick;
                    req.state = std::move(pendingState->payload);
                    pendingState.reset();
                }
            }

            auto result = exchange(req);
            if(result == ExchangeResult::Dropped)
            {
                log::warn("sessionClient: server dropped client {}", id.load());
                connected.store(false);
                break;
            }
            if(result == ExchangeResult::Ok)
            {
                lastSuccess = clock::now();
                connected.store(true);
                continue;
            }

            // failed: keep the state for the retry unless a newer one arrived
            {
                std::unique_lock<std::mutex> lock(mutex);
                if(req.hasState && !pendingState)
                {
                    pendingState = receivedState{req.id, req.tick, std::move(req.state)};
                }
                if(clock::now() - lastSuccess > std::chrono::milliseconds(kDefaultClientTimeoutMs))
                {
                    connected.store(false);
                }
                // a failure can be immediate (malformed reply); don't spin on it
                wake.wait_for(lock, std::chrono::milliseconds(kPollMs), [this] { return stopping.load(); });
            }
        }
    }


    sessionClient::sessionClient(std::string connectionString, Bytes hello, std::string peerEndpoint)
        : impl_(std::make_unique<impl>(std::move(connectionString), std::move(hello), std::move(peerEndpoint)))
    {}

    sessionClient::~sessionClient()
    {
        leave();
    }

    bool sessionClient::join()
    {
        if(impl_->id.load() != kServerId) {return true;}

        joinRequest req{impl_->token, impl_->hello, impl_->peerEndpoint};
        auto [bytes, valid] = impl_->joinRequester.send(encode(req));
        joinReply reply;
        if(!valid || !decode(bytes, reply) || reply.status != ReplyStatus::Ok) {return false;}

        impl_->updatePort = reply.updatePort;
        impl_->replicator.emplace(reply.id);
        impl_->id.store(reply.id);
        return true;
    }

    ClientId sessionClient::id() const
    {
        return impl_->id.load();
    }

    void sessionClient::start()
    {
        if(impl_->id.load() == kServerId)
        {
            log::error("sessionClient::start() before a successful join()");
            return;
        }
        if(impl_->started || impl_->left) {return;}
        impl_->started = true;

        impl_->updater = std::make_unique<requestHandler>(impl_->endpoint(impl_->updatePort));
        impl_->connected.store(true);

        impl_->updateThread = std::thread([this]() { impl_->updateLoop(); });
    }

    void sessionClient::leave()
    {
        if(impl_->id.load() == kServerId || impl_->left) {return;}
        impl_->left = true;

        impl_->stopping.store(true);
        impl_->wake.notify_all();
        if(impl_->updateThread.joinable()) {impl_->updateThread.join();}

        // joined but never started: still free the ClientId on the server
        if(!impl_->updater)
        {
            impl_->updater = std::make_unique<requestHandler>(impl_->endpoint(impl_->updatePort));
        }

        updateRequest req;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            req = impl_->buildRequestLocked(RequestKind::Leave);
        }
        // best effort; the server times us out anyway if this is lost
        impl_->exchange(req);
        impl_->connected.store(false);
    }

    void sessionClient::setReplyTimeout(int milliseconds)
    {
        impl_->replyTimeoutMs.store(milliseconds);
        impl_->joinRequester.setReplyTimeout(milliseconds);
    }

    void sessionClient::setHeartbeat(int milliseconds)
    {
        impl_->heartbeatMs.store(milliseconds);
    }

    bool sessionClient::connected() const
    {
        return impl_->connected.load();
    }

    void sessionClient::send(std::uint16_t type, Bytes payload, std::int64_t tick)
    {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->outbox.push_back(wireMessage{impl_->nextSendSeq++, type, tick, std::move(payload)});
        }
        impl_->wake.notify_one();
    }

    std::deque<receivedMessage> sessionClient::drainMessages()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::deque<receivedMessage> drained;
        drained.swap(impl_->inbox);
        return drained;
    }

    std::deque<rosterEvent> sessionClient::drainRosterEvents()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::deque<rosterEvent> drained;
        drained.swap(impl_->rosterEvents);
        return drained;
    }

    sceneReplicator& sessionClient::replicator()
    {
        assert(impl_->replicator && "sessionClient::replicator() before a successful join()");
        return *impl_->replicator;
    }

    void sessionClient::submitScene(const Scene& scene, std::int64_t tick)
    {
        Bytes state = replicator().encodeOwned(scene);
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->pendingState = receivedState{impl_->id.load(), tick, std::move(state)};
            impl_->lastTick = tick;
        }
        impl_->wake.notify_one();
    }

    void sessionClient::requestSnapshot(std::int64_t tick)
    {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->pollRequested = true;
            impl_->lastTick = tick;
        }
        impl_->wake.notify_one();
    }

    bool sessionClient::applySnapshot(Scene& scene)
    {
        auto snapshot = impl_->latestSnapshot.take();
        if(!snapshot) {return false;}
        return replicator().apply(scene, snapshot->payload, kServerId);
    }

}
