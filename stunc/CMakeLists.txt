file(GLOB SOURCES "*.cc")

add_executable(stunc ${SOURCES}) 

target_link_libraries(stunc PRIVATE stun)

install(TARGETS stunc DESTINATION ${PROJECT_BINARY_DIR})
