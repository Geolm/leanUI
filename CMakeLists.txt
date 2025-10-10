cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
project(leanui)

add_executable(leanui_test ./test/test.c ./lean_ui.c)

# Link math library on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(leanui_test m)
endif()

# Compiler-specific flags
if(MSVC)
    # MSVC flags
    target_compile_options(leanui_test PRIVATE /W3 /Zi)
    target_compile_definitions(leanui_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang flags
    target_compile_options(leanui_test PRIVATE -Wall -Wextra)
endif()

# Optional: set debug/release flags
set_target_properties(leanui_test PROPERTIES
    C_FLAGS_DEBUG "-g -O0 -DDEBUG"
    C_FLAGS_RELEASE "-O3"
    CXX_FLAGS_DEBUG "-g -O0"
    CXX_FLAGS_RELEASE "-O3 -DDEBUG"
)

# macOS architectures
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
