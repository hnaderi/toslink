idf_component_register(
    SRCS "src/toslink_proto.c" "src/toslink_tx.c" "src/toslink_rx.c" "src/toslink_uart.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_uart esp_driver_gpio freertos
    PRIV_REQUIRES log
)
