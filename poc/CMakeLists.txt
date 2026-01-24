file(GLOB SOURCES "*.cc")

add_executable(recv ${SOURCES}) 

install(TARGETS recv DESTINATION ${PROJECT_BINARY_DIR})
