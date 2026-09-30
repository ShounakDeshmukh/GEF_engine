#include "engine/networking/requestHandler.hpp"

#include "connectionManager.hpp"

#include <cstring>

namespace engine::networking {
requestHandler::requestHandler(std::string endpoint) : endpoint_(std::move(endpoint)) {
    reconnect();
}
requestHandler::~requestHandler() = default;
void requestHandler::reconnect() {
    connection_ = std::make_unique<connectionManager>(zmq::socket_type::req);
    connection_->sck.set(zmq::sockopt::rcvtimeo, timeout_);
    connection_->sck.set(zmq::sockopt::sndtimeo, timeout_);
    connection_->sck.connect(endpoint_);
}
void requestHandler::setReplyTimeout(int milliseconds) {
    std::lock_guard lock(mutex_);
    timeout_ = milliseconds;
    connection_->sck.set(zmq::sockopt::rcvtimeo, timeout_);
    connection_->sck.set(zmq::sockopt::sndtimeo, timeout_);
}
bool requestHandler::send(ByteView msg, Bytes& response) {
    return sendAndReceive(msg.data(), msg.size(), response) == NetworkError::None;
}
bool requestHandler::send(const Bytes& msg, Bytes& response) {
    return send(ByteView{msg}, response);
}
std::pair<Bytes, bool> requestHandler::send(ByteView msg) {
    Bytes reply;
    const bool valid = send(msg, reply);
    return {std::move(reply), valid};
}
std::pair<Bytes, bool> requestHandler::send(const Bytes& msg) {
    return send(ByteView{msg});
}
std::string requestHandler::send(std::string request) {
    Bytes reply;
    if (sendAndReceive(request.data(), request.size(), reply) != NetworkError::None ||
        reply.empty())
        return {};
    return {reinterpret_cast<const char*>(reply.data()), reply.size()};
}
NetworkError requestHandler::sendAndReceive(const void* request, std::size_t size, void* reply,
                                            std::size_t replySize) {
    Bytes bytes;
    const auto error = sendAndReceive(request, size, bytes);
    if (error != NetworkError::None)
        return error;
    if (bytes.size() != replySize)
        return NetworkError::InvalidResponseSize;
    if (replySize)
        std::memcpy(reply, bytes.data(), replySize);
    return NetworkError::None;
}
NetworkError requestHandler::sendAndReceive(const void* request, std::size_t size, Bytes& reply) {
    std::lock_guard lock(mutex_);
    reply.clear();
    const auto failed = [this](NetworkError error) {
        // A timed-out REQ socket cannot send again until it is recreated.
        reconnect();
        return error;
    };
    try {
        auto& socket = connection_->sck;
        if (!socket.send(zmq::buffer(request, size), zmq::send_flags::none))
            return failed(NetworkError::SendFailed);
        zmq::message_t errorFrame, data;
        if (!socket.recv(errorFrame) || errorFrame.size() != sizeof(NetworkError) ||
            !socket.get(zmq::sockopt::rcvmore) || !socket.recv(data) ||
            socket.get(zmq::sockopt::rcvmore))
            return failed(NetworkError::ReceiveFailed);
        NetworkError error;
        std::memcpy(&error, errorFrame.data(), sizeof(error));
        if (error != NetworkError::None)
            return error;
        reply.resize(data.size());
        if (!reply.empty())
            std::memcpy(reply.data(), data.data(), data.size());
        return NetworkError::None;
    } catch (const zmq::error_t&) {
        return failed(NetworkError::ReceiveFailed);
    }
}
} // namespace engine::networking
