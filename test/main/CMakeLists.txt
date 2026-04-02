idf_component_register(
    SRCS
        "test_main.c"
        "test_timeout.c"
        "test_slist.c"
        "test_dlist.c"
        "test_k_sem.c"
        "test_k_mutex.c"
        "test_k_msgq.c"
        "test_k_event.c"
        "test_k_timer.c"
        "test_k_work.c"
        "test_retry.c"
    INCLUDE_DIRS "."
    REQUIRES unity zkernel zsys
)
