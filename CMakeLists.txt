cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(atp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

add_subdirectory(plog)
add_subdirectory(fmt)

add_subdirectory(libs/stun)
add_subdirectory(stunc)

add_subdirectory(udp_hole_punch)
add_subdirectory(poc)

add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)
