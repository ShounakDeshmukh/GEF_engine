#include <engine/engine.hpp>
#include <cstring>


int main(int argv, char* argc[]) {
    
    if(argv > 1 && !strcmp(argc[1], "subpub"))
    {
        auto sub = engine::subscriber("tcp://localhost:5555");

        sub.listen();

    }
    else
    {
        auto reqHand = engine::requestHandler("tcp://localhost:5555");
    
        reqHand.send("testing 123");
        reqHand.send("Hello World!");
        reqHand.send("MAGIC STRING");
        
    }


    
}