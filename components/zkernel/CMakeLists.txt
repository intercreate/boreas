set(srcs
    "src/k_sem.c"
    "src/k_mutex.c"
    "src/k_msgq.c"
    "src/k_event.c"
    "src/k_thread.c"
    "src/sys_init.c"
    "src/fatal.c"
)

idf_build_get_property(target IDF_TARGET)

if(NOT "${target}" STREQUAL "linux")
    # k_timer and k_work require esp_timer (no linux impl)
    list(APPEND srcs "src/k_timer.c" "src/k_work.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include/boreas" "include"
    REQUIRES freertos esp_timer log esp_system nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
