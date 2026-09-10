#include <engine/engine.hpp>



int main() {
    
    auto reqHand = engine::requestHandler("tcp://localhost:5555");

    reqHand.send("testing 123");
    reqHand.send("Hello World!");
    reqHand.send("weirdd");
    
}