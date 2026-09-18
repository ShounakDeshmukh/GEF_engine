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
    
        reqHand.send("testing 123");
        reqHand.send("Hello World!");
        reqHand.send("MAGIC STRING");
        
    }


    
}