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
        connection_->sck.recv(reply, zmq::recv_flags::none);
        std::string reply_str(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Received reply from server: " << reply_str << std::endl;
    }



    publisher::publisher(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::pub);
        try {
            connection_->sck.bind(connectionString);
        }
        catch (...)
        {
            std::cerr << "failed to bind to connection" << std::endl;
        }
    }

    publisher::~publisher()
    {
        connection_->sck.close();
        connection_->ctx.close();
    }


    void publisher::publish()
    {
        int count = 0;
        while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
            std::string update = "Message #" + std::to_string(count);
        count = ++count;
            zmq::message_t message(update.size());
            memcpy(message.data(), update.data(), update.size());
            connection_->sck.send(message, zmq::send_flags::none);
            std::cout << "Published: " << update << std::endl;
        }
    }

    void publisher::publish(std::string input)
    {
            zmq::message_t message(input.size());
            memcpy(message.data(), input.data(), input.size());
            connection_->sck.send(message, zmq::send_flags::none);
            std::cout << "Published: " << input << std::endl;
    }

    void publisher::send(const void* data, std::size_t size)
    {
        connection_->sck.send(zmq::buffer(data, size), zmq::send_flags::none);
    }



    subscriber::subscriber(std::string connectionString)
    {
        connection_ = std::make_unique<connectionManager>(zmq::socket_type::sub);
        try {
            connection_->sck.set(zmq::sockopt::subscribe, "");
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

    void subscriber::listen()
    {
        std::cout << "Subscribed to updates...\n";
        while (true) {
            zmq::message_t update;
            connection_->sck.recv(update, zmq::recv_flags::none);
            std::string update_str(static_cast<char*>(update.data()), update.size());
            std::cout << "Received: " << update_str << std::endl;
        }
    }

    void subscriber::receive(void* data, std::size_t size)
    {
        zmq::message_t message;
        std::cout << "here1" << std::endl;

        auto result = connection_->sck.recv(message, zmq::recv_flags::none);
        std::cout << "here2" << std::endl;

        if(!result && message.size() == size)
        {
            throw std::runtime_error("Received malformed message");
        }
        std::memcpy(data, message.data(), size);
    }












}
