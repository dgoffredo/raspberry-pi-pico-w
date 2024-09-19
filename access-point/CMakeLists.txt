cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE)
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(access-point CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(access-point
        access-point.cpp
        )

# Enable coroutines (GCC 10 requires a flag) and stricter warnings for our C++ code only.
set_source_files_properties(access-point.cpp lwipopts.h
        PROPERTIES COMPILE_OPTIONS -fcoroutines -Wextra -pedantic)

# Make our lwipopts.h visible to lwip, which includes it.
target_include_directories(access-point PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lwipopts
        )

add_subdirectory(picoro)

target_link_libraries(access-point
        hardware_gpio

        pico_async_context_poll
        pico_cyw43_arch_lwip_poll
        pico_stdlib

        picoro_event_loop
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(access-point)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(access-point 1)
pico_enable_stdio_uart(access-point 0)
