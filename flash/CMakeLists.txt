cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board")
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(flash CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(flash flash.cpp)

# Enable stricter warnings for our C++ code only.
set_source_files_properties(flash.cpp
        PROPERTIES COMPILE_OPTIONS "-Wextra")

target_link_libraries(flash
        pico_stdlib
        # hardware_flash
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(flash)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(flash 1)
pico_enable_stdio_uart(flash 0)
