#include "engine/networking/publisher.hpp"
#include "connectionManager.hpp"

#include <zmq.hpp>
#include <iostream>


namespace engine::networking {

    publisher::publisher(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::pub);
        try {
            connection_->sck.bind(connectionString);
        }
        catch (...)
        {
            std::cerr << "failed to bind to connection" << std::endl;
            throw std::runtime_error("Failed to create publisher"); 
        }
    }

    publisher::~publisher()
    {
        connection_->sck.close();
    }

    void publisher::publish(std::string input, std::string topic)
    {
            std::lock_guard<std::mutex> lock(mutex_);
            zmq::message_t message(input.size());
            memcpy(message.data(), input.data(), input.size());
            connection_->sck.send(zmq::buffer(topic), zmq::send_flags::sndmore);
            connection_->sck.send(message, zmq::send_flags::none);
    }

    void publisher::send(const void* data, std::size_t size, std::string topic)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_->sck.send(zmq::buffer(topic), zmq::send_flags::sndmore);
        connection_->sck.send(zmq::buffer(data, size), zmq::send_flags::none);
    }

}