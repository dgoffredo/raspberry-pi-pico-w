cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico2_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE) 
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

# SDK initialization functions must be defined before project().
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

# Make sure that "ASM" is an argument to project().
# Otherwise, CMAKE_ASM_COMPILE_OBJECT will not be set and the build will fail.
project(pico2w-blink CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

add_compile_options(-Wall -Werror)

add_executable(pico2w-blink blink.cpp)

# Make our lwipopts.h visible to lwIP, which includes it.
target_include_directories(pico2w-blink PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lwipopts
        )

target_link_libraries(pico2w-blink
        pico_stdlib
        # we need wifi in order to access the GPIO for the built-in LED
        # pico_cyw43_arch_none
        # pico_cyw43_arch_lwip_poll
        pico_cyw43_arch_lwip_threadsafe_background
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(pico2w-blink)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(pico2w-blink 1)
pico_enable_stdio_uart(pico2w-blink 0)
