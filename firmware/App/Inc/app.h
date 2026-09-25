#ifndef APP_H
#define APP_H

/* Call once from main() after the CubeMX MX_*_Init() calls. */
void app_init(void);

/* Call continuously from the main while(1) loop. */
void app_loop(void);

/* 24-char hex string built from the MCU's 96-bit unique ID. */
const char *app_serial(void);

#endif
