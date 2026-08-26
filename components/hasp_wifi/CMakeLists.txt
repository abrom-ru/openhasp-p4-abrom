idf_component_register(
    SRCS "src/hasp_wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hasp_service
             esp_wifi
             esp_event
             esp_netif
             nvs_flash
    PRIV_REQUIRES bblanchon__arduinojson
)