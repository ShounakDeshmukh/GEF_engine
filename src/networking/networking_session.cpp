#include "engine/networking/session.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <zmq.h>

namespace engine::networking {
namespace {
using Clock = std::chrono::steady_clock;

struct Socket {
    void* handle;
    Socket(void* context, int type) : handle(zmq_socket(context, type)) {
        if (!handle)
            throw std::runtime_error(zmq_strerror(zmq_errno()));
        const int linger = 0;
        zmq_setsockopt(handle, ZMQ_LINGER, &linger, sizeof(linger));
        const int timeout = 100;
        zmq_setsockopt(handle, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    }
    ~Socket() { zmq_close(handle); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    void bind(const std::string& address) {
        if (zmq_bind(handle, address.c_str()) != 0)
            throw std::runtime_error("ZeroMQ bind " + address + ": " + zmq_strerror(zmq_errno()));
    }
    void connect(const std::string& address) {
        if (zmq_connect(handle, address.c_str()) != 0)
            throw std::runtime_error("ZeroMQ connect " + address + ": " +
                                     zmq_strerror(zmq_errno()));
    }
    void disconnect(const std::string& address) { zmq_disconnect(handle, address.c_str()); }
    bool send(ByteView bytes) {
        return zmq_send(handle, bytes.data(), bytes.size(), 0) == static_cast<int>(bytes.size());
    }
    std::optional<Bytes> receive() {
        Bytes buffer(Packet::headerSize + Packet::maxPayloadSize + 1);
        const int size = zmq_recv(handle, buffer.data(), buffer.size(), 0);
        if (size < 0)
            return std::nullopt;
        buffer.resize(static_cast<std::size_t>(size));
        return buffer;
    }
};

Bytes stringBytes(const std::string& string) {
    Bytes result;
    for (unsigned char c : string)
        result.push_back(static_cast<std::byte>(c));
    return result;
}

std::string bytesString(ByteView bytes) {
    std::string result;
    for (auto b : bytes)
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
    return result;
}

std::uint32_t read32(ByteView bytes) {
    return std::to_integer<std::uint32_t>(bytes[0]) |
           (std::to_integer<std::uint32_t>(bytes[1]) << 8) |
           (std::to_integer<std::uint32_t>(bytes[2]) << 16) |
           (std::to_integer<std::uint32_t>(bytes[3]) << 24);
}

Bytes welcomePayload(ClientId id, std::uint16_t port) {
    return {std::byte(id & 255),         std::byte((id >> 8) & 255), std::byte((id >> 16) & 255),
            std::byte((id >> 24) & 255), std::byte(port & 255),      std::byte((port >> 8) & 255)};
}

std::string hostOf(const std::string& endpoint) {
    const auto start = endpoint.find("://");
    const auto colon = endpoint.rfind(':');
    if (endpoint.rfind("tcp://", 0) != 0 || start == std::string::npos ||
        colon == std::string::npos || colon <= start + 3 || colon + 1 == endpoint.size())
        throw std::invalid_argument("expected tcp://host:port endpoint");
    return endpoint.substr(start + 3, colon - start - 3);
}

std::string bindAddress(const std::string& endpoint) {
    hostOf(endpoint);
    return "tcp://*:" + endpoint.substr(endpoint.rfind(':') + 1);
}

} // namespace

struct CoordinatorServer::Impl {
    struct Client {
        ClientId id;
        std::string endpoint;
        Clock::time_point lastSeen;
    };
    std::uint16_t registrationPort;
    std::uint16_t firstControlPort;
    WorldPosition worldPosition;
    Clock::time_point origin = Clock::now();
    void* context = nullptr;
    std::atomic<bool> stopping{false};
    std::thread registrationThread;
    std::vector<std::thread> controlThreads;
    std::mutex clientsMutex;
    std::vector<Client> clients;

    Impl(std::uint16_t registration, std::uint16_t control, WorldPosition world)
        : registrationPort(registration), firstControlPort(control),
          worldPosition(std::move(world)) {}

