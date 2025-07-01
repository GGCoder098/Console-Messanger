cmake_minimum_required(VERSION 3.31)
project(Client)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

# Add Unicode definitions
add_definitions(-DUNICODE -D_UNICODE)

# Include directories
include_directories(.)

# Header files


# Add resource file

# Set WIN32 to create a Windows GUI application instead of a console application
add_executable(Client client.cpp)

target_link_libraries(Client
    wsock32
    ws2_32
    comctl32
    user32
    gdi32
    comdlg32
)
