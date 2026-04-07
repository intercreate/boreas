set(srcs "src/device_model.c")

if(CONFIG_DEVICE_MODEL_I2C)
    list(APPEND srcs "src/i2c_dt.c")
endif()

if(CONFIG_DEVICE_MODEL_SPI)
    list(APPEND srcs "src/spi_dt.c")
endif()

if(CONFIG_DEVICE_MODEL_GPIO)
    list(APPEND srcs "src/gpio_dt.c")
endif()

set(requires zkernel zsys)

idf_build_get_property(target IDF_TARGET)
if(NOT "${target}" STREQUAL "linux")
    list(APPEND requires driver)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include/boreas" "include"
    REQUIRES ${requires}
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
