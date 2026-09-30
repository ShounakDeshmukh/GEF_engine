#include "engine/networking/responseHandler.hpp"
#include "connectionManager.hpp"

#include <zmq.hpp>
#include "engine/log.hpp"

namespace engine::networking {


    responseHandler::responseHandler(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::rep);
        try {
            connection_->sck.bind(connectionString);
            auto endpoint = connection_->sck.get(zmq::sockopt::last_endpoint);
            port_ = std::stoi(endpoint.substr(endpoint.rfind(':') + 1));
        }
        catch (...)
        {
            log::error("failed to bind to connection");
            throw std::runtime_error("Failed to create responseHandler"); 
        }
    }


    responseHandler::~responseHandler()
    {
        connection_->sck.close();
    }

    void responseHandler::run()
    {
        if(running_.exchange(true))
        {
            log::error("responseHandler already running");
            return;
        }

        RunningGuard guard{running_, stop_};

        while (!stop_.load()) {
            Bytes request;
            auto status = receiveRaw(request);
            if(status == ReceivedStatus::NoMessage) {continue;}
            if(status == ReceivedStatus::Closed) {break;}

            std::string request_str(reinterpret_cast<const char*>(request.data()), request.size());
            log::debug("responseHandler received: {}", request_str);

            std::string reply_str = "Hello Client: you sent " + request_str;
            sendRaw(reply_str.data(), reply_str.size(), NetworkError::None);
        }
    }


    void responseHandler::run(std::function<std::string(std::string)> func)
    {

        if(running_.exchange(true))
        {
            log::error("responseHandler already running");
            return;
        }

        RunningGuard guard{running_, stop_};

        while (!stop_.load()) {
            Bytes request;
            auto status = receiveRaw(request);
            if(status == ReceivedStatus::NoMessage) {continue;}
            if(status == ReceivedStatus::Closed) {break;}

            std::string request_str(reinterpret_cast<const char*>(request.data()), request.size());

            std::string reply_str;
            auto err = NetworkError::None;
            try {
                reply_str = func(request_str);
            }
            catch (...)
            {
                err = NetworkError::HandlerError;
            }
            sendRaw(reply_str.data(), reply_str.size(), err);
        }
    }

    void responseHandler::setReceiveTimeout(int milliseconds)
    {
        connection_->sck.set(zmq::sockopt::rcvtimeo, milliseconds);
    }


    ReceivedStatus responseHandler::receiveRaw(void* data, std::size_t size)
    {
        Bytes request;
        auto status = receiveRaw(request);
        if(status != ReceivedStatus::Success) {return status;}
        if(request.size() != size) {return ReceivedStatus::InvalidSize;}
        memcpy(data, request.data(), size);
        return ReceivedStatus::Success;
    }

    ReceivedStatus responseHandler::receiveRaw(Bytes& data)
    {
        zmq::message_t request;
        try {
            auto recvVal = connection_->sck.recv(request, zmq::recv_flags::none);
            if(!recvVal) {return ReceivedStatus::NoMessage;}
        }
        catch (const zmq::error_t& e)
        {
            // EINTR: a signal (e.g. SDL's SIGINT handler) interrupted the wait
            if(e.num() == EINTR) {return ReceivedStatus::NoMessage;}
            log::error("responseHandler receive failed: {}", e.what());
            return ReceivedStatus::Closed;
        }

        data.resize(request.size());
        memcpy(data.data(), request.data(), request.size());
        return ReceivedStatus::Success;
    }


    void responseHandler::sendRaw(const void* data, std::size_t size, NetworkError err)
    {
        try {
            connection_->sck.send(zmq::buffer(&err, sizeof(err)), zmq::send_flags::sndmore);
            connection_->sck.send(zmq::buffer(data, size), zmq::send_flags::none);
        }
        catch (const zmq::error_t& e)
        {
            // a later receive reports the broken socket as Closed
            log::error("responseHandler send failed: {}", e.what());
        }
    }


}