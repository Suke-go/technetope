cmake_minimum_required(VERSION 3.18)
project(toio_control_server LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.75 REQUIRED)
find_package(Threads REQUIRED)

add_executable(toio_control_server
    src/main.cpp
    src/app.cpp
    src/relay_connection.cpp
    src/relay_manager.cpp
    src/cube_registry.cpp
    src/fleet_orchestrator.cpp
    src/ws_server.cpp
    src/command_gateway.cpp
    src/util/config_loader.cpp
)

target_include_directories(toio_control_server
    PRIVATE
        include
        third_party
)

target_link_libraries(toio_control_server
    PRIVATE
        Boost::boost
        Threads::Threads
)
