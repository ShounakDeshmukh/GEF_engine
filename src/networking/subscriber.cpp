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
            if(!connectionString.empty()) {connection_->sck.connect(connectionString);}
        }
        catch (...)
        {
            std::cerr << "failed to connect to connection" << std::endl;
            throw std::runtime_error("Failed to create subscriber"); 
        }
    }

    subscriber::~subscriber()
    {
        connection_->sck.close();
    }

    bool subscriber::connect(const std::string& endpoint)
    {
        try {
            connection_->sck.connect(endpoint);
        }
        catch (...)
        {
            std::cerr << "failed to connect to " << endpoint << std::endl;
            return false;
        }
        return true;
    }

    bool subscriber::disconnect(const std::string& endpoint)
    {
        try {
            connection_->sck.disconnect(endpoint);
        }
        catch (...)
        {
            std::cerr << "failed to disconnect from " << endpoint << std::endl;
            return false;
        }
        return true;
    }

    void subscriber::setReceiveTimeout(int milliseconds)
    {
        connection_->sck.set(zmq::sockopt::rcvtimeo, milliseconds);
    }

    std::string subscriber::listen()
    {
        std::string message;
        listen(message);
        return message;
    }

    ReceivedStatus subscriber::listen(std::string& message)
    {
        Bytes update;
        auto status = receive(update);
        if(status != ReceivedStatus::Success) {return status;}
        message.assign(reinterpret_cast<const char*>(update.data()), update.size());
        return status;
    }

    Bytes subscriber::listenBytes()
    {
        Bytes message;
        listenBytes(message);
        return message;
    }

    ReceivedStatus subscriber::listenBytes(Bytes& message)
    {
        return receive(message);
    }

    ReceivedStatus subscriber::receive(void* data, std::size_t size)
    {
        Bytes update;
        auto status = receive(update);
        if(status != ReceivedStatus::Success) {return status;}
        if(update.size() != size) {return ReceivedStatus::InvalidSize;}
        std::memcpy(data, update.data(), size);
        return status;
    }

    ReceivedStatus subscriber::receive(Bytes& data)
    {
        if(running_.exchange(true))
        {
            std::cerr << "subscriber already listening on another thread" << std::endl;
            return ReceivedStatus::NoMessage;
        }

        RunningGuard guard{running_};

        zmq::message_t topic;
        zmq::message_t update;
        try {
            if(!connection_->sck.recv(topic, zmq::recv_flags::none)) {return ReceivedStatus::NoMessage;}
            // publisher always sends [topic][data]; parts of a multipart message arrive
            // together, so this second recv never waits
            if(!topic.more()) {return ReceivedStatus::InvalidSize;}
            if(!connection_->sck.recv(update, zmq::recv_flags::none)) {return ReceivedStatus::NoMessage;}
        }
        catch (const zmq::error_t& e)
        {
            // EINTR: a signal (e.g. SDL's SIGINT handler) interrupted the wait
            if(e.num() == EINTR) {return ReceivedStatus::NoMessage;}
            std::cerr << "subscriber receive failed: " << e.what() << std::endl;
            return ReceivedStatus::Closed;
        }

        data.resize(update.size());
        std::memcpy(data.data(), update.data(), update.size());
        return ReceivedStatus::Success;
    }

}
