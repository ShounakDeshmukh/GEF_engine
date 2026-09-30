#include "engine/networking/responseHandler.hpp"

#include "connectionManager.hpp"

#include <cstring>
#include <stdexcept>

namespace engine::networking {
responseHandler::responseHandler(std::string endpoint)
    : connection_(std::make_unique<connectionManager>(zmq::socket_type::rep)) {
    connection_->sck.bind(endpoint);
    const auto bound = connection_->sck.get(zmq::sockopt::last_endpoint);
    endpoint_ = bound;
    if (bound.starts_with("tcp://"))
        port_ = std::stoi(bound.substr(bound.rfind(':') + 1));
    setReceiveTimeout(100);
    connection_->sck.set(zmq::sockopt::sndtimeo, 100);
}
responseHandler::~responseHandler() {
    if (connection_ && !endpoint_.empty()) {
        try {
            connection_->sck.unbind(endpoint_);
        } catch (...) {
        }
    }
}
void responseHandler::run() {
    run([](std::string request) { return "Hello Client: you sent " + request; });
}
void responseHandler::run(std::function<std::string(std::string)> func) {
    run<Bytes, Bytes>([&func](const Bytes& request) {
        const std::string input(
            request.empty() ? "" : reinterpret_cast<const char*>(request.data()), request.size());
        const auto reply = func(input);
        Bytes bytes(reply.size());
        if (!bytes.empty())
            std::memcpy(bytes.data(), reply.data(), reply.size());
        return bytes;
    });
}
void responseHandler::setReceiveTimeout(int milliseconds) {
    connection_->sck.set(zmq::sockopt::rcvtimeo, milliseconds);
}
ReceivedStatus responseHandler::receiveRaw(void* data, std::size_t size) {
    Bytes bytes;
    const auto status = receiveRaw(bytes);
    if (status != ReceivedStatus::Success)
        return status;
    if (bytes.size() != size)
        return ReceivedStatus::InvalidSize;
    if (size)
        std::memcpy(data, bytes.data(), size);
    return ReceivedStatus::Success;
}
ReceivedStatus responseHandler::receiveRaw(Bytes& bytes) {
    zmq::message_t request;
    auto& socket = connection_->sck;
    if (!socket.recv(request))
        return ReceivedStatus::NoMessage;
    if (socket.get(zmq::sockopt::rcvmore)) {
        do {
            zmq::message_t extra;
            if (!socket.recv(extra))
                break;
        } while (socket.get(zmq::sockopt::rcvmore));
        return ReceivedStatus::InvalidSize;
    }
    bytes.resize(request.size());
    if (!bytes.empty())
        std::memcpy(bytes.data(), request.data(), request.size());
    return ReceivedStatus::Success;
}
void responseHandler::sendRaw(const void* data, std::size_t size, NetworkError error) {
    if (!connection_->sck.send(zmq::buffer(&error, sizeof(error)), zmq::send_flags::sndmore) ||
        !connection_->sck.send(zmq::buffer(data, size), zmq::send_flags::none))
        throw std::runtime_error("responseHandler send failed");
}
} // namespace engine::networking