    WorldSnapshot snapshot() {
        const auto elapsed = std::chrono::duration<double>(Clock::now() - origin).count();
        const auto position = worldPosition(elapsed);
        WorldSnapshot result{static_cast<std::uint64_t>(elapsed * 60), position.x, position.y, {}};
        const auto now = Clock::now();
        std::lock_guard lock(clientsMutex);
        for (const auto& client : clients) {
            if (now - client.lastSeen < std::chrono::seconds(3))
                result.peers.push_back({client.id, client.endpoint});
        }
        return result;
    }

    void control(ClientId id, std::uint16_t port, std::promise<std::string> ready) {
        try {
            // Each registered client has a control socket owned by this thread.
            Socket socket(context, ZMQ_REP);
            socket.bind("tcp://*:" + std::to_string(port));
            ready.set_value({});
            while (!stopping) {
                auto bytes = socket.receive();
                if (!bytes)
                    continue;
                const auto request = decode(*bytes);
                if (request && request->type == MessageType::Ping && request->sender == id) {
                    std::lock_guard lock(clientsMutex);
                    clients[id - 1].lastSeen = Clock::now();
                }
                const auto world = snapshot();
                socket.send(encode(
                    {MessageType::SharedWorldState, kServerId, 0, world.tick, encodeWorld(world)}));
            }
        } catch (const std::exception& error) {
            try {
                ready.set_value(error.what());
            } catch (...) {
            }
        }
    }

