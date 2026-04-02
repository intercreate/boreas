idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer log esp_system nvs_flash
)
