#include <stdio.h>
#include <string.h>

#include "main.h"
#include "tusb.h"

#include "app.h"
#include "scpi.h"

#define LED_PORT GPIOA
#define LED_PIN  GPIO_PIN_15

#define LINE_MAX 128

void usb_hw_init(void);

static char line[LINE_MAX];
static size_t line_len;
static bool line_overflow;
static uint32_t led_off_until;

const char *app_serial(void)
{
  static char serial[25];
  if (!serial[0]) {
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    snprintf(serial, sizeof serial, "%08lX%08lX%08lX",
             (unsigned long)uid[2], (unsigned long)uid[1], (unsigned long)uid[0]);
  }
  return serial;
}

void app_init(void)
{
  scpi_reset();
  usb_hw_init();
  const tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
}

static void cdc_write(const char *s)
{
  size_t n = strlen(s);
  while (n) {
    uint32_t w = tud_cdc_write(s, n);
    s += w;
    n -= w;
    if (n) {
      tud_task();
      if (!tud_cdc_connected()) return;
    }
  }
}

static void handle_line(void)
{
  static char out[256];
  line[line_len] = '\0';
  scpi_process_line(line, out, sizeof out);
  if (out[0]) {
    cdc_write(out);
    cdc_write("\n");
    tud_cdc_write_flush();
  }
  led_off_until = HAL_GetTick() + 30; /* activity blink */
}

static void poll_cdc(void)
{
  while (tud_cdc_available()) {
    char c = (char)tud_cdc_read_char();
    if (c == '\n' || c == '\r') {
      if (line_overflow)
        scpi_push_error(-363, "Input buffer overrun");
      else if (line_len)
        handle_line();
      line_len = 0;
      line_overflow = false;
    } else if (line_len < LINE_MAX - 1) {
      line[line_len++] = c;
    } else {
      line_overflow = true;
    }
  }
}

/* Not enumerated: blink at 2 Hz. Enumerated: on, with a short off-flick per command. */
static void update_led(void)
{
  uint32_t now = HAL_GetTick();
  bool on;
  if (!tud_mounted())
    on = (now / 250) & 1;
  else
    on = (int32_t)(now - led_off_until) >= 0;
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void app_loop(void)
{
  tud_task();
  poll_cdc();
  update_led();
}
