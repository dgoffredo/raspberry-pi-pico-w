cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD "pico2_w" CACHE STRING "The particular Raspberry Pi Pico board" FORCE)
set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

# SDK initialization functions must be defined before project().
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

# Make sure that "ASM" is an argument to project().
# Otherwise, CMAKE_ASM_COMPILE_OBJECT will not be set and the build will fail.
project(pico2w-server CXX C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

add_compile_options(-Wall -Werror)

add_executable(pico2w-server server.cpp)

# Make our lwipopts.h visible to lwIP, which includes it.
target_include_directories(pico2w-server PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lwipopts
        )

add_subdirectory(picoro)

target_link_libraries(pico2w-server
        picoro_broadcaster
        picoro_coroutine
        picoro_debug
        picoro_event_loop
        picoro_sleep
        picoro_tcp

        pico_stdlib
        pico_time

        hardware_watchdog
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(pico2w-server)

# map stdio to the USB CDC, not to UART
pico_enable_stdio_usb(pico2w-server 1)
pico_enable_stdio_uart(pico2w-server 0)
