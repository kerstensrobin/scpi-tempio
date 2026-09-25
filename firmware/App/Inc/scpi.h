#ifndef SCPI_H
#define SCPI_H

#include <stddef.h>

/* Clears the error queue and puts the device in its power-on state (*RST). */
void scpi_reset(void);

/* Executes one received line (without terminator). Commands may be chained with ';'.
 * Query responses are joined with ';' into out; out is "" when there is nothing to send. */
void scpi_process_line(char *line, char *out, size_t out_size);

/* Queue an SCPI error, e.g. scpi_push_error(-363, "Input buffer overrun"). */
void scpi_push_error(int code, const char *msg);

/* Number of errors in the queue. */
unsigned scpi_error_count(void);

#endif
