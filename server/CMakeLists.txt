cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE) 
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(server CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# Require -Wall cleanliness for all code, including SDK headers.
add_compile_options(-Wall -Werror)

add_executable(server
        server.cpp
        )

# Enable coroutines (GCC 10 requires a flag) and stricter warnings for our C++ code only.
set_source_files_properties(server.cpp
        PROPERTIES COMPILE_OPTIONS -fcoroutines -Wextra -pedantic)

# Make our lwipopts.h visible to lwip, which includes it.
target_include_directories(server PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        )

target_link_libraries(server
        pico_stdlib
        pico_cyw43_arch_lwip_poll
        pico_async_context_poll
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(server)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(server 1)
pico_enable_stdio_uart(server 0)
