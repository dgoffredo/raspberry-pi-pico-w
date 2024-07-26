cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE)
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(dht22 CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(dht22
        dht.cpp
        )

# Enable coroutines (GCC 10 requires a flag) and stricter warnings for our C++ code only.
set_source_files_properties(dht.cpp dht.h
        PROPERTIES COMPILE_OPTIONS -fcoroutines -Wextra -pedantic)

target_link_libraries(dht22
        pico_stdlib
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(dht22)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(dht22 1)
pico_enable_stdio_uart(dht22 0)
