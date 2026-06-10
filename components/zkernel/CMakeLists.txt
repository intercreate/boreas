set(srcs
    "src/k_sem.c"
    "src/k_mutex.c"
    "src/k_msgq.c"
    "src/k_event.c"
    "src/k_thread.c"
    "src/k_work.c"
    "src/ring_buf.c"
    "src/sys_init.c"
    "src/fatal.c"
)

idf_build_get_property(target IDF_TARGET)

if("${target}" STREQUAL "linux")
    # esp_timer is headers-only on linux: k_timer uses the FreeRTOS
    # dispatcher-task backend instead (see k_timer_linux.c).
    list(APPEND srcs "src/k_timer_linux.c")
else()
    list(APPEND srcs "src/k_timer.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include/boreas" "include"
    REQUIRES freertos esp_timer log esp_system nvs_flash
    LDFRAGMENTS "zkernel.lf"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
