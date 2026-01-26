# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set C++ standard globally for GCC 14.x compatibility with ESP-IDF newlib
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell LVGL where to find our lv_conf.h (must be before project.cmake)
# This ensures LVGL compiles with our font settings (fonts 12-32 enabled)
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(LCCLightingTouchscreen)
