#ifndef DIO_H
#define DIO_H

#include <stdbool.h>
#include <stdint.h>

/* DIO0..DIO7 are PA0..PA7, brought out on J2 pins 2..9. */
#define DIO_COUNT 8

/* PWM frequency in millihertz, duty in hundredths of a percent (5000 = 50 %). */
#define DIO_PWM_MIN_MHZ 100ull          /* 0.1 Hz */
#define DIO_PWM_MAX_MHZ 32000000000ull  /* 32 MHz = timer clock / 2 */
#define DIO_PWM_DUTY_MAX 10000u

typedef enum { DIO_MODE_IN, DIO_MODE_OUT, DIO_MODE_OD, DIO_MODE_PWM } dio_mode_t;

typedef enum {
  DIO_OK,
  DIO_ERR_RANGE,    /* frequency or duty out of range */
  DIO_ERR_CONFLICT, /* timer is shared with another PWM pin running other settings */
} dio_err_t;

void dio_init(void); /* all pins to input, outputs latched low, PWM stopped */
void dio_set_mode(unsigned pin, dio_mode_t mode); /* IN, OUT or OD; use dio_set_pwm for PWM */
dio_mode_t dio_get_mode(unsigned pin);
void dio_write(unsigned pin, bool level);
bool dio_read(unsigned pin);    /* actual pin level */
uint8_t dio_read_port(void);    /* bit n = DIOn */

/* Hardware PWM from a timer channel. Pins on the same timer share one frequency:
 * DIO0+DIO1+DIO5 (TIM2, DIO0 and DIO5 also share the duty cycle), DIO2+DIO3 (TIM15).
 * DIO4, DIO6 and DIO7 each have their own timer. */
dio_err_t dio_set_pwm(unsigned pin, uint64_t freq_mhz, uint32_t duty_c100);

/* Frequency and duty actually generated (after timer rounding); 0,0 if not in PWM mode. */
void dio_get_pwm(unsigned pin, uint64_t *freq_mhz, uint32_t *duty_c100);

#endif
