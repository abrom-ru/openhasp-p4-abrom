idf_component_register(
    SRCS "src/hasp_ftp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hasp_service
             hasp_wifi
             simple_ftp_server
             nvs_flash
    PRIV_REQUIRES bblanchon__arduinojson
)