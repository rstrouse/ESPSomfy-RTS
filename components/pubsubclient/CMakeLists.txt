
idf_component_register(SRCS "${pubsubclient_SOURCE_DIR}/src/PubSubClient.cpp"
                    INCLUDE_DIRS "${pubsubclient_SOURCE_DIR}/src/."
                    REQUIRES "arduino-esp32" # Library requires Arduino
                    )


