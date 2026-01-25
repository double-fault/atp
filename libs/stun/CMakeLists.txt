file(GLOB SOURCES "stun/*.cc")

add_library(stun ${SOURCES})

target_link_libraries(stun PUBLIC plog fmt::fmt)

target_include_directories(stun PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
