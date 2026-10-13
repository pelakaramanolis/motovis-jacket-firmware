idf_component_register(
    SRCS "ble_uuids.c" "ble_server.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash
)
