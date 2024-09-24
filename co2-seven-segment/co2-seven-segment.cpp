#include <pico/async_context.h>
#include <pico/time.h>

#include <pico/types.h>
#include <picoro/coroutine.h>
#include <picoro/debug.h>
#include <picoro/drivers/sensirion/scd4x.h>
#include <picoro/event_loop.h>
#include <picoro/sleep.h>

#include <pico/async_context_poll.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include <hardware/i2c.h>
#include <hardware/gpio.h>

#include <tusb.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

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

  // Turn the decimal point LED at the specified `position` (0 to 3) on or off
  // (`value == true` for on, `false` for off).
  void decimal_point(unsigned position, bool value);

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
  digit(3, digit3);
}

inline
void SevenSegmentDisplay::decimal_point(unsigned position, bool value) {
  const int offset = position >= 2;
  position += offset;
  if (value) {
    buffer[1 + position * 2] |= 0x80;
  } else {
    buffer[1 + position * 2] &= 0x7F;
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

  // When you set the brightness to, say, 7, then the display shows "br 7".
  const std::uint8_t b = 0x7C;
  const std::uint8_t r = 0x50;
  number(magnitude, OMIT_LEADING_ZEROS);
  buffer[1 + 0 * 2] = b;
  buffer[1 + 1 * 2] = r;
  update();
}

picoro::Coroutine<bool> data_ready(const picoro::sensirion::SCD4x &sensor) {
  bool result;
  int rc = co_await sensor.get_data_ready_flag(&result);
  if (rc) {
    picoro::debug(
        "Unable to query whether the sensor has data ready. Error code %d.\n",
        rc);
    co_return false;
  }
  co_return result;
}

picoro::Coroutine<void> monitor_scd4x(
    async_context_t *ctx,
    const std::function<void(unsigned)>& show_number) {
  // I²C GPIO pins
  const uint sda_pin = 20;
  const uint scl_pin = 21;
  // I²C clock rate
  const uint clock_hz = 400 * 1000;

  i2c_inst_t *const instance = i2c0;
  const uint actual_baudrate = i2c_init(instance, clock_hz);
  printf("The actual I2C baudrate is %u Hz\n", actual_baudrate);
  gpio_set_function(sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(sda_pin);
  gpio_pull_up(scl_pin);

  picoro::sensirion::SCD4x sensor{ctx};
  sensor.device.instance = instance;

  int rc = co_await sensor.set_automatic_self_calibration(0);
  if (rc) {
    picoro::debug("Unable to disable automatic self-calibration. Error code %d.\n", rc);
  }

  rc = co_await sensor.start_periodic_measurement();
  if (rc) {
    picoro::debug("Unable to start periodic measurement mode. Error code %d.\n", rc);
  }

  for (;;) {
    co_await picoro::sleep_for(ctx, std::chrono::seconds(5));
    while (! co_await data_ready(sensor)) {
      co_await picoro::sleep_for(ctx, std::chrono::seconds(1));
    }

    uint16_t co2_ppm;
    int32_t temperature_millicelsius;
    int32_t relative_humidity_millipercent;
    rc = co_await sensor.read_measurement(&co2_ppm, &temperature_millicelsius, &relative_humidity_millipercent);
    if (rc) {
      picoro::debug("Unable to read sensor measurement. Error code %d.\n", rc);
    } else {
      std::printf("CO2: %hu ppm\ttemperature: %.1f C\thumidity: %.1f%%\n", co2_ppm, temperature_millicelsius / 1000.0f, relative_humidity_millipercent / 1000.0f);
      show_number(co2_ppm);
    }
  }
}

picoro::Coroutine<void> boing_boing(
    async_context_t *ctx,
    SevenSegmentDisplay& display,
    const bool& stop) {
  for (;;) {
    for (int i = 0; i < 4 && !stop; ++i) {
      display.clear();
      display.decimal_point(i, true);
      display.update();
      co_await picoro::sleep_for(ctx, std::chrono::milliseconds(100));
    }
    if (stop) {
      co_return;
    }
    display.clear();
    co_await picoro::sleep_for(ctx, std::chrono::milliseconds(400));
  }
}

/* TODO
struct Measurements {
  absolute_time_t first_measurement;
  std::vector<std::uint16_t> co2_ppm;
} measurements = {
  .first_measurement = nil_time,
  .co2_ppm = {}
};
*/

struct Button {
  async_when_pending_worker worker;
  async_context_t *ctx;
  std::uint8_t gpio;
  volatile int event_count;
  SevenSegmentDisplay *display;
  unsigned brightness;

  struct Event {
    absolute_time_t when;
    std::uint32_t mask;
  };

  Event events[8];
  absolute_time_t previous;
  volatile unsigned next_write;
  unsigned next_read;

  enum { up, down } state;

  static void do_work(async_context_t*, async_when_pending_worker_t*);
} button = {
  .worker = {
    .do_work = &Button::do_work,
    .work_pending = false,
    .user_data = nullptr, // will cast instead
  },
  .ctx = nullptr, // initialized in `main()`
  .gpio = 16,
  .event_count = 0,
  .display = nullptr, // initialized in `main()`
  .brightness = 0,
  .events = {},
  .previous = nil_time,
  .next_write = 0,
  .next_read = 0,
  .state = Button::up
};

void Button::do_work(async_context_t*, async_when_pending_worker_t* worker) {
  auto *button = reinterpret_cast<Button*>(worker);
  std::printf("Button event %d with masks:", button->event_count);
  absolute_time_t time = button->previous;
  for (
      int i = 0;
      button->next_read != button->next_write;
      button->next_read = (button->next_read + 1) % std::size(button->events), ++i) {
    auto new_time = button->events[button->next_read].when;
    int millis = absolute_time_diff_us(time, new_time) / 1000;
    auto mask = button->events[button->next_read].mask;
    if (is_nil_time(time) || millis > 50) {
      std::printf(" [%d %lu]", millis, mask);
      if (button->state == Button::up && (mask & 4)) {
        button->state = Button::down;
      } else if (button->state == Button::down && (mask & 8)) {
        button->state = Button::up;
        // Consider this a button press.
        std::printf("!%u", button->brightness);
        button->display->brightness(button->brightness);
        button->display->update();
        button->brightness = (button->brightness + 1) % 16;
      }
    }
    time = new_time;
  }
  button->previous = time;
  std::printf("\n");
}

void gpio_irq_handler(uint gpio, uint32_t event_mask) {
  static int event_count = 0;
  if (gpio != button.gpio) {
    return;
  }

  button.event_count = ++event_count;
  button.events[button.next_write] = Button::Event{
    .when = get_absolute_time(),
    .mask = event_mask
  };
  button.next_write = (button.next_write + 1) % std::size(button.events);

  async_context_set_work_pending(button.ctx, &button.worker);
}

int main() {
  stdio_init_all();

  // Make sure that GPIO 9 is in input mode (high impedance), because it's
  // shorted to 3.3V.
  gpio_init(9);
  gpio_set_dir(9, GPIO_IN);

  // I²C GPIO pins
  const uint sda_pin = 10;
  const uint scl_pin = 11;
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

  display.brightness(0);

  async_context_poll_t context = {};
  bool succeeded = async_context_poll_init_with_defaults(&context);
  if (!succeeded) {
    panic("Failed to initialize async_context_poll_t\n");
  }
  async_context_t *const ctx = &context.core;

  // Do some WiFi chip setup here, just so that we can use the LED.
  cyw43_arch_set_async_context(ctx);
  // 🇺🇸 🦅
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
    panic("failed to initialize WiFi\n");
  }

  // Set up the button.
  button.display = &display;
  button.ctx = ctx;
  async_context_add_when_pending_worker(ctx, &button.worker);
  gpio_set_dir(button.gpio, GPIO_IN);
  gpio_pull_up(button.gpio);
  const bool enabled = true;
  const uint32_t event_mask = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
  gpio_set_irq_enabled_with_callback (button.gpio, event_mask, enabled, gpio_irq_handler);

  bool stop_boing_boing = false;

  run_event_loop(ctx,
    boing_boing(ctx, display, stop_boing_boing),
    monitor_scd4x(ctx, [&](unsigned co2_ppm) {
      stop_boing_boing = true;
      display.number(co2_ppm, SevenSegmentDisplay::OMIT_LEADING_ZEROS);
      display.update();
    }));

  // unreachable
  cyw43_arch_deinit();
  async_context_deinit(ctx);
  i2c_deinit(instance);
}
