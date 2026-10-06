cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/config/sdkconfig.defaults")
endif()
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5sticks3_local_automation_app)
