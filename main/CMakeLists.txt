idf_component_register(
    SRCS
        "main.cpp"
        "board_lvgl.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_mm
        gen_bmgr_codes
        hasp
        hasp_fs
        hasp_ftp
        hasp_http
        hasp_log
        hasp_mqtt
        hasp_service
        hasp_wifi
        nvs_flash
        bblanchon__arduinojson
        espressif__esp_board_manager
        espressif__esp_lvgl_port
        lvgl__lvgl
)

# littlefs_create_partition_image(storage ${CMAKE_CURRENT_LIST_DIR}/../data/storage FLASH_IN_PROJECT)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-error=missing-field-initializers
    -Wno-error=stringop-truncation
)

idf_build_set_property(
    COMPILE_OPTIONS
    "-Wno-error=format-truncation;-Wno-error=stringop-truncation;-Wno-error=missing-field-initializers;-Wno-error=unused-variable"
    APPEND
)

# Strict compiler warnings for this component
idf_component_get_property(
    simple_ftp_lib
    cezap__simple_ftp_server
    COMPONENT_LIB
)

target_compile_options(${simple_ftp_lib} PRIVATE
    -Wno-error=format-truncation
    -Wno-error=stringop-truncation
)

message(STATUS "simple_ftp_server library = ${simple_ftp_lib}")

# Factory lvgl_demo_v9 pattern: LVGL's Kconfig does not propagate
# LV_USE_DEMO_* into C macros, so `lv_demos.h` conditional includes
# expand to nothing unless we define the flag ourselves on the LVGL lib.
if(CONFIG_LV_USE_DEMO_WIDGETS)
    idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
    target_compile_definitions(${LVGL_LIB} PUBLIC LV_USE_DEMO_WIDGETS=1)
endif()