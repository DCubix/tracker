cmake_minimum_required(VERSION 3.11)
project(synth)

add_definitions(-DSDL_MAIN_HANDLED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SRC "src/*.h" "src/*.cpp")

find_package(SDL2 CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2)