idf_component_register(
    SRCS "src/hasp_mqtt.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hasp_service
             mqtt
             nvs_flash
    PRIV_REQUIRES bblanchon__arduinojson
)