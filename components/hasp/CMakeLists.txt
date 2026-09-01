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
    INCLUDE_DIRS "include"
    REQUIRES
        bblanchon__arduinojson
        lvgl__lvgl
        hasp_log
)
