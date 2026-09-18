#pragma once 

#include <string>
#include <memory>


namespace engine::networking {

    class connectionManager;

    //Create a subscriber per topic
    class subscriber {
        public:
        subscriber(std::string connectionString, std::string topic = "");
        ~subscriber();

        std::string listen();

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
