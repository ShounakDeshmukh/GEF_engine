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
    
        engine::Transform boi; 
        boi.position.x = 23;
        boi.position.y = 43;

        
        auto retVal = reqHand.send<engine::Transform, engine::Transform>(boi);
        
        
        if(retVal.second != engine::networking::NetworkError::None)
        {
            std::cerr << "request failed" << std::endl;
        }
        else
        {    
            std::cout << "request data " << boi;
            std::cout << "response data " << retVal.first;
        }


        // reqHand.send("testing 123");
        // reqHand.send("Hello World!");
        // reqHand.send("MAGIC STRING");
        
    }


    
}