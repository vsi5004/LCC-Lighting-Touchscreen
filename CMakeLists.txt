# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set C++ standard globally for GCC 14.x compatibility with ESP-IDF newlib
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Copy lv_conf.h to managed_components/ so LVGL finds our configuration
# This is needed because managed_components/ is gitignored and downloaded fresh in CI
# LVGL looks for lv_conf.h in this location when built as an ESP-IDF component
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/managed_components")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h"
     DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/managed_components/")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(LCCLightingTouchscreen)
