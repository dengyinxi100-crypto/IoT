# IotGateway - 银发智慧居家物联网网关
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format")
project(IotGateway)

# 修复 GCC 12.2.0 ICE: esp_lcd 在 -Og 下编译会 Segmentation fault
# 改用 -O0 优化级别避免编译器崩溃
if(CONFIG_COMPILER_OPTIMIZATION_DEFAULT)
  set_source_files_properties(
    "${IDF_PATH}/components/esp_lcd/src/esp_lcd_panel_rgb.c"
    PROPERTIES COMPILE_FLAGS "-O0"
  )
endif()
