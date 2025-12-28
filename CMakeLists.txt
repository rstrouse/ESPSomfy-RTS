# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)

FetchContent_Declare(
  c1101_driver
  GIT_REPOSITORY https://github.com/Viproz/SmartRC-CC1101-Driver-Lib.git
  GIT_TAG        3664897200d1d695fbc78f7ae5d731cb2acff8be
)

FetchContent_MakeAvailable(c1101_driver)


FetchContent_Declare(
  pubsubclient
  GIT_REPOSITORY https://github.com/knolleary/pubsubclient.git
  GIT_TAG        2d228f2f862a95846c65a8518c79f48dfc8f188c
)

FetchContent_MakeAvailable(pubsubclient)

FetchContent_Declare(
  arduinoWebSockets
  GIT_REPOSITORY https://github.com/Links2004/arduinoWebSockets.git
  GIT_TAG        8d0744eb5e916ec646d83bd1ffed5f643aab04d8
)
FetchContent_MakeAvailable(arduinoWebSockets)


project(main)
