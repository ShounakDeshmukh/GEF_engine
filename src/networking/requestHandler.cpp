#include "engine/networking/requestHandler.hpp"
#include "connectionManager.hpp"

#include <zmq.hpp>
#include <iostream>

namespace engine::networking 
{
    requestHandler::requestHandler(std::string connectionString)
        : endpoint_(std::move(connectionString))
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::req);
        try {
            connection_->sck.connect(endpoint_);
        }
        catch (...)
        {
            std::cerr << "failed to connect to connection" << std::endl;
            throw std::runtime_error("Failed to create requestHandler"); 
        }
    }


    requestHandler::~requestHandler()
    {
        connection_->sck.close();
    }

    void requestHandler::setReplyTimeout(int milliseconds)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timeoutMs_ = milliseconds;
        connection_->sck.set(zmq::sockopt::rcvtimeo, milliseconds);
    }

    void requestHandler::reconnect()
    {
        try {
            connection_->sck.set(zmq::sockopt::linger, 0);  // drop the abandoned request
            connection_->sck.close();

            connection_ = std::make_unique<connectionManager>(zmq::socket_type::req);
            connection_->sck.set(zmq::sockopt::linger, 0);
            connection_->sck.set(zmq::sockopt::rcvtimeo, timeoutMs_);  // options don't carry over
            connection_->sck.connect(endpoint_);
        }
        catch (const zmq::error_t& e) {
            std::cerr << "requestHandler reconnect failed: " << e.what() << std::endl;
        }
    }


    bool requestHandler::send(ByteView msg, Bytes& response)
    {
        NetworkError error = sendAndReceive(msg.data(), msg.size(), response);
        bool valid = (error == NetworkError::None);
        //TODO: adding logging on error
        return valid;
    }

    bool requestHandler::send(const Bytes& msg, Bytes& response)
    {
        return send(ByteView{msg}, response);
    }



    std::pair<Bytes, bool> requestHandler::send(ByteView msg)
    {
        Bytes reply;
        NetworkError error = sendAndReceive(msg.data(), msg.size(), reply);
        bool valid = (error == NetworkError::None);
        //TODO: adding logging on error
        return {std::move(reply), valid};
    }
    
    std::pair<Bytes, bool> requestHandler::send(const Bytes& msg)
    {
        return send(ByteView{msg});
    }

    std::string requestHandler::send(std::string req_string)
    {

        std::lock_guard<std::mutex> lock(mutex_);
        std::string invalidString = "";

        zmq::message_t request(req_string.size());
        memcpy(request.data(),req_string.data(), req_string.size());
        connection_->sck.send(request, zmq::send_flags::none);

        zmq::message_t errorMessage;
        auto errorRecv = connection_->sck.recv(errorMessage, zmq::recv_flags::none);
        if(!errorRecv) {reconnect(); return invalidString;}
        if(errorMessage.size() != sizeof(NetworkError)) {return invalidString;}
        NetworkError error;
        std::memcpy(&error, errorMessage.data(), sizeof(NetworkError));

        zmq::message_t reply;
        auto recvVal = connection_->sck.recv(reply, zmq::recv_flags::none);
        if(!recvVal){reconnect(); return invalidString;}
        std::string reply_str(static_cast<char*>(reply.data()), reply.size());
        return reply_str;
    }


    NetworkError requestHandler::sendAndReceive(const void* request, std::size_t reqSize, void* reply, std::size_t repSize)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sendVal = connection_->sck.send(zmq::buffer(request, reqSize), zmq::send_flags::none);
        if(!sendVal) {return NetworkError::SendFailed;}
        
        zmq::message_t errorMessage;
        auto errorRecv = connection_->sck.recv(errorMessage, zmq::recv_flags::none);
        if(!errorRecv) {reconnect(); return NetworkError::ReceiveFailed;}
        if(errorMessage.size() != sizeof(NetworkError)) {return NetworkError::ReceiveFailed;}
        NetworkError error;
        std::memcpy(&error, errorMessage.data(), sizeof(NetworkError));

        zmq::message_t replyData;
        auto recvVal = connection_->sck.recv(replyData, zmq::recv_flags::none);
        if(!recvVal) {reconnect(); return NetworkError::ReceiveFailed;}
        if(replyData.size() != repSize) {return NetworkError::InvalidResponseSize;}

        memcpy(reply, replyData.data(), repSize);
        return error;
    }
    
    NetworkError requestHandler::sendAndReceive(const void* request, std::size_t reqSize, Bytes& reply)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sendVal = connection_->sck.send(zmq::buffer(request, reqSize), zmq::send_flags::none);
        if(!sendVal) {return NetworkError::SendFailed;}
        
        zmq::message_t errorMessage;
        auto errorRecv = connection_->sck.recv(errorMessage, zmq::recv_flags::none);
        if(!errorRecv) {reconnect(); return NetworkError::ReceiveFailed;}
        if(errorMessage.size() != sizeof(NetworkError)) {return NetworkError::ReceiveFailed;}
        NetworkError error;
        std::memcpy(&error, errorMessage.data(), sizeof(NetworkError));

        zmq::message_t replyData;
        auto recvVal = connection_->sck.recv(replyData, zmq::recv_flags::none);
        if(!recvVal) {reconnect(); return NetworkError::ReceiveFailed;}

        reply.resize(replyData.size());

        memcpy(reply.data(), replyData.data(), replyData.size());
        return error;
    }



}