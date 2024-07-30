add_library(dht_pio INTERFACE)

pico_generate_pio_header(dht_pio ${CMAKE_CURRENT_LIST_DIR}/dht.pio)

target_include_directories(dht_pio
    INTERFACE
    ./include)

target_sources(dht_pio
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/dht.c
)

target_link_libraries(dht_pio
    INTERFACE
    hardware_clocks
    hardware_dma
    hardware_pio
)
