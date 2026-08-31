idf_component_register(
    SRCS "src/hasp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bblanchon__arduinojson
        lvgl__lvgl
        hasp_log
)
