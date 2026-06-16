zephyr_library()
zephyr_library_sources(src/battery_rgb.c)
# ZMK app headers (zmk/events/, zmk/rgb_underglow.h, etc.) no están en el
# path de módulos externos; CMAKE_SOURCE_DIR apunta a zmk/app en el build.
zephyr_include_directories(${CMAKE_SOURCE_DIR}/include)
