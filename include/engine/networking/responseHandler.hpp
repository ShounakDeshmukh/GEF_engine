#pragma once 

#include <string>
#include <memory>

#include <functional>


namespace engine::networking {

    
    class connectionManager;

    class responseHandler {
        public:
        responseHandler(std::string connectionString);
        ~responseHandler();

        void run();
        
        void run(std::function<std::string(std::string)> func);

        template <typename T, typename U, typename Func>
        void run(Func&& func);


        private:
        std::unique_ptr<connectionManager> connection_;
    };


}
