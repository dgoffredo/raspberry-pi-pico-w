#include <algorithm>
#include <cstdint>

// #include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <pico/stdlib.h>
#include <tusb.h>

const std::uint32_t sequence[] = {
  1,
  1,
  2,
  4,
  9,
  21,
  51,
  127,
  323,
  835,
  2'188,
  5'798,
  15'511,
  41'835,
  113'634,
  310'572,
  853'467,
  2'356'779,
  6'536'382,
  18'199'284,
  50'852'019,
  142'547'559,
  400'763'223,
  1'129'760'415,
  3'192'727'797
};

int main() {
  stdio_init_all();

  // Wait for the host to attach to the USB terminal (i.e. ttyACM0).
  // Give up after 10 seconds.
  for (int i = 0; i < 10 && !tud_cdc_connected(); ++i) {
    sleep_ms(1000);
  }

  const auto flash_begin = reinterpret_cast<const std::uint32_t*>(XIP_BASE);
  const auto one_meg_later = flash_begin + (1 << 17);
  auto searcher = std::boyer_moore_horspool_searcher(std::begin(sequence), std::end(sequence));
  auto found = std::search(flash_begin, one_meg_later, searcher);
  if (found == one_meg_later) {
    printf("Sequence not found.\n");
  } else {
    printf("Sequence found %d bytes from the beginning of flash.\n", (found - flash_begin)*4);
  }

  auto main_addr = reinterpret_cast<const char*>(&main);
  auto flash_addr = reinterpret_cast<const char *>(flash_begin);
  printf("For comparison, &main is %d bytes from the beginning of flash.\n", main_addr - flash_addr);
}
