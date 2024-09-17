cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board")
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(seven-segment CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(seven-segment seven-segment.cpp)

# Enable stricter warnings for our C++ code only.
set_source_files_properties(seven-segment.cpp
        PROPERTIES COMPILE_OPTIONS "-Werror")

target_link_libraries(seven-segment
        pico_stdlib
        hardware_gpio
        hardware_i2c
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(seven-segment)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(seven-segment 1)
pico_enable_stdio_uart(seven-segment 0)
