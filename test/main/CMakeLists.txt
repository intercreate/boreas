set(test_srcs
    "test_main.c"
    "test_timeout.c"
    "test_slist.c"
    "test_dlist.c"
    "test_byteorder.c"
    "test_atomic.c"
    "test_ring_buf.c"
    "test_k_sem.c"
    "test_k_mutex.c"
    "test_k_msgq.c"
    "test_k_event.c"
    "test_retry.c"
    "test_log.c"
    "test_device_registry.c"
)

list(APPEND test_srcs
    "test_k_timer.c"
    "test_k_work.c"
    "test_uptime.c"
    "test_sleep.c"
    "test_k_thread.c"
    "test_k_mutex_pi.c"
    "test_k_event_mt.c"
    "test_k_msgq_mt.c"
    "test_init.c"
)

idf_build_get_property(target IDF_TARGET)

if(NOT "${target}" STREQUAL "linux")
    # Needs zdevice GPIO types (driver component) -- not available on linux
    list(APPEND test_srcs "test_gpio_flags.c")
endif()

if(CONFIG_ZSHELL)
    list(APPEND test_srcs "test_shell.c")
endif()

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS "."
    REQUIRES unity zkernel zsys zshell zdevice
)
