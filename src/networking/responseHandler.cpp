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
        if(running_.exchange(true))
        {
            std::cerr << "responseHandler already running" << std::endl;
            return;
        }

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
        running_.store(false);
    }


    void responseHandler::run(std::function<std::string(std::string)> func)
    {

        if(running_.exchange(true))
        {
            std::cerr << "responseHandler already running" << std::endl;
            return;
        }

        while (true) {
            zmq::message_t request;
            connection_->sck.recv(request, zmq::recv_flags::none);

            std::string request_str(static_cast<char*>(request.data()), request.size());

            std::string reply_str = func(request_str);
            zmq::message_t reply(reply_str.size());
            memcpy(reply.data(), reply_str.data(), reply_str.size());
            connection_->sck.send(reply, zmq::send_flags::none);
        }
        running_.store(false);
    }

    template <typename T, typename U, typename Func>
    void responseHandler::run(Func&& func)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(std::is_trivially_copyable_v<U>);

        if(running_.exchange(true))
        {
            std::cerr << "responseHandler already running" << std::endl;
            return;
        }

        while(true)
        {
            zmq::message_t request;
            auto recvVal = connection_->sck.recv(request, zmq::recv_flags::none);

            if(!recvVal) {continue;}

            if(request.size() != sizeof(U))
            {
                throw std::runtime_error("Unexpected request size");
            }

            U requestData {};
            memcpy(&requestData, request.data(), sizeof(U));

            T replyData = std::invoke(std::forward<Func>(func), requestData);
            connection_->sck.send(zmq::buffer(&replyData, sizeof(T)), zmq::send_flags::none);

        }
        running_.store(false);

    }
        


}