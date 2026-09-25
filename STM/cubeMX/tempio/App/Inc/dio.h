#ifndef DIO_H
#define DIO_H

#include <stdbool.h>
#include <stdint.h>

/* DIO0..DIO7 are PA0..PA7, brought out on J2 pins 2..9. */
#define DIO_COUNT 8

typedef enum { DIO_MODE_IN, DIO_MODE_OUT, DIO_MODE_OD } dio_mode_t;

void dio_init(void); /* all pins to input, outputs latched low */
void dio_set_mode(unsigned pin, dio_mode_t mode);
dio_mode_t dio_get_mode(unsigned pin);
void dio_write(unsigned pin, bool level);
bool dio_read(unsigned pin);    /* actual pin level */
uint8_t dio_read_port(void);    /* bit n = DIOn */

#endif
