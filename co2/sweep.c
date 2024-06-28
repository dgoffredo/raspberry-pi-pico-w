// Sweep through all 7-bit I2C addresses, to see if any slaves are present on
// the I2C bus. Print out a table of present addresses.

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"

#include <stdio.h>

static void pulse_led(uint32_t ms) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    sleep_ms(ms);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

// I2C reserves some addresses for special purposes. Exclude these from the
// scan. These are any addresses of the form 000 0xxx or 111 1xxx.
static bool reserved_addr(uint8_t addr) {
  return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

int main(void) {
  stdio_init_all();
  if (cyw43_arch_init()) {
      printf("Wi-Fi init failed\n");
      return -1;
  }

  const uint i2c_sda_pin = 12;
  const uint i2c_scl_pin = 13;
  const uint desired_baudrate = 200 * 1000;
  const uint actual_baudrate = i2c_init(i2c0, desired_baudrate);
  (void)actual_baudrate;
  // printf("Actual I2C bus baudrate is %u\n", actual_baudrate);
  gpio_set_function(i2c_sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(i2c_scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(i2c_sda_pin);
  gpio_pull_up(i2c_scl_pin);
  // Make the I2C pins available to picotool
  bi_decl(bi_2pins_with_func(i2c_sda_pin, i2c_scl_pin, GPIO_FUNC_I2C));
  
  for (int i = 1; i <= 5; ++i) {
      printf("%d: Hello! I'm going to sleep for two seconds, five times.\n", i);
      pulse_led(1000);
      sleep_ms(1000);
  }

  printf("\nI2C Bus Scan\n");
  printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

  for (int addr = 0; addr < (1 << 7); ++addr) {
    if (addr % 16 == 0) {
      printf("%02x ", addr);
    }
    
    // Perform a 1-byte dummy read from the probe address. If a slave
    // acknowledges this address, then the function returns the number of
    // bytes transferred. If the address byte is ignored, then the function
    // returns -1.

    int ret;
    uint8_t rxdata;
    if (reserved_addr(addr)) {
      ret = PICO_ERROR_GENERIC;
    } else {
      ret = i2c_read_blocking(i2c0, addr, &rxdata, 1, false);
    }
    printf(ret < 0 ? "." : "@");
    printf(addr % 16 == 15 ? "\n" : "  ");
  }
  printf("\n\nAll done.\n");
}

/*
The micropython scan code using a length-zero write, as opposed to a length-one read.
Problem is, the pico's I2C hardware apparently doesn't allow length-zero writes.
So, it's done in software.
I'll give up on this for now.

// return value:
//  >=0 - success; for read it's 0, for write it's number of acks received
//   <0 - error, with errno being the negative of the return value
int mp_machine_soft_i2c_transfer(mp_obj_base_t *self_in, uint16_t addr, size_t n, mp_machine_i2c_buf_t *bufs, unsigned int flags) {
    machine_i2c_obj_t *self = (machine_i2c_obj_t *)self_in;

    // start the I2C transaction
    int ret = mp_hal_i2c_start(self);
    if (ret != 0) {
        return ret;
    }

    // write the slave address
    ret = mp_hal_i2c_write_byte(self, (addr << 1) | (flags & MP_MACHINE_I2C_FLAG_READ));
    if (ret < 0) {
        return ret;
    } else if (ret != 0) {
        // nack received, release the bus cleanly
        mp_hal_i2c_stop(self);
        return -MP_ENODEV;
    }

    int transfer_ret = 0;
    // REDACTED (because n is 0)

    // finish the I2C transaction
    if (flags & MP_MACHINE_I2C_FLAG_STOP) {
        ret = mp_hal_i2c_stop(self);
        if (ret != 0) {
            return ret;
        }
    }

    return transfer_ret;
}

*/
