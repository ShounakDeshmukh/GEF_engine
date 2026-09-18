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

        std::lock_guard<std::mutex> lock(mutex_);

        zmq::message_t request(req_string.size());
        memcpy(request.data(),req_string.data(), req_string.size());
        connection_->sck.send(request, zmq::send_flags::none);

        zmq::message_t reply;
        auto recvVal = connection_->sck.recv(reply, zmq::recv_flags::none);
        if(!recvVal){return;}
        std::string reply_str(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Received reply from server: " << reply_str << std::endl;
    }


    bool requestHandler::sendAndReceive(void* request, std::size_t reqSize, void* reply, std::size_t repSize)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sendVal = connection_->sck.send(zmq::buffer(request, reqSize), zmq::send_flags::none);
        if(!sendVal) {return false;}
        
        zmq::message_t replyData;
        auto recvVal = connection_->sck.recv(replyData, zmq::recv_flags::none);
        if(!recvVal || replyData.size() != repSize) {return false;}

        memcpy(reply, replyData.data(), repSize);
        return true;

    }
    



}