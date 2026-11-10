cmake_minimum_required(VERSION 3.16)
project(synth)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/lib/$<CONFIGURATION>")

add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)

file(GLOB_RECURSE SOURCES "src/*.cpp" "lib/MidiFile/src/*.cpp")
add_executable(synth ${SOURCES})
set_target_properties(synth
    PROPERTIES
        OUTPUT_NAME synth
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

target_include_directories(synth PRIVATE 
    include 
        "include"
        "lib/MidiFile/include"
        "lib/nlohmann/include"
)

target_compile_options(synth PRIVATE -Wall -Wextra -g -O0)
target_link_libraries(synth PRIVATE SDL3::SDL3)