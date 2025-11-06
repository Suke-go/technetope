cmake_minimum_required(VERSION 3.20)

project(locomotion_robot LANGUAGES CXX)

add_library(locomotion_robot STATIC
    src/MotionPlanner.cpp
)

target_include_directories(locomotion_robot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(locomotion_robot PUBLIC cxx_std_20)
