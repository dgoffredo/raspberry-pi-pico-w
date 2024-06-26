cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE) 
set(PICO_SDK_PATH "/home/david/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(co2 CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_compile_options(-Wall -Werror)

add_executable(co2
        co2.c
        ../embedded-i2c-scd4x/sensirion_i2c.c
        ../embedded-i2c-scd4x/sensirion_common.c
        ../embedded-i2c-scd4x/scd4x_i2c.c
        ../embedded-i2c-scd4x/sample-implementations/RaspberryPi_Pico/sensirion_i2c_hal.c
        )

target_include_directories(co2 PRIVATE ../embedded-i2c-scd4x)

target_link_libraries(co2
        pico_stdlib
        # we need wifi in order to access the GPIO for the built-in LED
        pico_cyw43_arch_none
        hardware_i2c
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(co2)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(co2 1)
pico_enable_stdio_uart(co2 0)
