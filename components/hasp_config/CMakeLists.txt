idf_component_register(
    SRCS "src/hasp_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hasp_service
             hasp_fs
             bblanchon__arduinojson
)
