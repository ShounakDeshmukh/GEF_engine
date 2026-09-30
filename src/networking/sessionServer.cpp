#include "engine/networking/sessionServer.hpp"
#include "engine/networking/publisher.hpp"
#include "engine/networking/responseHandler.hpp"
#include "sessionProtocol.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace engine::networking {

    using namespace protocol;

    namespace {
        using clock = std::chrono::steady_clock;

        // receive timeout for every handler, and the watchdog period: bounds how long
        // stop() and timeout detection take
        constexpr int kPollMs = 50;
    }

    struct sessionServer::impl {

        struct clientSlot {
            clientInfo info;
            std::uint64_t token = 0;
            std::unique_ptr<responseHandler> handler;
            std::thread thread;
            clock::time_point lastSeen;
            std::uint32_t lastReceivedSeq = 0;      // highest in-order message from the client
            std::uint32_t nextSendSeq = 1;
            std::deque<wireMessage> outbox;         // sent to the client, not yet acknowledged
        };

        explicit impl(std::string connectionString)
            : joinHandler(std::move(connectionString)),
              snapshotPublisher("tcp://*:0"),
              replicator(kServerId)
        {}

        responseHandler joinHandler;
        publisher snapshotPublisher;
        sceneReplicator replicator;                 // sim thread only
        std::atomic<int> clientTimeoutMs{kDefaultClientTimeoutMs};

        mutable std::mutex mutex;                   // guards everything below
        std::map<ClientId, std::unique_ptr<clientSlot>> clients;
        std::vector<std::unique_ptr<clientSlot>> retired;   // left; thread still to join
        ClientId nextId = 1;
        std::vector<rosterEvent> rosterLog;         // version n = first n entries
        std::deque<rosterEvent> rosterEvents;
        std::vector<ClientId> departed;             // for applyClientStates
        std::unordered_map<ClientId, receivedState> states;
        std::deque<receivedMessage> inbox;

        std::thread joinThread;
        std::thread watchdogThread;
        std::mutex watchdogMutex;
        std::condition_variable watchdogWake;
        bool watchdogStop = false;                  // guarded by watchdogMutex

        bool started = false;
        bool stopped = false;

        Bytes handleJoin(const Bytes& request);
        Bytes handleUpdate(ClientId id, const Bytes& request);
        void addRosterEventLocked(RosterChange change, const clientInfo& client);
        void retireLocked(ClientId id);
        void watchdog();

        static void joinSlot(clientSlot& slot)
        {
            slot.handler->stop();
            if(slot.thread.joinable()) {slot.thread.join();}
        }
    };

    Bytes sessionServer::impl::handleJoin(const Bytes& request)
    {
        joinReply reply;
        joinRequest req;
        if(!decode(request, req)) {return encode(reply);}

        std::lock_guard<std::mutex> lock(mutex);

        // a retried join (reply lost or timed out) gets the ClientId it already has
        for(const auto& [id, slot] : clients)
        {
            if(slot->token == req.token)
            {
                reply.status = ReplyStatus::Ok;
                reply.id = id;
                reply.updatePort = slot->handler->port();
                reply.snapshotPort = snapshotPublisher.port();
                return encode(reply);
            }
        }

        if(nextId > kMaxClientIdPerRun) {return encode(reply);}

        auto slot = std::make_unique<clientSlot>();
        try {
            slot->handler = std::make_unique<responseHandler>("tcp://*:0");
        }
        catch (...)
        {
            return encode(reply);
        }
        slot->handler->setReceiveTimeout(kPollMs);
        slot->info = {nextId++, std::move(req.hello)};
        slot->token = req.token;
        slot->lastSeen = clock::now();

        ClientId id = slot->info.id;
        responseHandler* handler = slot->handler.get();
        slot->thread = std::thread([this, handler, id]() {
            handler->run<Bytes, Bytes>([this, id](Bytes& update) { return handleUpdate(id, update); });
        });

        addRosterEventLocked(RosterChange::Joined, slot->info);

        reply.status = ReplyStatus::Ok;
        reply.id = id;
        reply.updatePort = handler->port();
        reply.snapshotPort = snapshotPublisher.port();
        clients.emplace(id, std::move(slot));
        return encode(reply);
    }

    Bytes sessionServer::impl::handleUpdate(ClientId id, const Bytes& request)
    {
        updateReply reply;
        updateRequest req;
        if(!decode(request, req) || req.id != id)
        {
            reply.status = ReplyStatus::Rejected;
            return encode(reply);
        }

        std::lock_guard<std::mutex> lock(mutex);

        auto it = clients.find(id);
        if(it == clients.end())
        {
            reply.status = ReplyStatus::UnknownClient;
            return encode(reply);
        }
        clientSlot& slot = *it->second;
        slot.lastSeen = clock::now();

        if(req.hasState) {states[id] = receivedState{id, req.tick, std::move(req.state)};}

        for(auto& m : req.messages)
        {
            if(m.seq != slot.lastReceivedSeq + 1) {continue;}      // resend of one we have
            inbox.push_back(receivedMessage{id, m.type, m.tick, std::move(m.payload)});
            slot.lastReceivedSeq = m.seq;
        }

        while(!slot.outbox.empty() && slot.outbox.front().seq <= req.ackSeq) {slot.outbox.pop_front();}

        reply.status = ReplyStatus::Ok;
        reply.ackSeq = slot.lastReceivedSeq;
        reply.rosterVersion = static_cast<std::uint32_t>(rosterLog.size());
        if(req.rosterVersion == 0)
        {
            for(const auto& [clientId, client] : clients)
            {
                reply.rosterEvents.push_back({RosterChange::Joined, client->info});
            }
        }
        else
        {
            for(std::size_t v = req.rosterVersion; v < rosterLog.size(); ++v)
            {
                reply.rosterEvents.push_back(rosterLog[v]);
            }
        }
        reply.messages.assign(slot.outbox.begin(), slot.outbox.end());

        if(req.kind == RequestKind::Leave) {retireLocked(id);}
        return encode(reply);
    }

    void sessionServer::impl::addRosterEventLocked(RosterChange change, const clientInfo& client)
    {
        rosterEvent event{change, client};
        rosterLog.push_back(event);
        rosterEvents.push_back(std::move(event));
    }

    void sessionServer::impl::retireLocked(ClientId id)
    {
        auto it = clients.find(id);
        if(it == clients.end()) {return;}

        auto slot = std::move(it->second);
        clients.erase(it);

        // may be called from the client's own thread; it exits after this reply
        slot->handler->stop();
        addRosterEventLocked(RosterChange::Left, slot->info);
        departed.push_back(id);
        states.erase(id);
        retired.push_back(std::move(slot));
        watchdogWake.notify_one();
    }

    void sessionServer::impl::watchdog()
    {
        while(true)
        {
            {
                std::unique_lock<std::mutex> lock(watchdogMutex);
                watchdogWake.wait_for(lock, std::chrono::milliseconds(kPollMs), [this] { return watchdogStop; });
                if(watchdogStop) {break;}
            }

            std::vector<std::unique_ptr<clientSlot>> finished;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto now = clock::now();
                auto timeout = std::chrono::milliseconds(clientTimeoutMs.load());

                std::vector<ClientId> silent;
                for(const auto& [id, slot] : clients)
                {
                    if(now - slot->lastSeen > timeout) {silent.push_back(id);}
                }
                for(ClientId id : silent) {retireLocked(id);}
                finished.swap(retired);
            }

            // outside the lock: a client thread may be waiting on it
            for(auto& slot : finished) {joinSlot(*slot);}
        }
    }


    sessionServer::sessionServer(std::string connectionString)
        : impl_(std::make_unique<impl>(std::move(connectionString)))
    {}

    sessionServer::~sessionServer()
    {
        stop();
    }

    int sessionServer::port() const
    {
        return impl_->joinHandler.port();
    }

    void sessionServer::start()
    {
        if(impl_->started)
        {
            std::cerr << "sessionServer already started" << std::endl;
            return;
        }
        impl_->started = true;

        impl_->joinHandler.setReceiveTimeout(kPollMs);
        impl_->joinThread = std::thread([this]() {
            impl_->joinHandler.run<Bytes, Bytes>([this](Bytes& request) { return impl_->handleJoin(request); });
        });
        impl_->watchdogThread = std::thread([this]() { impl_->watchdog(); });
    }

    void sessionServer::stop()
    {
        if(!impl_->started || impl_->stopped) {return;}
        impl_->stopped = true;

        // no new clients, then no more timeouts, then every client thread
        impl_->joinHandler.stop();
        impl_->joinThread.join();

        {
            std::lock_guard<std::mutex> lock(impl_->watchdogMutex);
            impl_->watchdogStop = true;
        }
        impl_->watchdogWake.notify_one();
        impl_->watchdogThread.join();

        std::vector<std::unique_ptr<impl::clientSlot>> slots;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            for(auto& [id, slot] : impl_->clients) {slots.push_back(std::move(slot));}
            impl_->clients.clear();
            for(auto& slot : impl_->retired) {slots.push_back(std::move(slot));}
            impl_->retired.clear();
        }
        for(auto& slot : slots) {impl::joinSlot(*slot);}
    }

    void sessionServer::setClientTimeout(int milliseconds)
    {
        impl_->clientTimeoutMs.store(milliseconds);
    }

    void sessionServer::publishSnapshot(ByteView snapshot, std::int64_t tick)
    {
        impl_->snapshotPublisher.publish(encodeSnapshot(tick, snapshot), kSnapshotTopic);
    }

    std::vector<receivedState> sessionServer::drainStates()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::vector<receivedState> drained;
        drained.reserve(impl_->states.size());
        for(auto& [id, state] : impl_->states) {drained.push_back(std::move(state));}
        impl_->states.clear();
        return drained;
    }

    void sessionServer::send(ClientId to, std::uint16_t type, Bytes payload, std::int64_t tick)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->clients.find(to);
        if(it == impl_->clients.end()) {return;}

        auto& slot = *it->second;
        slot.outbox.push_back(wireMessage{slot.nextSendSeq++, type, tick, std::move(payload)});
    }

    void sessionServer::broadcast(std::uint16_t type, Bytes payload, std::int64_t tick)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for(auto& [id, slot] : impl_->clients)
        {
            slot->outbox.push_back(wireMessage{slot->nextSendSeq++, type, tick, payload});
        }
    }

    std::deque<receivedMessage> sessionServer::drainMessages()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::deque<receivedMessage> drained;
        drained.swap(impl_->inbox);
        return drained;
    }

    std::deque<rosterEvent> sessionServer::drainRosterEvents()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::deque<rosterEvent> drained;
        drained.swap(impl_->rosterEvents);
        return drained;
    }

    std::vector<clientInfo> sessionServer::roster() const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::vector<clientInfo> clients;
        for(const auto& [id, slot] : impl_->clients) {clients.push_back(slot->info);}
        return clients;
    }

    sceneReplicator& sessionServer::replicator()
    {
        return impl_->replicator;
    }

    void sessionServer::publishScene(const Scene& scene, std::int64_t tick)
    {
        publishSnapshot(impl_->replicator.encodeAll(scene), tick);
    }

    void sessionServer::applyClientStates(Scene& scene)
    {
        for(auto& state : drainStates())
        {
            impl_->replicator.apply(scene, state.payload, state.from);
        }

        std::vector<ClientId> departed;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            departed.swap(impl_->departed);
        }
        for(ClientId id : departed) {impl_->replicator.dropOwner(scene, id);}
    }

}
