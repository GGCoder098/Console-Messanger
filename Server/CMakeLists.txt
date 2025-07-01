cmake_minimum_required(VERSION 3.31)
project(Server)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

include_directories(.)


add_executable(Server "server.cpp"
)

target_link_libraries(Server wsock32 ws2_32)
