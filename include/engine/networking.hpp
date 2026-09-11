#pragma once 

#include <string>
#include <memory>

#include <functional>

namespace engine {

    class tester{
        public:
        static int testFunction();
    
    };

    class connectionManager;

    class requestHandler {

        public:
        requestHandler(std::string connectionString);
        ~requestHandler();

        void send(std::string txt);

        private:
        std::unique_ptr<connectionManager> connection_;


    };

    class responseHandler {
        public:
        responseHandler(std::string connectionString);
        ~responseHandler();

        void run();
        
        void run(std::function<std::string(std::string)> func);

        private:
        std::unique_ptr<connectionManager> connection_;
    };

    class publisher {
        public:
        publisher(std::string connectionString);
        ~publisher();

        void publish();
        void publish(std::string input);

        template <typename T> 
        void publish(const T& data)
        {
            static_assert(std::is_trivially_copyable_v<T>, "requires a trivially copyable datatype");
            send(&data, sizeof(T));
        } 

        private:
        std::unique_ptr<connectionManager> connection_;

        void send(const void* data, std::size_t size);

    };

    class subscriber {
        public:
        subscriber(std::string connectionString);
        ~subscriber();

        void listen();

        template<typename T>
        T listenT()
        {
            static_assert(std::is_trivially_copyable_v<T>, "requires a trivially copyable datatype");
            T result{};
            try {
                receive(&result, sizeof(T));
            } catch (...)
            {
                throw std::runtime_error("Received malformed message");
            }

            return result;
        }

        private:
        std::unique_ptr<connectionManager> connection_;

        void receive(void* data, std::size_t size);


    };


}
