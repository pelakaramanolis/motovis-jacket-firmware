idf_component_register(
    SRCS "crash_detect.c" "adxl345.c" "accel_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus esp_driver_i2c esp_timer
)