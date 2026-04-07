cmake_minimum_required(VERSION 3.16)

# zdevice conditionally drops the `driver` dependency on linux,
# so all components can be included on all targets.
set(EXTRA_COMPONENT_DIRS "../components")

if("${IDF_TARGET}" STREQUAL "linux")
    set(COMPONENTS main zkernel zsys zshell zdevice)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(boreas_test)
