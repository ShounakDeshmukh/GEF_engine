#include "engine/networking/responseHandler.hpp"
#include "connectionManager.hpp"

#include <zmq.hpp>
#include <iostream>

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
            std::cerr << "failed to bind to connection" << std::endl;
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
            std::cerr << "responseHandler already running" << std::endl;
            return;
        }

        RunningGuard guard{running_};
        stop_.store(false);

        while (!stop_.load()) {
            zmq::message_t request;
            connection_->sck.recv(request, zmq::recv_flags::none);

            std::string request_str(static_cast<char*>(request.data()), request.size());
            std::cout << "Received request from client: " << request_str << std::endl;

            std::string reply_str = "Hello Client: you sent " + request_str;
            sendRaw(reply_str.data(), reply_str.size(), NetworkError::None);
        }
    }


    void responseHandler::run(std::function<std::string(std::string)> func)
    {

        if(running_.exchange(true))
        {
            std::cerr << "responseHandler already running" << std::endl;
            return;
        }
        stop_.store(false);

        RunningGuard guard{running_};

        while (!stop_.load()) {
            zmq::message_t request;
            connection_->sck.recv(request, zmq::recv_flags::none);

            std::string request_str(static_cast<char*>(request.data()), request.size());

            std::string reply_str = func(request_str);
            zmq::message_t reply(reply_str.size());
            memcpy(reply.data(), reply_str.data(), reply_str.size());
            connection_->sck.send(reply, zmq::send_flags::none);
        }
    }

    void responseHandler::setReceiveTimeout(int milliseconds)
    {
        connection_->sck.set(zmq::sockopt::rcvtimeo, milliseconds);
    }


    ReceivedStatus responseHandler::receiveRaw(void* data, std::size_t size)
    {
        zmq::message_t request;
        auto recvVal = connection_->sck.recv(request, zmq::recv_flags::none);

        if(!recvVal) {return ReceivedStatus::NoMessage;}
        if(request.size() != size) {return ReceivedStatus::InvalidSize;}
        memcpy(data, request.data(), size);
        return ReceivedStatus::Success;
    }

    ReceivedStatus responseHandler::receiveRaw(Bytes& data)
    {
        zmq::message_t request;
        auto recvVal = connection_->sck.recv(request, zmq::recv_flags::none);

        if(!recvVal) {return ReceivedStatus::NoMessage;}
        data.resize(request.size());
        memcpy(data.data(), request.data(), request.size());
        return ReceivedStatus::Success;
    }


    void responseHandler::sendRaw(const void* data, std::size_t size, NetworkError err)
    {
        connection_->sck.send(zmq::buffer(&err, sizeof(err)), zmq::send_flags::sndmore);
        connection_->sck.send(zmq::buffer(data, size), zmq::send_flags::none);
    }


}