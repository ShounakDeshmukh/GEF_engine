#include "engine/networking/subscriber.hpp"

#include "connectionManager.hpp"
#include "engine/log.hpp"

namespace engine::networking {
subscriber::subscriber(std::string endpoint, std::string topic)
    : connection_(std::make_unique<connectionManager>(zmq::socket_type::sub)) {
    connection_->sck.set(zmq::sockopt::subscribe, topic);
    if (!endpoint.empty())
        connect(endpoint);
}
subscriber::~subscriber() = default;
void subscriber::connect(const std::string& endpoint) {
    connection_->sck.connect(endpoint);
}
void subscriber::disconnect(const std::string& endpoint) {
    connection_->sck.disconnect(endpoint);
}
void subscriber::setReceiveTimeout(int milliseconds) {
    connection_->sck.set(zmq::sockopt::rcvtimeo, milliseconds);
}
std::optional<Bytes> subscriber::tryReceive() {
    if (running_.exchange(true)) {
        log::error("subscriber already receiving");
        return std::nullopt;
    }
    struct Guard {
        std::atomic<bool>& running;
        ~Guard() { running = false; }
    } guard{running_};
    auto& socket = connection_->sck;
    zmq::message_t topic, data;
    if (!socket.recv(topic) || !socket.get(zmq::sockopt::rcvmore) || !socket.recv(data))
        return std::nullopt;
    if (socket.get(zmq::sockopt::rcvmore)) {
        do {
            zmq::message_t extra;
            if (!socket.recv(extra))
                break;
        } while (socket.get(zmq::sockopt::rcvmore));
        return std::nullopt;
    }
    Bytes bytes(data.size());
    if (!bytes.empty())
        std::memcpy(bytes.data(), data.data(), bytes.size());
    return bytes;
}
std::string subscriber::listen() {
    const auto data = tryReceive();
    if (!data || data->empty())
        return {};
    return {reinterpret_cast<const char*>(data->data()), data->size()};
}
} // namespace engine::networking
