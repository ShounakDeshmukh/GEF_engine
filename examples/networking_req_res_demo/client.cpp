#include <engine/engine.hpp>
#include <cstring>

#include <engine/entity.hpp>

#include <iostream> 

std::ostream& operator<<(std::ostream& out, const engine::Transform& t)
{
    out << "x: " << t.position.x << " y: " << t.position.y << std::endl;
    return out;
}


int main(int argv, char* argc[]) {
    
    if(argv > 1 && !strcmp(argc[1], "subpub"))
    {
        auto sub = engine::subscriber("tcp://localhost:5555");

        while(true)
        {
            auto i = sub.listenT<engine::Transform>();
            std::cout << "testing " << std::endl;
            std::cout << i;
        }

    }
    else
    {
        auto reqHand = engine::requestHandler("tcp://localhost:5555");
    
        reqHand.send("testing 123");
        reqHand.send("Hello World!");
        reqHand.send("MAGIC STRING");
        
    }


    
}