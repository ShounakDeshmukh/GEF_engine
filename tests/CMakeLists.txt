add_executable(engine_tests test_log.cpp test_entity.cpp test_physics.cpp test_input_handler.cpp
               test_timeline.cpp test_core_types.cpp test_threading.cpp
               test_networking_transport.cpp test_scene_replicator.cpp test_session.cpp)
target_link_libraries(engine_tests PRIVATE engine Catch2::Catch2WithMain)
target_include_directories(engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(engine_tests)
