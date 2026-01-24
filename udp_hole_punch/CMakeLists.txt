file(GLOB SOURCES "*.cc")

add_executable(peer ${SOURCES}) 

install(TARGETS peer DESTINATION ${PROJECT_BINARY_DIR})
