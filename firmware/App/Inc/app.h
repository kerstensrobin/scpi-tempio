#ifndef APP_H
#define APP_H

#define APP_LINE_MAX 128 /* longest accepted command line */
#define APP_RESP_MAX 256 /* longest response line */

/* Call once from main() after the CubeMX MX_*_Init() calls. */
void app_init(void);

/* Call continuously from the main while(1) loop. */
void app_loop(void);

/* 24-char hex string built from the MCU's 96-bit unique ID. */
const char *app_serial(void);

/* Short LED off-flick to show a command was received. */
void app_activity(void);

/* Fast LED blink for 2 s, to find the unit on the bench (USBTMC indicator pulse). */
void app_identify(void);

#endif
