# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set C++ standard globally for GCC 14.x compatibility with ESP-IDF newlib
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(LCCLightingTouchscreen)

# Ensure LVGL finds our lv_conf.h from project root
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_SOURCE_DIR}" APPEND)
