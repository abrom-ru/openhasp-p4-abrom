# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sample_project)

# 3h-2: FreeType glue. The managed lvgl__lvgl component compiles
# src/libs/freetype/*.c unconditionally when CONFIG_LV_USE_FREETYPE=y,
# but its esp.cmake does not add espressif__freetype to PRIV_REQUIRES,
# so freetype headers and the library itself are not visible to LVGL.
# Inject the link+include propagation here (post-registration).
if(CONFIG_LV_USE_FREETYPE)
    idf_component_get_property(lvgl_lib   lvgl__lvgl          COMPONENT_LIB)
    idf_component_get_property(ft_lib     espressif__freetype COMPONENT_LIB)
    if(lvgl_lib AND ft_lib)
        target_link_libraries(${lvgl_lib} PUBLIC ${ft_lib})
    endif()
endif()
