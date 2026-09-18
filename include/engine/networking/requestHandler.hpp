#pragma once 

#include <string>
#include <memory>
#include <utility>
#include <mutex>

namespace engine::networking {

    
    class connectionManager;

    /** Client side request-reply handler */
    class requestHandler {
    
        public:
        requestHandler(std::string connectionString);
        ~requestHandler();
    
        /** threadsafe */
        void send(std::string txt);

        //** threadsafe. Returns the requested data and an "isValidReply" bool in case of failure*/
        template <typename T, typename U>
        std::pair<T, bool> send(U msg);
    
        private:
        std::unique_ptr<connectionManager> connection_;
        std::mutex mutex_;
    
    
    };

}
