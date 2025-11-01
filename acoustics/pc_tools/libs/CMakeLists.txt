set(LIBS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_library(acoustics_osc STATIC
    src/OscPacket.cpp
)

target_include_directories(acoustics_osc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

target_compile_features(acoustics_osc PUBLIC cxx_std_17)
