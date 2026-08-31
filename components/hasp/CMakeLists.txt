idf_component_register(
    SRCS
        "src/hasp.cpp"
        "src/hasp_object.cpp"
        "src/hasp_event.cpp"
        "src/hasp_parser.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bblanchon__arduinojson
        lvgl__lvgl
        hasp_log
)
