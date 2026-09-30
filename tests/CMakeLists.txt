add_executable(engine_tests test_log.cpp test_entity.cpp test_physics.cpp test_input_handler.cpp
               test_timeline.cpp test_core_types.cpp test_threading.cpp
               test_networking_protocol.cpp test_networking_session.cpp)
target_link_libraries(engine_tests PRIVATE engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(engine_tests)

if(UNIX)
    add_executable(network_probe network_probe.cpp)
    target_link_libraries(network_probe PRIVATE engine::engine)

    add_executable(network_process_test network_process_test.cpp)
    target_compile_features(network_process_test PRIVATE cxx_std_20)
    add_test(NAME network_three_processes
        COMMAND $<TARGET_FILE:network_process_test> $<TARGET_FILE:network_probe>)
endif()
