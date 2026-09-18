#pragma once 

#include <string>
#include <memory>


namespace engine::networking {

    class connectionManager;

    //Publish to multiple topics per connection
    class publisher {
        public:
        publisher(std::string connectionString);
        ~publisher();

        void publish(std::string input, std::string topic = "");

        template <typename T> 
        void publish(const T& data, std::string topic = "")
        {
            static_assert(std::is_trivially_copyable_v<T>, "requires a trivially copyable datatype");
            send(&data, sizeof(T), topic);
        } 

        private:
        std::unique_ptr<connectionManager> connection_;

        void send(const void* data, std::size_t size, std::string topic);

    };

}
