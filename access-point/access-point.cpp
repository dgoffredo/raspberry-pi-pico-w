#include <hardware/gpio.h>

#include <pico/async_context.h>
#include <pico/async_context_poll.h>
#include <pico/cyw43_arch.h>
#include <pico/stdio.h>

#include <picoro/event_loop.h>

#include <tusb.h>

struct What {
  uint gpio;
  uint32_t event_mask;
};

static void button_worker_func(async_context_t*, async_when_pending_worker_t *worker) {
  What& what = *static_cast<What*>(worker->user_data);
  printf("GPIO %u triggered event mask %lu\n", what.gpio, what.event_mask);
}

static What what;

static async_when_pending_worker_t button_worker = {
  .do_work = button_worker_func,
  .user_data = &what
};

static void gpio_irq_handler(uint gpio, uint32_t event_mask) {
  what.gpio = gpio;
  what.event_mask = event_mask;
  button_worker.work_pending = true;
}

int main() {
  stdio_init_all();

  // Wait for the host to attach to the USB terminal (e.g. ttyACM0).
  while (!tud_cdc_connected()) {
    printf(".");
    sleep_ms(1000);
  }
  printf(" hai\n");

  const uint button_gpio = 16;
  gpio_set_dir(button_gpio, GPIO_IN);
  gpio_pull_up(button_gpio);
  const bool enabled = true;
  const uint32_t event_mask = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
  gpio_set_irq_enabled_with_callback (button_gpio, event_mask, enabled, gpio_irq_handler);

  async_context_poll_t context;
  const bool ok = async_context_poll_init_with_defaults(&context);
  if (!ok) {
    panic("AAAUUGGHHH!");
  }
  async_context_t *const ctx = &context.core;

  cyw43_arch_set_async_context(ctx);
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) { // 🇺🇸 🦅
      printf("failed to initialize WiFi\n");
      return 1;
  }

  async_context_add_when_pending_worker(ctx, &button_worker);
  picoro::run_event_loop(ctx);

  // unreachable
  cyw43_arch_deinit();
  async_context_deinit(ctx);
}
