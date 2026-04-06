idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES zkernel freertos esp_system log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
