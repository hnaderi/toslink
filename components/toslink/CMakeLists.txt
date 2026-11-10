idf_component_register(
    SRCS "toslink_proto.c" "toslink_tx.c" "toslink_rx.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_driver_ledc esp_driver_gpio
    PRIV_REQUIRES esp_timer esp_rom log
)
