cmake_minimum_required(VERSION 3.22)
project(2026-lab1-myFTP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
add_executable(ftp_server ftp_server.cpp)
add_executable(ftp_client ftp_client.cpp)
target_link_libraries(ftp_server PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ftp_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ftp_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
