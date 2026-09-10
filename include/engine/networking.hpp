#pragma once 

#include <string>


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

        private:
        std::unique_ptr<connectionManager> connection_;
    };


}
