cmake_minimum_required(VERSION 3.16)
project(edge_multi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ENET REQUIRED libenet)
pkg_check_modules(RAYLIB REQUIRED raylib)

add_executable(server
    server/main.cpp
    server/world.cpp
)

target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(server PRIVATE ${ENET_INCLUDE_DIRS})
target_link_libraries(server PRIVATE ${ENET_LIBRARIES})

add_executable(client
    client/main.cpp
    client/renderer.cpp
)

target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(client PRIVATE ${ENET_INCLUDE_DIRS} ${RAYLIB_INCLUDE_DIRS})
target_link_libraries(client PRIVATE ${ENET_LIBRARIES} ${RAYLIB_LIBRARIES})
