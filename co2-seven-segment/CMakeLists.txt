cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board")
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(co2-seven-segment CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(co2-seven-segment co2-seven-segment.cpp)

# Make our lwipopts.h visible to lwIP, which includes it.
target_include_directories(co2-seven-segment PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lwipopts
        )

# Enable stricter warnings for our C++ code only.
set_source_files_properties(co2-seven-segment.cpp
        PROPERTIES COMPILE_OPTIONS "-Werror")

add_subdirectory(picoro)

target_link_libraries(co2-seven-segment
        picoro_coroutine
        picoro_debug
        picoro_drivers_sensirion_scd4x
        picoro_event_loop
        picoro_sleep

        pico_cyw43_arch_lwip_poll
        pico_stdlib
        pico_time

        hardware_gpio
        hardware_i2c
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(co2-seven-segment)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(co2-seven-segment 1)
pico_enable_stdio_uart(co2-seven-segment 0)
