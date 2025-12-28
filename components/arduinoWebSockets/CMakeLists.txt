
file(GLOB_RECURSE SRCS_CPP ${arduinowebsockets_SOURCE_DIR}/src/*.cpp)

idf_component_register(SRCS ${SRCS_CPP}
                    INCLUDE_DIRS "${arduinowebsockets_SOURCE_DIR}/src/"
                    REQUIRES "arduino-esp32" # Library requires Arduino
                    )
