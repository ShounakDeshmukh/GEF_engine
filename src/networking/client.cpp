#include "engine/networking/client.hpp"

#include "engine/log.hpp"
#include "engine/networking/publisher.hpp"
#include "engine/networking/requestHandler.hpp"
#include "engine/networking/subscriber.hpp"
#include "engine/threading/latestValue.hpp"
#include "engine/threading/threadSafeQueue.hpp"

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace engine::networking {
namespace {
std::string address(const std::string& host, std::uint16_t port) {
    return "tcp://" + host + ":" + std::to_string(port);
}
std::uint32_t advance(std::uint32_t& sequence) {
    if (++sequence == 0)
        ++sequence;
    return sequence;
}
bool newer(std::uint32_t sequence, std::uint32_t previous) {
    return sequence != 0 &&
           (previous == 0 || (sequence - previous != 0 && sequence - previous < 0x80000000u));
}
} // namespace
struct Client::Impl {
    struct Outbound {
        Bytes payload;
        std::int64_t tick = 0;
    };
    ClientConfig config;
    std::atomic<bool> stopping{false};
    std::atomic<ClientId> self{0};
    bool ended = false;
    std::thread serverThread, peerThread;
    threading::ThreadSafeQueue<Outbound> outbound;
    threading::LatestValue<std::vector<PeerInfo>> directory;
    std::mutex eventsMutex;
    std::optional<ServerEvent> world;
    std::unordered_map<ClientId, std::string> peers;
    std::unordered_map<ClientId, ServerEvent> playerEvents;
    std::mutex wakeMutex;
    std::condition_variable wake;
    explicit Impl(ClientConfig c) : config(std::move(c)), outbound(config.outboundCapacity) {
        if (config.replyTimeout.count() <= 0 || config.heartbeat.count() <= 0 ||
            config.outboundCapacity == 0)
            throw std::invalid_argument("network timeouts and outbound capacity must be positive");
    }
    void requestStop() {
        {
            std::lock_guard lock(wakeMutex);
            stopping = true;
        }
        wake.notify_all();
        outbound.push({});
    }
    void acceptWorld(ServerUpdate update) {
        std::lock_guard lock(eventsMutex);
        std::unordered_map<ClientId, std::string> next;
        for (const auto& peer : update.peers)
            if (peer.id != self)
                next.emplace(peer.id, peer.endpoint);
        for (const auto& [id, endpoint] : peers)
            if (!next.contains(id))
                playerEvents[id] = {ServerEvent::Kind::PlayerLeft, id, 0, {}};
        peers = std::move(next);
        world = ServerEvent{ServerEvent::Kind::World, 0, update.worldTick, std::move(update.world)};
        if (config.peerToPeer) {
            directory.publish(std::move(update.peers));
        } else {
            for (auto& player : update.players)
                if (peers.contains(player.client))
                    playerEvents[player.client] = {ServerEvent::Kind::Player, player.client,
                                                   player.tick, std::move(player.payload)};
        }
    }
    void servePeers(std::promise<std::string> ready) {
        bool announced = false;
        try {
            publisher pub(address("*", config.peerPort));
            subscriber sub;
            sub.setReceiveTimeout(5);
            const auto advertised =
                address(config.advertisedHost, static_cast<std::uint16_t>(pub.port()));
            if (!validPeerEndpoint(advertised))
                throw std::invalid_argument("invalid advertised peer endpoint");
            ready.set_value(advertised);
            announced = true;
            std::unordered_map<ClientId, std::string> connected;
            std::unordered_map<ClientId, std::uint32_t> sequences;
            std::uint32_t sequence = 0;
            while (!stopping) {
                if (auto refresh = directory.take()) {
                    std::unordered_map<ClientId, std::string> next;
                    for (const auto& peer : *refresh) {
                        if (peer.id == self || peer.endpoint.empty())
                            continue;
                        next.emplace(peer.id, peer.endpoint);
                    }
                    for (const auto& [id, endpoint] : connected) {
                        if (!next.contains(id) || next[id] != endpoint) {
                            sub.disconnect(endpoint);
                            sequences.erase(id);
                        }
                    }
                    for (const auto& [id, endpoint] : next)
                        if (!connected.contains(id) || connected[id] != endpoint)
                            sub.connect(endpoint);
                    connected = std::move(next);
                }
                if (self != 0) {
                    auto pending = outbound.drainAll();
                    for (auto& state : pending) {
                        if (stopping)
                            break;
                        pub.publish(
                            encode({MessageType::PlayerState, self.load(), advance(sequence),
                                    state.tick, std::move(state.payload)}));
                    }
                }
                const auto bytes = sub.tryReceive();
                const auto packet = bytes ? decode(*bytes) : std::nullopt;
                if (!packet || packet->type != MessageType::PlayerState ||
                    !connected.contains(packet->sender) ||
                    !newer(packet->sequence, sequences[packet->sender]))
                    continue;
                std::lock_guard lock(eventsMutex);
                if (peers.contains(packet->sender)) {
                    sequences[packet->sender] = packet->sequence;
                    playerEvents[packet->sender] = {ServerEvent::Kind::Player, packet->sender,
                                                    packet->tick, packet->payload};
                }
            }
        } catch (const std::exception& error) {
            requestStop();
            if (!announced)
                ready.set_exception(std::current_exception());
            else
                log::error("network peer worker: {}", error.what());
        }
    }
    void serveServer(std::string advertised, std::promise<std::optional<ClientId>> ready) {
        bool announced = false;
        try {
            requestHandler lobby(address(config.serverHost, config.joinPort));
            lobby.setReplyTimeout(static_cast<int>(config.replyTimeout.count()));
            Bytes endpoint;
            for (unsigned char c : advertised)
                endpoint.push_back(std::byte(c));
            auto [reply, ok] =
                lobby.send(encode({MessageType::Join, 0, 0, 0, std::move(endpoint)}));
            const auto packet = ok ? decode(reply) : std::nullopt;
            const auto welcome =
                packet && packet->type == MessageType::Welcome && packet->sender == 0
                    ? decodeWelcome(packet->payload)
                    : std::nullopt;
            if (!welcome) {
                ready.set_value(std::nullopt);
                return;
            }
            requestHandler control(address(config.serverHost, welcome->port));
            control.setReplyTimeout(static_cast<int>(config.replyTimeout.count()));
            self = welcome->client;
            acceptWorld(welcome->update);
            ready.set_value(welcome->client);
            announced = true;
            std::uint32_t sequence = 0;
            while (!stopping) {
                std::optional<Outbound> state;
                if (config.peerToPeer) {
                    std::unique_lock lock(wakeMutex);
                    wake.wait_for(lock, config.heartbeat, [&] { return stopping.load(); });
                } else
                    state = outbound.waitPop(config.heartbeat);
                if (stopping)
                    break;
                auto [bytes, success] = control.send(
                    encode({MessageType::ClientUpdate, self.load(), state ? advance(sequence) : 0,
                            state ? state->tick : 0, state ? std::move(state->payload) : Bytes{}}));
                const auto response = success ? decode(bytes) : std::nullopt;
                if (response && response->type == MessageType::ServerUpdate &&
                    response->sender == 0)
                    if (auto update = decodeServerUpdate(response->payload))
                        acceptWorld(std::move(*update));
            }
            // Bound the best-effort Leave when the server is unavailable.
            control.setReplyTimeout(static_cast<int>(
                std::min(config.replyTimeout, std::chrono::milliseconds(100)).count()));
            const auto left = control.send(encode({MessageType::Leave, self.load(), 0, 0, {}}));
            (void)left;
        } catch (const std::exception& error) {
            requestStop();
            if (!announced)
                ready.set_value(std::nullopt);
            log::error("network server worker: {}", error.what());
        }
    }
};
Client::Client(ClientConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Client::~Client() {
    stop();
}
std::optional<ClientId> Client::join() {
    if (impl_->ended)
        return std::nullopt;
    if (impl_->self != 0)
        return impl_->self.load();
    try {
        std::string endpoint;
        if (impl_->config.peerToPeer) {
            std::promise<std::string> ready;
            auto future = ready.get_future();
            impl_->peerThread = std::thread([this, ready = std::move(ready)]() mutable {
                impl_->servePeers(std::move(ready));
            });
            endpoint = future.get();
        }
        std::promise<std::optional<ClientId>> ready;
        auto future = ready.get_future();
        impl_->serverThread =
            std::thread([this, endpoint = std::move(endpoint), ready = std::move(ready)]() mutable {
                impl_->serveServer(std::move(endpoint), std::move(ready));
            });
        auto id = future.get();
        if (!id)
            stop();
        return id;
    } catch (...) {
        stop();
        return std::nullopt;
    }
}
void Client::start() {
    if (!join())
        throw std::runtime_error("network join failed; check server address and mode");
}
void Client::stop() {
    if (impl_->ended)
        return;
    impl_->requestStop();
    if (impl_->serverThread.joinable())
        impl_->serverThread.join();
    if (impl_->peerThread.joinable())
        impl_->peerThread.join();
    impl_->ended = true;
}
ClientId Client::id() const noexcept {
    return impl_->self;
}
void Client::send(Bytes state, std::int64_t tick) {
    if (state.size() > Packet::maxPayloadSize)
        throw std::length_error("player payload exceeds 64 KiB");
    if (impl_->self == 0 || impl_->stopping)
        throw std::logic_error("send requires an active Client");
    impl_->outbound.push({std::move(state), tick});
}
std::vector<ServerEvent> Client::drain() {
    std::lock_guard lock(impl_->eventsMutex);
    std::vector<ServerEvent> result;
    if (impl_->world) {
        result.push_back(std::move(*impl_->world));
        impl_->world.reset();
    }
    for (auto& [id, event] : impl_->playerEvents)
        result.push_back(std::move(event));
    impl_->playerEvents.clear();
    return result;
}
std::uint64_t Client::droppedUpdates() const {
    return impl_->outbound.dropped();
}
} // namespace engine::networking
