idf_component_register(
    SRCS "src/hasp_service.cpp" "src/hasp_service_manager.cpp" "src/hasp_event.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
             esp_event
             bblanchon__arduinojson
)