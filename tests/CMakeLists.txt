add_executable(engine_tests test_log.cpp test_entity.cpp test_physics.cpp test_input_handler.cpp
               test_timeline.cpp test_core_types.cpp test_networking_protocol.cpp)
target_link_libraries(engine_tests PRIVATE engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(engine_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(network_probe network_probe.cpp)
target_link_libraries(network_probe PRIVATE engine::engine)
add_test(NAME network_three_processes
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/network_process_test.py
            $<TARGET_FILE:network_probe>)
