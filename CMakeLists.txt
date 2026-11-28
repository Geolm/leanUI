cmake_minimum_required(VERSION 3.25)
project(leanui C)

# C99, as documented in README
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
)

# Graphic regression test : renders every leanUI case offscreen through onedraw's Metal
# backend and compares the screenshots against test/references/. macOS only
if(APPLE)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_executable(leanui_graphic_test
        ./test/graphic_test.c
        ./lean_ui.c
        ./test/third_party/onedraw/onedraw.cpp
    )

    target_include_directories(leanui_graphic_test PRIVATE ./test/third_party ./test/third_party/onedraw)

    target_link_libraries(leanui_graphic_test PRIVATE "-framework Metal" "-framework Foundation")

    # third-party C++ source : suppress its warnings
    set_source_files_properties(./test/third_party/onedraw/onedraw.cpp PROPERTIES COMPILE_OPTIONS "-w")

    target_compile_options(leanui_graphic_test PRIVATE -Wall -Wextra)
endif()

# macOS architectures
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
