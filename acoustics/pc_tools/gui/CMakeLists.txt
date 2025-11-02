set(GUI_SOURCES
    src/main.cpp
    ../scheduler/src/SoundTimeline.cpp
)

add_executable(acoustics_gui
    ${GUI_SOURCES}
)

find_package(OpenGL REQUIRED)

target_include_directories(acoustics_gui
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/libs/include
        ${CMAKE_SOURCE_DIR}/scheduler/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(acoustics_gui
    PRIVATE
        imgui
        implot
        glfw
        OpenGL::GL
        acoustics_osc
        fmt::fmt
        spdlog::spdlog
        asio::asio
)

target_compile_features(acoustics_gui PRIVATE cxx_std_17)

if (UNIX AND NOT APPLE)
    target_link_libraries(acoustics_gui PRIVATE pthread dl)
endif()

set_target_properties(acoustics_gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gui
)
