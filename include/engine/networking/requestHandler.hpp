#pragma once 

#include <string>
#include <memory>
#include <utility>
#include <mutex>

namespace engine::networking {

    
    class connectionManager;

    class requestHandler {
    
        public:
        requestHandler(std::string connectionString);
        ~requestHandler();
    
        void send(std::string txt);

        //** Returns the requested data and an "isValidReply" bool in case of failure*/
        template <typename T, typename U>
        std::pair<T, bool> send(U msg);
    
        private:
        std::unique_ptr<connectionManager> connection_;
        std::mutex mutex_;
    
    
    };

}
