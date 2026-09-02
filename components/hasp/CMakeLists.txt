idf_component_register(
    SRCS
        "src/hasp.cpp"
        "src/hasp_object.cpp"
        "src/hasp_event.cpp"
        "src/hasp_parser.cpp"
        "src/hasp_page.cpp"
        "src/hasp_dispatch.cpp"
        "src/hasp_attribute.cpp"
        "src/hasp_font.cpp"
        "src/hasp_theme.cpp"
        "src/hasp_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bblanchon__arduinojson
        lvgl__lvgl
        hasp_log
        hasp_mqtt
        hasp_service
        esp_timer    # step 7B: uptime for statusupdate
        esp_netif    # step 7B: hostname + ip for statusupdate
        esp_wifi     # step 7B: rssi/ssid for statusupdate
)
