#include "engine/networking/subscriber.hpp"
#include "connectionManager.hpp"

#include <zmq.hpp>
#include <string>
#include <iostream>


namespace engine::networking {

    subscriber::subscriber(std::string connectionString, std::string topic)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::sub);
        try {
            connection_->sck.set(zmq::sockopt::subscribe, topic);
            connection_->sck.connect(connectionString);
        }
        catch (...)
        {
            std::cerr << "failed to connect to connection" << std::endl;
        }
    }

    subscriber::~subscriber()
    {
        connection_->sck.close();
        connection_->ctx.close();
    }

    std::string subscriber::listen()
    {
        if(running_.exchange(true))
        {
            std::cerr << "subscriber::listen() already running" << std::endl;
            return;
        }

        zmq::message_t topic;
        zmq::message_t update;

        connection_->sck.recv(topic);
        connection_->sck.recv(update);
        std::string update_str(static_cast<char*>(update.data()), update.size());
        running_.store(false);
        return update_str;
    }

    void subscriber::receive(void* data, std::size_t size)
    {
        if(running_.exchange(true))
        {
            std::cerr << "subscriber::listen() already running" << std::endl;
            return;
        }

        zmq::message_t topic;
        zmq::message_t message;

        connection_->sck.recv(topic, zmq::recv_flags::none);
        auto result = connection_->sck.recv(message, zmq::recv_flags::none);

        if(!result || message.size() != size)
        {
            throw std::runtime_error("Received malformed message");
        }
        std::memcpy(data, message.data(), size);
        running_.store(false);
    }

}