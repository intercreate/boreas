set(test_srcs
    "test_main.c"
    "test_timeout.c"
    "test_slist.c"
    "test_dlist.c"
    "test_k_sem.c"
    "test_k_mutex.c"
    "test_k_msgq.c"
    "test_k_event.c"
    "test_retry.c"
)

idf_build_get_property(target IDF_TARGET)

if(NOT "${target}" STREQUAL "linux")
    list(APPEND test_srcs "test_k_timer.c" "test_k_work.c")
endif()

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS "."
    REQUIRES unity zkernel zsys
)
