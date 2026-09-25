#ifndef SHT4X_H
#define SHT4X_H

#include <stdbool.h>
#include <stdint.h>

/* One high-precision measurement (~10 ms, blocking).
 * Results in hundredths: temp_c100 = 2341 means 23.41 degC, rh_c100 = 4572 means 45.72 %RH. */
bool sht4x_measure(int32_t *temp_c100, int32_t *rh_c100);

/* Returns true if the sensor ACKs its address. */
bool sht4x_present(void);

#endif
