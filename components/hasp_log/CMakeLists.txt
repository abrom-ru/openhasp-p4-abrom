idf_component_register(
    SRCS "src/hasp_log.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_driver_uart
        esp_driver_usb_serial_jtag
        log
)