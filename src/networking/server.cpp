#include "engine/networking/server.hpp"

#include "engine/log.hpp"
#include "engine/networking/responseHandler.hpp"
#include "engine/threading/threadSafeQueue.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace engine::networking {
namespace {
using Clock = std::chrono::steady_clock;
bool newer(std::uint32_t sequence, std::uint32_t previous) {
    return sequence != 0 &&
           (previous == 0 || (sequence - previous != 0 && sequence - previous < 0x80000000u));
}
} // namespace
struct Server::Impl {
    struct Record {
        ClientId id;
        std::string endpoint;
        bool active = true;
        Clock::time_point lastSeen = Clock::now();
        std::optional<PlayerUpdate> player;
        std::uint32_t sequence = 0;
        std::thread worker;
    };
    ServerConfig config;
    std::atomic<bool> stopping{false};
    std::atomic<std::uint16_t> boundPort{0};
    std::thread joinThread;
    mutable std::mutex mutex;
    std::vector<std::unique_ptr<Record>> records;
    Bytes world;
    std::int64_t worldTick = 0;
    threading::ThreadSafeQueue<ClientEvent> events{4096};
    explicit Impl(ServerConfig c) : config(std::move(c)) {
        if (config.clientTimeout.count() <= 0)
            throw std::invalid_argument("clientTimeout must be positive");
    }
    ServerUpdate snapshotLocked(ClientId recipient = 0) const {
        ServerUpdate result{worldTick, world, {}, {}};
        for (const auto& record : records) {
            if (!record->active)
                continue;
            result.peers.push_back({record->id, record->endpoint});
            if (config.relayPlayers && record->player && record->id != recipient)
                result.players.push_back(*record->player);
        }
        return result;
    }
    bool fitsLocked() const {
        const auto bytes = encodeServerUpdate(snapshotLocked());
        return !bytes.empty() && bytes.size() + 6 <= Packet::maxPayloadSize;
    }
    void departLocked(Record& record) {
        if (!record.active)
            return;
        record.active = false;
        record.player.reset();
        events.push({ClientEvent::Kind::Left, record.id, 0, {}});
    }
    void serveClient(Record& record, std::promise<int> ready) {
        bool announced = false;
        try {
            responseHandler socket(config.bindAddress + ":0");
            socket.setReceiveTimeout(static_cast<int>(
                std::min(config.clientTimeout, std::chrono::milliseconds(100)).count()));
            ready.set_value(socket.port());
            announced = true;
            socket.run<Bytes, Bytes>(
                [&](const Bytes& bytes) {
                    const auto packet = decode(bytes);
                    std::lock_guard lock(mutex);
                    if (!record.active || !packet || packet->sender != record.id)
                        return Bytes{};
                    if (packet->type == MessageType::Leave) {
                        departLocked(record);
                    } else if (packet->type == MessageType::ClientUpdate) {
                        record.lastSeen = Clock::now();
                        if (config.relayPlayers && newer(packet->sequence, record.sequence)) {
                            auto previous = std::move(record.player);
                            record.player = PlayerUpdate{record.id, packet->tick, packet->payload};
                            if (!fitsLocked()) {
                                record.player = std::move(previous);
                                log::warn(
                                    "network player update exceeds the shared snapshot budget");
                                return Bytes{};
                            }
                            record.sequence = packet->sequence;
                            events.push({ClientEvent::Kind::Update, record.id, packet->tick,
                                         packet->payload});
                        } else if (!config.relayPlayers &&
                                   (!packet->payload.empty() || packet->sequence != 0)) {
                            return Bytes{};
                        }
                    } else
                        return Bytes{};
                    const auto update = snapshotLocked(record.id);
                    return encode({MessageType::ServerUpdate, 0, 0, update.worldTick,
                                   encodeServerUpdate(update)});
                },
                [&] {
                    std::lock_guard lock(mutex);
                    if (stopping || Clock::now() - record.lastSeen >= config.clientTimeout)
                        departLocked(record);
                    return record.active;
                });
        } catch (const std::exception& error) {
            if (!announced)
                ready.set_exception(std::current_exception());
            else
                log::error("network client handler: {}", error.what());
            std::lock_guard lock(mutex);
            departLocked(record);
        }
    }
    Bytes registerClient(const Bytes& bytes) {
        const auto packet = decode(bytes);
        if (!packet || packet->type != MessageType::Join || packet->sender != 0)
            return {};
        std::string endpoint;
        for (auto b : packet->payload)
            endpoint.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
        // Empty endpoint denotes relay mode, so a mode mismatch fails during the handshake.
        if ((config.relayPlayers && !endpoint.empty()) ||
            (!config.relayPlayers && !validPeerEndpoint(endpoint)))
            return {};
        Record* record;
        {
            std::lock_guard lock(mutex);
            if (records.size() >= kMaxClientIdPerRun)
                return {};
            for (const auto& item : records)
                if (item->active && !endpoint.empty() && item->endpoint == endpoint)
                    return {};
            auto next = std::make_unique<Record>();
            next->id = static_cast<ClientId>(records.size() + 1);
            next->endpoint = std::move(endpoint);
            record = next.get();
            records.push_back(std::move(next));
            if (!fitsLocked()) {
                records.pop_back();
                return {};
            }
        }
        std::promise<int> ready;
        auto future = ready.get_future();
        record->worker = std::thread([this, record, ready = std::move(ready)]() mutable {
            serveClient(*record, std::move(ready));
        });
        const auto controlPort = future.get();
        std::lock_guard lock(mutex);
        events.push({ClientEvent::Kind::Joined, record->id, 0, {}});
        return encode({MessageType::Welcome, 0, 0, worldTick,
                       encodeWelcome({record->id, static_cast<std::uint16_t>(controlPort),
                                      snapshotLocked(record->id)})});
    }
    void serveJoins(std::promise<void> ready) {
        bool announced = false;
        try {
            responseHandler socket(config.bindAddress + ":" + std::to_string(config.joinPort));
            boundPort = static_cast<std::uint16_t>(socket.port());
            ready.set_value();
            announced = true;
            socket.run<Bytes, Bytes>([&](const Bytes& bytes) { return registerClient(bytes); },
                                     [&] { return !stopping; });
        } catch (const std::exception& error) {
            if (!announced)
                ready.set_exception(std::current_exception());
            else
                log::error("network join handler: {}", error.what());
        }
    }
};
Server::Server(ServerConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Server::~Server() {
    stop();
}
void Server::start() {
    if (impl_->joinThread.joinable())
        return;
    impl_->stopping = false;
    std::promise<void> ready;
    auto future = ready.get_future();
    impl_->joinThread = std::thread(
        [this, ready = std::move(ready)]() mutable { impl_->serveJoins(std::move(ready)); });
    try {
        future.get();
    } catch (...) {
        stop();
        throw;
    }
}
void Server::stop() {
    impl_->stopping = true;
    if (impl_->joinThread.joinable())
        impl_->joinThread.join();
    for (auto& record : impl_->records)
        if (record->worker.joinable())
            record->worker.join();
    std::lock_guard lock(impl_->mutex);
    impl_->records.clear();
    impl_->boundPort = 0;
}
std::uint16_t Server::port() const noexcept {
    return impl_->boundPort;
}
void Server::publishWorld(Bytes world, std::int64_t tick) {
    std::lock_guard lock(impl_->mutex);
    auto previous = std::move(impl_->world);
    impl_->world = std::move(world);
    if (!impl_->fitsLocked()) {
        impl_->world = std::move(previous);
        throw std::length_error("world exceeds the shared snapshot budget");
    }
    impl_->worldTick = tick;
}
std::vector<ClientEvent> Server::drain() {
    auto queued = impl_->events.drainAll();
    return {std::make_move_iterator(queued.begin()), std::make_move_iterator(queued.end())};
}
std::size_t Server::connectedClients() const {
    std::lock_guard lock(impl_->mutex);
    return std::count_if(impl_->records.begin(), impl_->records.end(),
                         [](const auto& r) { return r->active; });
}
std::size_t Server::relayedPlayerCount() const {
    std::lock_guard lock(impl_->mutex);
    return std::count_if(impl_->records.begin(), impl_->records.end(),
                         [](const auto& r) { return r->active && r->player.has_value(); });
}
} // namespace engine::networking
