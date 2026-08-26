idf_component_register(
    SRCS "src/hasp_http.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hasp_service
             hasp_wifi
             esp_http_server
             mbedtls
             nvs_flash
    PRIV_REQUIRES
            bblanchon__arduinojson
            esp_hw_support
)