#include <zmq.hpp>

namespace engine::networking {

    zmq::context_t& getContext() {
        static zmq::context_t context(1);
        return context;
    }


    class connectionManager{
        public:
            connectionManager(zmq::socket_type socket_type):
            sck{getContext(), socket_type} {
            }

        zmq::socket_t sck; 
    };

}
