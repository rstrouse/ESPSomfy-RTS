
idf_component_register(SRCS "${c1101_driver_SOURCE_DIR}/ELECHOUSE_CC1101_SRC_DRV.cpp"
                    INCLUDE_DIRS "${c1101_driver_SOURCE_DIR}/"
                    REQUIRES "arduino-esp32" # Library requires Arduino
                    )

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation -Wno-maybe-uninitialized)

