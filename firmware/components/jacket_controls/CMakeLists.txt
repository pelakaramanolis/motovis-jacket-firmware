idf_component_register(
    SRCS "debounce.c" "jacket_controls.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer
)