    void registration(std::promise<std::string> ready) {
        try {
            Socket socket(context, ZMQ_REP);
            socket.bind("tcp://*:" + std::to_string(registrationPort));
            ready.set_value({});
            while (!stopping) {
                auto bytes = socket.receive();
                if (!bytes)
                    continue;
                const auto request = decode(*bytes);
                if (!request || request->type != MessageType::Register ||
                    request->payload.empty() || request->payload.size() > 255) {
                    socket.send(encode({MessageType::Welcome, kServerId, 0, 0, {}}));
                    continue;
                }
                const auto endpoint = bytesString(request->payload);
                try {
                    hostOf(endpoint);
                } catch (...) {
                    socket.send(encode({MessageType::Welcome, kServerId, 0, 0, {}}));
                    continue;
                }
                ClientId id;
                {
                    std::lock_guard lock(clientsMutex);
                    id = static_cast<ClientId>(clients.size() + 1);
                }
                if (id > 255 || firstControlPort + id > 65535) {
                    socket.send(encode({MessageType::Welcome, kServerId, 0, 0, {}}));
                    continue;
                }
                {
                    std::lock_guard lock(clientsMutex);
                    clients.push_back({id, endpoint, Clock::now()});
                }
                const auto port = static_cast<std::uint16_t>(firstControlPort + id);
                std::promise<std::string> controlReady;
                auto future = controlReady.get_future();
                controlThreads.emplace_back(
                    [this, id, port, promise = std::move(controlReady)]() mutable {
                        control(id, port, std::move(promise));
                    });
                if (!future.get().empty()) {
                    {
                        std::lock_guard lock(clientsMutex);
                        clients.pop_back();
                    }
                    socket.send(encode({MessageType::Welcome, kServerId, 0, 0, {}}));
                    continue;
                }
                socket.send(
                    encode({MessageType::Welcome, kServerId, 0, 0, welcomePayload(id, port)}));
            }
        } catch (const std::exception& error) {
            try {
                ready.set_value(error.what());
            } catch (...) {
            }
        }
    }
};

CoordinatorServer::CoordinatorServer(std::uint16_t registrationPort, std::uint16_t firstControlPort,
                                     WorldPosition position)
    : impl_(std::make_unique<Impl>(registrationPort, firstControlPort, std::move(position))) {}
CoordinatorServer::~CoordinatorServer() {
    stop();
}
void CoordinatorServer::start() {
    auto& state = *impl_;
    if (state.context)
        return;
    state.context = zmq_ctx_new();
    if (!state.context)
        throw std::runtime_error("cannot create ZeroMQ context");
    std::promise<std::string> ready;
    auto future = ready.get_future();
    state.registrationThread = std::thread(
        [&state, promise = std::move(ready)]() mutable { state.registration(std::move(promise)); });
    const auto error = future.get();
    if (!error.empty()) {
        stop();
        throw std::runtime_error(error);
    }
}
void CoordinatorServer::stop() {
    auto& state = *impl_;
    if (!state.context)
        return;
    state.stopping = true;
    zmq_ctx_shutdown(state.context);
    if (state.registrationThread.joinable())
        state.registrationThread.join();
    for (auto& thread : state.controlThreads)
        if (thread.joinable())
            thread.join();
    zmq_ctx_term(state.context);
    state.context = nullptr;
}

struct PeerClient::Impl {
    std::string serverEndpoint;
    std::string advertisedEndpoint;
    int updatesPerSecond;
    void* context = nullptr;
    std::atomic<bool> stopping{false};
    std::thread worker;
    std::mutex positionMutex;
    PlayerPosition latest{};
    bool hasPosition = false;
    std::mutex eventsMutex;
    std::vector<PeerEvent> events;
    Impl(std::string server, std::string advertised, int rate)
        : serverEndpoint(std::move(server)), advertisedEndpoint(std::move(advertised)),
          updatesPerSecond(std::max(1, rate)) {}
    void push(PeerEvent event) {
        std::lock_guard lock(eventsMutex);
        // Keep only the newest state for each peer while the game loop is busy.
        if (event.kind != PeerEvent::Kind::Welcome) {
            for (auto& queued : events) {
                if (queued.kind == event.kind && queued.sender == event.sender) {
                    queued = std::move(event);
                    return;
                }
            }
        }
        events.push_back(std::move(event));
    }
    void run(std::promise<std::string> ready) {
        try {
            // This worker owns all client sockets. PUB/SUB carries player states
            // directly between clients; REQ sockets only contact the coordinator.
            Socket pub(context, ZMQ_PUB);
            pub.bind(bindAddress(advertisedEndpoint));
            Socket sub(context, ZMQ_SUB);
            zmq_setsockopt(sub.handle, ZMQ_SUBSCRIBE, "", 0);
            Socket lobby(context, ZMQ_REQ);
            const int registrationTimeout = 3000;
            zmq_setsockopt(lobby.handle, ZMQ_RCVTIMEO, &registrationTimeout,
                           sizeof(registrationTimeout));
            lobby.connect(serverEndpoint);
            if (!lobby.send(
                    encode({MessageType::Register, 0, 0, 0, stringBytes(advertisedEndpoint)})))
                throw std::runtime_error("registration send failed");
            auto replyBytes = lobby.receive();
            const auto reply = replyBytes ? decode(*replyBytes) : std::nullopt;
            if (!reply || reply->type != MessageType::Welcome || reply->payload.size() != 6)
                throw std::runtime_error("registration rejected or timed out");
            const ClientId id = read32(reply->payload);
            const auto port = std::to_integer<unsigned>(reply->payload[4]) |
                              (std::to_integer<unsigned>(reply->payload[5]) << 8);
            Socket control(context, ZMQ_REQ);
            control.connect("tcp://" + hostOf(serverEndpoint) + ":" + std::to_string(port));
            push({PeerEvent::Kind::Welcome, id});
            ready.set_value({});

            std::unordered_map<ClientId, std::string> endpoints;
            std::unordered_map<ClientId, std::uint32_t> sequences;
            std::uint32_t outboundSequence = 0;
            bool awaitingWorld = false;
            auto nextPublish = Clock::now();
            auto nextWorld = Clock::now();
            while (!stopping) {
                const auto now = Clock::now();
                if (now >= nextPublish) {
                    PlayerPosition position;
                    bool available;
                    {
                        std::lock_guard lock(positionMutex);
                        position = latest;
                        available = hasPosition;
                    }
                    if (available) {
                        auto payload = encodePosition(position);
                        pub.send(encode({MessageType::PlayerState, id, ++outboundSequence, 0,
                                         std::move(payload)}));
                    }
                    nextPublish = now + std::chrono::milliseconds(1000 / updatesPerSecond);
                }
                if (!awaitingWorld && now >= nextWorld) {
                    if (control.send(encode({MessageType::Ping, id, 0, 0, {}})))
                        awaitingWorld = true;
                    nextWorld = now + std::chrono::milliseconds(50);
                }
                zmq_pollitem_t items[] = {{sub.handle, 0, ZMQ_POLLIN, 0},
                                          {control.handle, 0, ZMQ_POLLIN, 0}};
                zmq_poll(items, 2, 10);
                if (items[1].revents & ZMQ_POLLIN) {
                    awaitingWorld = false;
                    auto bytes = control.receive();
                    const auto packet = bytes ? decode(*bytes) : std::nullopt;
                    if (packet && packet->type == MessageType::SharedWorldState) {
                        auto world = decodeWorld(packet->payload);
                        if (world) {
                            std::unordered_map<ClientId, std::string> next;
                            for (const auto& peer : world->peers) {
                                if (peer.id == id)
                                    continue;
                                // Directory refresh connects late arrivals without a restart.
                                next[peer.id] = peer.endpoint;
                                if (!endpoints.contains(peer.id) ||
                                    endpoints[peer.id] != peer.endpoint)
                                    sub.connect(peer.endpoint);
                            }
                            for (const auto& [peerId, endpoint] : endpoints)
                                if (!next.contains(peerId) || next[peerId] != endpoint)
                                    sub.disconnect(endpoint);
                            endpoints = std::move(next);
                            push({PeerEvent::Kind::World, 0, {}, std::move(*world)});
                        }
                    }
                }
                if (items[0].revents & ZMQ_POLLIN) {
                    auto bytes = sub.receive();
                    const auto packet = bytes ? decode(*bytes) : std::nullopt;
                    if (packet && packet->type == MessageType::PlayerState &&
                        endpoints.contains(packet->sender) &&
                        (!sequences.contains(packet->sender) ||
                         packet->sequence > sequences[packet->sender])) {
                        auto position = decodePosition(packet->payload);
                        if (position) {
                            sequences[packet->sender] = packet->sequence;
                            push({PeerEvent::Kind::Player, packet->sender, *position});
                        }
                    }
                }
            }
        } catch (const std::exception& error) {
            try {
                ready.set_value(error.what());
            } catch (...) {
            }
        }
    }
};

PeerClient::PeerClient(std::string serverEndpoint, std::string advertisedEndpoint,
                       int updatesPerSecond)
    : impl_(std::make_unique<Impl>(std::move(serverEndpoint), std::move(advertisedEndpoint),
                                   updatesPerSecond)) {}
PeerClient::~PeerClient() {
    stop();
}
void PeerClient::start() {
    auto& state = *impl_;
    if (state.context)
        return;
    state.context = zmq_ctx_new();
    if (!state.context)
        throw std::runtime_error("cannot create ZeroMQ context");
    std::promise<std::string> ready;
    auto future = ready.get_future();
    state.worker = std::thread(
        [&state, promise = std::move(ready)]() mutable { state.run(std::move(promise)); });
    const auto error = future.get();
    if (!error.empty()) {
        stop();
        throw std::runtime_error(error);
    }
}
void PeerClient::stop() {
    auto& state = *impl_;
    if (!state.context)
        return;
    state.stopping = true;
    zmq_ctx_shutdown(state.context);
    if (state.worker.joinable())
        state.worker.join();
    zmq_ctx_term(state.context);
    state.context = nullptr;
}
void PeerClient::publish(PlayerPosition position) {
    std::lock_guard lock(impl_->positionMutex);
    impl_->latest = position;
    impl_->hasPosition = true;
}
std::vector<PeerEvent> PeerClient::drain() {
    std::lock_guard lock(impl_->eventsMutex);
    std::vector<PeerEvent> result;
    result.swap(impl_->events);
    return result;
}

} // namespace engine::networking
