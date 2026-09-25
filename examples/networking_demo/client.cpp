#include <engine/engine.hpp>
#include <cstring>

#include <engine/entity.hpp>

#include <iostream> 
#include <future>
#include <thread>

std::ostream& operator<<(std::ostream& out, const engine::Transform& t)
{
    out << "x: " << t.position.x << " y: " << t.position.y << std::endl;
    return out;
}


int main(int argv, char* argc[]) {
    
    if(argv > 1 && !strcmp(argc[1], "subpub"))
    {
        auto sub = engine::networking::subscriber("tcp://localhost:5555", "test/");
        auto sub2 = engine::networking::subscriber("tcp://localhost:5555", "heartbeat/");


        auto i = std::async(std::launch::async, [&]()
        {
            while(true)
            {
                auto j = sub2.listen();
                std::cout << j << std::endl;
            }
        });

        while(true)
        {
            auto i = sub.listenT<engine::Transform>();
            std::cout << i;
        }

    }
    else
    {
        auto reqHand = engine::networking::requestHandler("tcp://localhost:5555");
    

        if(argv > 1 && !strcmp(argc[1], "string"))
        {
            std::string msg = "Hello from client";
            std::cout << "Sending string " << msg << std::endl;
            auto response = reqHand.send(msg);
            std::cout << "received response : " << response << std::endl;
        }
        else if (argv > 1 && !strcmp(argc[1], "template"))
        {
            engine::Transform boi; 
            boi.position.x = 23;
            boi.position.y = 43;
    
            
            auto retVal = reqHand.send<engine::Transform, engine::Transform>(boi);
            
            
            if(!retVal.second)
            {
                std::cerr << "template request failed" << std::endl;
            }
            else
            {    
                std::cout << "template request data " << boi;
                std::cout << "template response data " << retVal.first;
            }
        }
        else if (argv > 1 && !strcmp(argc[1], "bytes"))
        {
            std::string msg = "variable sized byte message";
            engine::networking::ByteView request{reinterpret_cast<const std::byte*>(msg.data()), msg.size()};
            auto retVal = reqHand.send(request);
            auto response = retVal.first;
            if(!retVal.second)
            {
                std::cerr << "byte request failed" << std::endl;
            }
            else
            {    
                std::string responseString(reinterpret_cast<const char*>(response.data()), response.size());
                std::cout << "byte request string :" << msg << std::endl;
                std::cout << "byte response string :\"" << responseString << "\""  << std::endl;
            }
        }
        else
        {
            std::cout << "Usage: ./client (string/template/bytes/subpub)" << std::endl;
        }
        
    }
    
}