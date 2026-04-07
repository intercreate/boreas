set(srcs)

if(CONFIG_ZSHELL)
    list(APPEND srcs
        "src/shell.c"
        "src/shell_cmd_reg.c"
        "src/shell_parse.c"
        "src/shell_print.c"
        "src/shell_builtins.c"
    )

    if(CONFIG_ZSHELL_VT100)
        list(APPEND srcs "src/shell_vt100.c")
    endif()

    if(CONFIG_ZSHELL_TAB_COMPLETION)
        list(APPEND srcs "src/shell_completion.c")
    endif()

    if(CONFIG_ZSHELL_HISTORY)
        list(APPEND srcs "src/shell_history.c")
    endif()

    # UART backend -- only on real targets
    idf_build_get_property(target IDF_TARGET)
    if(NOT "${target}" STREQUAL "linux")
        list(APPEND srcs "src/shell_uart.c")
    endif()

    # zsys-dependent builtins
    if(CONFIG_ZSHELL_CMD_LOG OR CONFIG_ZSHELL_CMD_THREAD)
        list(APPEND srcs "src/shell_builtins_zsys.c")
    endif()
endif()

set(requires zkernel freertos log zsys)

idf_build_get_property(target IDF_TARGET)
if(NOT "${target}" STREQUAL "linux")
    list(APPEND requires driver)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include/boreas" "include"
    REQUIRES ${requires}
)

if(CONFIG_ZSHELL)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
endif()
