#pragma once 

#include <string>
#include <memory>

#include <functional>
#include <atomic>


namespace engine::networking {

    
    class connectionManager;

    /** Server side request-reply handler */
    class responseHandler {
        public:
        responseHandler(std::string connectionString);
        ~responseHandler();

        /** Enforces single thread usage */
        void run();
        
        /** Enforces single thread usage */
        void run(std::function<std::string(std::string)> func);

        /** Enforces single thread usage */
        template <typename T, typename U, typename Func>
        void run(Func&& func);


        private:
        std::unique_ptr<connectionManager> connection_;
        std::atomic<bool> running_{false};
    };


}
