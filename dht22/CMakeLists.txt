cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board")
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(dht22 CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(dht22 dht22.cpp)

# Enable stricter warnings for our C++ code only.
set_source_files_properties(dht22.cpp
        PROPERTIES COMPILE_OPTIONS "-Wextra")

# Make our lwipopts.h visible to lwip, which includes it.
target_include_directories(dht22 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lwipopts
        )

add_subdirectory(picoro)

target_link_libraries(dht22
        picoro_coroutine
        picoro_drivers_dht22
        picoro_drivers_sensirion_sht3x
        picoro_event_loop
        picoro_sleep
        picoro_tcp

        pico_stdlib

        hardware_watchdog
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(dht22)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(dht22 1)
pico_enable_stdio_uart(dht22 0)
