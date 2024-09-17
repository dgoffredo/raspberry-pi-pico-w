#include <hardware/i2c.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>

#include <tusb.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>

class SevenSegmentDisplay {
  // digits 0 - F
  static constexpr std::uint8_t font[] = {
    0x3F, 0x06, 0x5B, 0x4F,
    0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C,
    0x39, 0x5E, 0x79, 0x71
  };

  i2c_inst_t *const instance;
  const std::uint8_t address;
  const std::chrono::microseconds write_timeout;
  std::uint8_t buffer[17];

  void write(const std::uint8_t *data, int length);
  void digit(unsigned position, unsigned value);

 public:
  struct Config {
    i2c_inst_t *i2c_instance;
    std::uint8_t scl_gpio;
    std::uint8_t sda_gpio;
    std::uint8_t i2c_address = 0x70;
    std::chrono::microseconds i2c_write_timeout = std::chrono::microseconds(1000);
  };

  SevenSegmentDisplay(const Config&);

  // Remove digits from the display and update the display (so that it shows
  // nothing).
  void clear();

  enum LeadingZeros {
    SHOW_LEADING_ZEROS,
    OMIT_LEADING_ZEROS
  };
  // Store the specified `number` for display. Include or exclude leading zeros
  // per the specified `policy`. The number will not display until `update()`
  // is called.
  void number(unsigned number, LeadingZeros policy);

  // Display the most recently stored number.
  void update();

  // Set the display brightness to the specified `magnitude`, which is at most
  // 15.
  void brightness(unsigned magnitude);
};

inline
void SevenSegmentDisplay::write(const std::uint8_t *data, int length) {
  const bool nostop = true;  // retain control of the bus after the write
  const unsigned timeout_μs = write_timeout / std::chrono::microseconds(1);
  const int rc = i2c_write_timeout_us(
    instance,
    address,
    data,
    length,
    nostop,
    timeout_μs);
  (void)rc;
}

inline
void SevenSegmentDisplay::digit(unsigned position, unsigned value) {
  const int offset = position >= 2;
  position += offset;
  buffer[1 + position * 2] = font[value] & 0xFF;
}

inline
SevenSegmentDisplay::SevenSegmentDisplay(const Config& config)
: instance(config.i2c_instance)
, address(config.i2c_address)
, write_timeout(config.i2c_write_timeout) {
  std::fill(std::begin(buffer), std::end(buffer), 0);
  const std::uint8_t setup = 0x21;
  write(&setup, 1);
  // The `0` is the blink rate.
  const std::uint8_t no_blinking = 0x80 | 1 | (0 << 1);
  write(&no_blinking, 1);
  brightness(15);
  update();
}

inline
void SevenSegmentDisplay::clear() {
  std::fill(std::begin(buffer), std::end(buffer), 0);
  update();
}

inline
void SevenSegmentDisplay::number(unsigned number, SevenSegmentDisplay::LeadingZeros policy) {
  if (number > 9999) {
    panic("%u won't fit on the display; the display is four digits.", number);
  }
  const unsigned digit3 = number % 10;
  const unsigned digit2 = number / 10 % 10;
  const unsigned digit1 = number / 100 % 10;
  const unsigned digit0 = number / 1000 % 10;

  std::fill(std::begin(buffer), std::end(buffer), 0);
  const bool show = policy == SevenSegmentDisplay::SHOW_LEADING_ZEROS;
  if (show || digit0) {
    digit(0, digit0);
  }
  if (show || digit0 + digit1) {
    digit(1, digit1);
  }
  if (show || digit0 + digit1 + digit2) {
    digit(2, digit2);
  }
  if (show || digit0 + digit1 + digit2 + digit3) {
    digit(3, digit3);
  }
}

inline
void SevenSegmentDisplay::update() {
  write(buffer, sizeof buffer);
}

inline
void SevenSegmentDisplay::brightness(unsigned magnitude) {
  constexpr std::uint8_t brightness_command = 0xE0;
  std::uint8_t data = brightness_command | magnitude;
  write(&data, 1);
}

int main() {
  stdio_init_all();

  // Wait for the host to attach to the USB terminal (i.e. ttyACM0).
  // Give up after 10 seconds.
  for (int i = 0; i < 10 && !tud_cdc_connected(); ++i) {
    sleep_ms(1000);
  }
  if (tud_cdc_connected()) {
    std::printf("Welcome!\n");
  }

  // I²C GPIO pins
  const uint sda_pin = 26;
  const uint scl_pin = 27;
  // I²C clock rate
  const uint clock_hz = 400 * 1000;

  i2c_inst_t *const instance = i2c1;
  const uint actual_baudrate = i2c_init(instance, clock_hz);
  std::printf("The actual I2C baudrate is %u Hz\n", actual_baudrate);
  gpio_set_function(sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(sda_pin);
  gpio_pull_up(scl_pin);

  SevenSegmentDisplay display(SevenSegmentDisplay::Config{
    .i2c_instance = instance,
    .scl_gpio = scl_pin,
    .sda_gpio = sda_pin
  });

  for (int i = 0; i < 10'000; ++i) {
    display.number(i, SevenSegmentDisplay::OMIT_LEADING_ZEROS);
    display.update();
    sleep_ms(100);
  }
}
