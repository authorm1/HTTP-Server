cmake_minimum_required(VERSION 3.20)

project(http_server LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This HTTP server targets Linux only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(http_server_core
    src/http_response.cpp
    src/server.cpp
)
target_include_directories(http_server_core PUBLIC src)

add_executable(http_server
    src/main.cpp
)
target_link_libraries(http_server PRIVATE http_server_core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
