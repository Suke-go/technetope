cmake_minimum_required(VERSION 3.16)
project(acoustics_pc_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ACOUSTICS_ENABLE_ASAN "Enable AddressSanitizer for local builds" OFF)

if(ACOUSTICS_ENABLE_ASAN)
    if (MSVC)
        message(WARNING "ASan option is currently not supported on MSVC in this project.")
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address -fno-omit-frame-pointer)
    endif()
endif()

add_subdirectory(libs)
add_subdirectory(scheduler)
add_subdirectory(monitor)
