cmake_minimum_required(VERSION 3.16)

# On linux target, exclude zdevice (requires ESP-IDF driver component / real HW HAL)
if("${IDF_TARGET}" STREQUAL "linux")
    set(EXTRA_COMPONENT_DIRS
        "../components/zkernel"
        "../components/zsys"
    )
    set(COMPONENTS main zkernel zsys)
else()
    set(EXTRA_COMPONENT_DIRS "../components")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(boreas_test)
