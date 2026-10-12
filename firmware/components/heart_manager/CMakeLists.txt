idf_component_register(
    SRCS "pulse_detect.c" "heart_adc.c" "heart_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_timer
)