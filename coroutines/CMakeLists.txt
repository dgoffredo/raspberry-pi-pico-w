cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE) 
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(coroutines CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(coroutines
        coroutines.cpp
        )

# Enable coroutines (GCC 10 requires a flag) and stricter warnings for our C++ code only.
set_source_files_properties(coroutines.cpp picoro.h scd4x.h
        PROPERTIES COMPILE_OPTIONS -fcoroutines -Wextra -pedantic)

target_link_libraries(coroutines
        pico_stdlib
        # we need wifi in order to access the GPIO for the built-in LED
        pico_cyw43_arch_none
        hardware_i2c
        pico_async_context_poll
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(coroutines)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(coroutines 1)
pico_enable_stdio_uart(coroutines 0)
