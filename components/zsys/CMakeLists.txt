idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include/boreas" "include"
    REQUIRES zkernel freertos esp_system log
    LDFRAGMENTS "zsys.lf"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
