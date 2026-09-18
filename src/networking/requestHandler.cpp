#include "engine/networking/requestHandler.hpp"
#include "connectionManager.hpp"

#include <zmq.hpp>
#include <iostream>

namespace engine::networking 
{
    requestHandler::requestHandler(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::req);
        try {
            connection_->sck.connect(connectionString);
        }
        catch (...)
        {
            std::cerr << "failed to connect to connection" << std::endl;
        }
    }


    requestHandler::~requestHandler()
    {
        connection_->sck.close();
        connection_->ctx.close();
    }


    void requestHandler::send(std::string req_string)
    {
        zmq::message_t request(req_string.size());
        memcpy(request.data(),req_string.data(), req_string.size());
        connection_->sck.send(request, zmq::send_flags::none);

        zmq::message_t reply;
        auto recvVal = connection_->sck.recv(reply, zmq::recv_flags::none);
        if(!recvVal){return;}
        std::string reply_str(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Received reply from server: " << reply_str << std::endl;
    }


    template <typename T, typename U>
    std::pair<T, bool> requestHandler::send(U msg)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(std::is_trivially_copyable_v<U>);
        
        T reply {};

        auto sendVal = connection_->sck.send(zmq::buffer(&msg, sizeof(U)), zmq::send_flags::none);
        if(!sendVal) {return std::pair<T, bool>(reply, false);}

        zmq::message_t replyData;
        auto recvVal = connection_->sck.recv(replyData, zmq::recv_flags::none);

        if(!recvVal) {return std::pair<T, bool>(reply, false);}

        if(replyData.size() != sizeof(T))
        {
            throw std::runtime_error("Unexpected request size");
        }

        memcpy(&reply, replyData.data(), sizeof(T));

        return std::pair<T, bool>(reply, true);
        
    }


}