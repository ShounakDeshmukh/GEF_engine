add_executable(peer_to_peer_server server.cpp)
target_link_libraries(peer_to_peer_server PRIVATE engine::engine)

add_executable(peer_to_peer_client client.cpp)
target_link_libraries(peer_to_peer_client PRIVATE engine::engine)
