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

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES driver zkernel
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
