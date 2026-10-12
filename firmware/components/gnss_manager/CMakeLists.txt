idf_component_register(
    SRCS "nmea.c" "gnss_uart.c" "gnss_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_timer
)