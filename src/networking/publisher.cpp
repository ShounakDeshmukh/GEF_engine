#include "engine/networking/publisher.hpp"

#include "connectionManager.hpp"

#include <stdexcept>

namespace engine::networking {
publisher::publisher(std::string endpoint)
    : connection_(std::make_unique<connectionManager>(zmq::socket_type::pub)) {
    connection_->sck.bind(endpoint);
    const auto bound = connection_->sck.get(zmq::sockopt::last_endpoint);
    if (bound.starts_with("tcp://"))
        port_ = std::stoi(bound.substr(bound.rfind(':') + 1));
}
publisher::~publisher() = default;
void publisher::publish(std::string input, std::string topic) {
    send(input.data(), input.size(), topic);
}
void publisher::publish(ByteView data, const std::string& topic) {
    send(data.data(), data.size(), topic);
}
void publisher::send(const void* data, std::size_t size, const std::string& topic) {
    std::lock_guard lock(mutex_);
    if (!connection_->sck.send(zmq::buffer(topic), zmq::send_flags::sndmore) ||
        !connection_->sck.send(zmq::buffer(data, size), zmq::send_flags::none))
        throw std::runtime_error("publisher send failed");
}
} // namespace engine::networking
