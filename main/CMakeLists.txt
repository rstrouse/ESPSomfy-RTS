
file(GLOB_RECURSE SRCS_CPP *.cpp)
idf_component_register(SRCS ${SRCS_CPP}
                    INCLUDE_DIRS "." REQUIRES esp_common arduinoWebSockets pubsubclient SmartRC-CC1101-Driver-Lib)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-misleading-indentation -Wno-error=address)

# Note: you must have a partition named the first argument (here it's "littlefs")
# in your partition table csv file.
littlefs_create_partition_image(spiffs data FLASH_IN_PROJECT)

