cmake_minimum_required(VERSION 3.25)

if(UNIX AND NOT APPLE)
    target_link_libraries(leanui_test m)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "-O3 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DDEBUG -Wall -Wextra")
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)

project(leanui)

add_executable(leanui_test ./test/test.c ./lean_ui.c)