#include <zmq.hpp>

namespace engine::networking {

    class connectionManager{
        public:
            connectionManager(zmq::socket_type socket_type):
            ctx{1}, sck{ctx, socket_type} {
            }

        zmq::context_t ctx;
        zmq::socket_t sck; 
    };

}
