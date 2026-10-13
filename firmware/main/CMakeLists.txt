idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio event_bus battery_manager accel_manager gnss_manager heart_manager jacket_controls
)