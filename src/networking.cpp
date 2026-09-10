#include "engine/networking.hpp"

#include <zmq.hpp>

#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace engine {

    class connectionManager{
        public:
            connectionManager(zmq::socket_type socket_type):
            ctx{1}, sck{ctx, socket_type} {
            }

        zmq::context_t ctx;
        zmq::socket_t sck; 
        
            

    };



    responseHandler::responseHandler(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::rep);
        try {
            connection_->sck.bind(connectionString);
        }
        catch (...)
        {
            std::cerr << "failed to bind to connection" << std::endl;
        }
    }


    responseHandler::~responseHandler()
    {
        connection_->sck.close();
        connection_->ctx.close();
    }

    void responseHandler::run()
    {
        while (true) {
            zmq::message_t request;
            connection_->sck.recv(request, zmq::recv_flags::none);

            std::string request_str(static_cast<char*>(request.data()), request.size());
            std::cout << "Received request from client: " << request_str << std::endl;

            std::string reply_str = "Hello Client: you sent " + request_str;
            zmq::message_t reply(reply_str.size());
            memcpy(reply.data(), reply_str.data(), reply_str.size());
            connection_->sck.send(reply, zmq::send_flags::none);
        }
    }


    void responseHandler::run(std::function<std::string(std::string)> func)
    {
        while (true) {
            zmq::message_t request;
            connection_->sck.recv(request, zmq::recv_flags::none);

            std::string request_str(static_cast<char*>(request.data()), request.size());
            // std::cout << "Received request from client: " << request_str << std::endl;

            std::string reply_str = func(request_str); //"Hello Client: you sent" + request_str;
            zmq::message_t reply(reply_str.size());
            memcpy(reply.data(), reply_str.data(), reply_str.size());
            connection_->sck.send(reply, zmq::send_flags::none);
        }
    }



    requestHandler::requestHandler(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::req);
        try {
            connection_->sck.connect(connectionString);
        }
        catch (...)
        {
            std::cerr << "failed to bind to connection" << std::endl;
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
        connection_->sck.recv(reply, zmq::recv_flags::none);
        std::string reply_str(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Received reply from server: " << reply_str << std::endl;
    }

}
