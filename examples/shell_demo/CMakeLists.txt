cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../../components")
# Boreas required configuration (notification-index reservation etc.)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.boreas")
# Start locked (type the password alone at the `login: ` prompt): idf.py -DLOGIN=y build
if(LOGIN)
    list(APPEND SDKCONFIG_DEFAULTS sdkconfig.login)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(shell_demo)
