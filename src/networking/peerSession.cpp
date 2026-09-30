#include "engine/networking/peerSession.hpp"
#include "engine/networking/publisher.hpp"
#include "engine/networking/sessionClient.hpp"
#include "engine/networking/subscriber.hpp"
#include "engine/threading/latestValue.hpp"
#include "sessionProtocol.hpp"

#include <atomic>
#include "engine/log.hpp"
#include <map>
#include <thread>

namespace engine::networking {

    using namespace protocol;

    namespace {
        // peer listeners' receive timeout: bounds how long disconnecting a peer takes
        constexpr int kPollMs = 50;

        struct peerSlot {
            std::string endpoint;
            std::thread thread;
            std::atomic<bool> stop{false};
            threading::LatestValue<Bytes> latest;

            void listen()
            {
                try {
                    // built on this thread: a subscriber belongs to the thread that listens
                    subscriber peer(endpoint, kPeerTopic);
                    peer.setReceiveTimeout(kPollMs);

                    Bytes data;
                    while(!stop.load())
                    {
                        auto status = peer.listenBytes(data);
                        if(status == ReceivedStatus::Closed) {break;}
                        if(status == ReceivedStatus::Success) {latest.publish(data);}
                    }
                }
                catch (const std::exception& e)
                {
                    log::error("peerSession listener for {} failed: {}", endpoint, e.what());
                }
            }
        };

    }

    struct peerSession::impl {

        impl(std::string serverConnectionString, const std::string& advertisedHost, Bytes hello)
            : peerPublisher("tcp://*:0"),
              session(std::move(serverConnectionString), std::move(hello),
                      "tcp://" + advertisedHost + ":" + std::to_string(peerPublisher.port()))
        {}

        publisher peerPublisher;                    // declared first: session needs its port
        sessionClient session;
        std::map<ClientId, std::unique_ptr<peerSlot>> peers;
        std::map<ClientId, clientInfo> roster;
        bool left = false;

        void connect(ClientId id, const std::string& endpoint)
        {
            if(endpoint.empty() || peers.contains(id)) {return;}

            auto slot = std::make_unique<peerSlot>();
            slot->endpoint = endpoint;
            peerSlot* s = slot.get();
            slot->thread = std::thread([s]() { s->listen(); });
            peers.emplace(id, std::move(slot));
        }

        void disconnect(ClientId id)
        {
            auto it = peers.find(id);
            if(it == peers.end()) {return;}

            it->second->stop.store(true);
            it->second->thread.join();
            peers.erase(it);
        }
    };


    peerSession::peerSession(std::string serverConnectionString, std::string advertisedHost,
                             Bytes hello)
        : impl_(std::make_unique<impl>(std::move(serverConnectionString), advertisedHost,
                                       std::move(hello)))
    {}

    peerSession::~peerSession()
    {
        leave();
    }

    bool peerSession::join()
    {
        return impl_->session.join();
    }

    ClientId peerSession::id() const
    {
        return impl_->session.id();
    }

    void peerSession::start()
    {
        impl_->session.start();
    }

    void peerSession::leave()
    {
        if(impl_->left) {return;}
        impl_->left = true;

        while(!impl_->peers.empty()) {impl_->disconnect(impl_->peers.begin()->first);}
        impl_->session.leave();
    }

    bool peerSession::connected() const
    {
        return impl_->session.connected();
    }

    void peerSession::publishScene(const Scene& scene, std::int64_t tick)
    {
        impl_->peerPublisher.publish(encodeSnapshot(tick, replicator().encodeOwned(scene)), kPeerTopic);
        impl_->session.requestSnapshot(tick);
    }

    void peerSession::applyUpdates(Scene& scene)
    {
        ClientId self = id();

        for(auto& event : impl_->session.drainRosterEvents())
        {
            ClientId peer = event.client.id;
            if(event.change == RosterChange::Joined) {impl_->roster[peer] = event.client;}
            else {impl_->roster.erase(peer);}
            if(peer == self) {continue;}

            if(event.change == RosterChange::Joined)
            {
                impl_->connect(peer, event.client.peerEndpoint);
            }
            else
            {
                impl_->disconnect(peer);
                replicator().dropOwner(scene, peer);
            }
        }

        impl_->session.applySnapshot(scene);

        for(auto& [peer, slot] : impl_->peers)
        {
            auto data = slot->latest.take();
            if(!data) {continue;}

            receivedState state;
            if(decodeSnapshot(*data, state)) {replicator().apply(scene, state.payload, peer);}
        }
    }

    void peerSession::send(std::uint16_t type, Bytes payload, std::int64_t tick)
    {
        impl_->session.send(type, std::move(payload), tick);
    }

    std::deque<receivedMessage> peerSession::drainMessages()
    {
        return impl_->session.drainMessages();
    }

    std::vector<clientInfo> peerSession::roster() const
    {
        std::vector<clientInfo> clients;
        clients.reserve(impl_->roster.size());
        for(const auto& [id, client] : impl_->roster) {clients.push_back(client);}
        return clients;
    }

    sceneReplicator& peerSession::replicator()
    {
        return impl_->session.replicator();
    }

    std::vector<ClientId> peerSession::peers() const
    {
        std::vector<ClientId> ids;
        for(const auto& [id, slot] : impl_->peers) {ids.push_back(id);}
        return ids;
    }

}
