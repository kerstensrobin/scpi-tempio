/* Minimal SCPI parser.
 *
 * Headers are matched case-insensitively in short or long form (MEAS / MEASure).
 * A '#' in a pattern node takes a numeric suffix (DIGital:PIN# matches DIG:PIN3).
 * Each ';'-separated command is parsed from the root; relative paths after ';'
 * (MEAS:TEMP?;HUM?) are not supported, write MEAS:TEMP?;MEAS:HUM? instead. */
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scpi.h"
#include "app.h"
#include "dio.h"
#include "sht4x.h"
#include "version.h"

/* ---------- error queue ---------- */

#define ERR_QUEUE_LEN 8

typedef struct {
  int code;
  const char *msg;
} scpi_err_t;

static scpi_err_t err_queue[ERR_QUEUE_LEN];
static unsigned err_count;

void scpi_push_error(int code, const char *msg)
{
  if (err_count < ERR_QUEUE_LEN) {
    err_queue[err_count++] = (scpi_err_t){code, msg};
  } else {
    err_queue[ERR_QUEUE_LEN - 1] = (scpi_err_t){-350, "Queue overflow"};
  }
}

static scpi_err_t pop_error(void)
{
  if (err_count == 0) return (scpi_err_t){0, "No error"};
  scpi_err_t e = err_queue[0];
  memmove(&err_queue[0], &err_queue[1], (--err_count) * sizeof err_queue[0]);
  return e;
}

/* ---------- command context ---------- */

typedef struct {
  int suffix;        /* numeric suffix of the header, -1 if none */
  const char *param; /* parameter text, "" if none */
  char *out;         /* response for this command */
  size_t out_size;
} cmd_t;

__attribute__((format(printf, 2, 3)))
static void reply(cmd_t *c, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(c->out, c->out_size, fmt, ap);
  va_end(ap);
}

static bool no_param(cmd_t *c)
{
  if (c->param[0]) {
    scpi_push_error(-108, "Parameter not allowed");
    return false;
  }
  return true;
}

static bool need_param(cmd_t *c)
{
  if (!c->param[0]) {
    scpi_push_error(-109, "Missing parameter");
    return false;
  }
  return true;
}

static bool pin_suffix(cmd_t *c)
{
  if (c->suffix < 0 || c->suffix >= DIO_COUNT) {
    scpi_push_error(-114, "Header suffix out of range");
    return false;
  }
  return true;
}

static bool streq_ci(const char *a, const char *b)
{
  while (*a && *b) {
    if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return false;
  }
  return *a == *b;
}

/* Hundredths to text without printf float support: 2341 -> "23.41", -105 -> "-1.05". */
static void fmt_c100(char *buf, size_t n, int32_t v)
{
  const char *sign = v < 0 ? "-" : "";
  if (v < 0) v = -v;
  snprintf(buf, n, "%s%ld.%02ld", sign, (long)(v / 100), (long)(v % 100));
}

#define SCPI_NAN "9.91E37"

/* ---------- handlers ---------- */

static void cmd_idn(cmd_t *c)
{
  if (no_param(c)) reply(c, DEV_MANUFACTURER "," DEV_MODEL ",%s," FW_VERSION, app_serial());
}

static void cmd_rst(cmd_t *c)
{
  if (no_param(c)) {
    dio_init();
  }
}

static void cmd_cls(cmd_t *c)
{
  if (no_param(c)) err_count = 0;
}

static void cmd_opc(cmd_t *c)
{
  if (no_param(c)) reply(c, "1");
}

static void cmd_tst(cmd_t *c)
{
  if (no_param(c)) reply(c, "%d", sht4x_present() ? 0 : 1);
}

static void cmd_err(cmd_t *c)
{
  if (!no_param(c)) return;
  scpi_err_t e = pop_error();
  reply(c, "%d,\"%s\"", e.code, e.msg);
}

static void cmd_sys_ver(cmd_t *c)
{
  if (no_param(c)) reply(c, "1999.0");
}

enum { MEAS_TEMP, MEAS_HUM, MEAS_ALL };

static void measure(cmd_t *c, int what)
{
  if (!no_param(c)) return;
  int32_t t, rh;
  if (!sht4x_measure(&t, &rh)) {
    scpi_push_error(-240, "Hardware error; SHT4x not responding");
    reply(c, what == MEAS_ALL ? SCPI_NAN "," SCPI_NAN : SCPI_NAN);
    return;
  }
  char ts[12], hs[12];
  fmt_c100(ts, sizeof ts, t);
  fmt_c100(hs, sizeof hs, rh);
  switch (what) {
    case MEAS_TEMP: reply(c, "%s", ts); break;
    case MEAS_HUM:  reply(c, "%s", hs); break;
    default:        reply(c, "%s,%s", ts, hs); break;
  }
}

static void cmd_meas_temp(cmd_t *c) { measure(c, MEAS_TEMP); }
static void cmd_meas_hum(cmd_t *c)  { measure(c, MEAS_HUM); }
static void cmd_meas_all(cmd_t *c)  { measure(c, MEAS_ALL); }

static void cmd_pin_mode(cmd_t *c)
{
  if (!pin_suffix(c) || !need_param(c)) return;
  const char *p = c->param;
  if (streq_ci(p, "IN") || streq_ci(p, "INP") || streq_ci(p, "INPUT"))
    dio_set_mode((unsigned)c->suffix, DIO_MODE_IN);
  else if (streq_ci(p, "OUT") || streq_ci(p, "OUTP") || streq_ci(p, "OUTPUT"))
    dio_set_mode((unsigned)c->suffix, DIO_MODE_OUT);
  else if (streq_ci(p, "OD"))
    dio_set_mode((unsigned)c->suffix, DIO_MODE_OD);
  else
    scpi_push_error(-224, "Illegal parameter value");
}

static void cmd_pin_mode_q(cmd_t *c)
{
  if (!pin_suffix(c) || !no_param(c)) return;
  static const char *const names[] = {"IN", "OUT", "OD"};
  reply(c, "%s", names[dio_get_mode((unsigned)c->suffix)]);
}

static void cmd_pin(cmd_t *c)
{
  if (!pin_suffix(c) || !need_param(c)) return;
  const char *p = c->param;
  if (streq_ci(p, "1") || streq_ci(p, "ON"))
    dio_write((unsigned)c->suffix, true);
  else if (streq_ci(p, "0") || streq_ci(p, "OFF"))
    dio_write((unsigned)c->suffix, false);
  else
    scpi_push_error(-224, "Illegal parameter value");
}

static void cmd_pin_q(cmd_t *c)
{
  if (pin_suffix(c) && no_param(c)) reply(c, "%d", dio_read((unsigned)c->suffix));
}

static void cmd_port_q(cmd_t *c)
{
  if (!no_param(c)) return;
  uint8_t v = dio_read_port();
  char bits[DIO_COUNT + 1];
  for (int i = 0; i < DIO_COUNT; i++) bits[i] = (v & (0x80 >> i)) ? '1' : '0'; /* DIO7 first */
  bits[DIO_COUNT] = '\0';
  reply(c, "%s", bits);
}

typedef struct {
  const char *pattern;
  void (*fn)(cmd_t *c);
} scpi_cmd_t;

static const scpi_cmd_t commands[] = {
  {"*IDN?",                cmd_idn},
  {"*RST",                 cmd_rst},
  {"*CLS",                 cmd_cls},
  {"*OPC?",                cmd_opc},
  {"*TST?",                cmd_tst},
  {"SYSTem:ERRor?",        cmd_err},
  {"SYSTem:ERRor:NEXT?",   cmd_err},
  {"SYSTem:VERSion?",      cmd_sys_ver},
  {"MEASure:TEMPerature?", cmd_meas_temp},
  {"MEASure:HUMidity?",    cmd_meas_hum},
  {"MEASure:ALL?",         cmd_meas_all},
  {"DIGital:PIN#:MODE",    cmd_pin_mode},
  {"DIGital:PIN#:MODE?",   cmd_pin_mode_q},
  {"DIGital:PIN#",         cmd_pin},
  {"DIGital:PIN#?",        cmd_pin_q},
  {"DIGital:PORT?",        cmd_port_q},
};

/* ---------- header matching ---------- */

/* pat: one pattern node like "MEASure" or "PIN#"; in: one input node like "meas" or "PIN3". */
static bool node_match(const char *pat, size_t plen, const char *in, size_t ilen, int *suffix)
{
  bool wants_suffix = plen && pat[plen - 1] == '#';
  if (wants_suffix) {
    plen--;
    size_t d = ilen;
    while (d > 0 && isdigit((unsigned char)in[d - 1])) d--;
    if (d == ilen || ilen - d > 3) return false;
    int n = 0;
    for (size_t i = d; i < ilen; i++) n = n * 10 + (in[i] - '0');
    *suffix = n;
    ilen = d;
  }

  size_t short_len = 0;
  while (short_len < plen && !islower((unsigned char)pat[short_len])) short_len++;
  if (ilen != short_len && ilen != plen) return false;

  for (size_t i = 0; i < ilen; i++) {
    if (toupper((unsigned char)in[i]) != toupper((unsigned char)pat[i])) return false;
  }
  return true;
}

static bool header_match(const char *pat, const char *hdr, int *suffix)
{
  size_t pl = strlen(pat), hl = strlen(hdr);
  bool pq = pl && pat[pl - 1] == '?';
  bool hq = hl && hdr[hl - 1] == '?';
  if (pq != hq) return false;
  if (pq) { pl--; hl--; }

  *suffix = -1;
  const char *p = pat, *pe = pat + pl;
  const char *h = hdr, *he = hdr + hl;
  while (p < pe && h < he) {
    const char *pn = memchr(p, ':', (size_t)(pe - p));
    const char *hn = memchr(h, ':', (size_t)(he - h));
    if (!pn) pn = pe;
    if (!hn) hn = he;
    if (!node_match(p, (size_t)(pn - p), h, (size_t)(hn - h), suffix)) return false;
    p = pn < pe ? pn + 1 : pe;
    h = hn < he ? hn + 1 : he;
  }
  return p == pe && h == he;
}

static char *trim(char *s)
{
  while (isspace((unsigned char)*s)) s++;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
  return s;
}

static void execute(char *cmd, char *out, size_t out_size)
{
  cmd = trim(cmd);
  if (!*cmd) return;
  if (*cmd == ':') cmd++;

  char *param = cmd;
  while (*param && !isspace((unsigned char)*param)) param++;
  if (*param) *param++ = '\0';
  param = trim(param);

  for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++) {
    cmd_t c = {.param = param, .out = out, .out_size = out_size};
    if (header_match(commands[i].pattern, cmd, &c.suffix)) {
      commands[i].fn(&c);
      return;
    }
  }
  scpi_push_error(-113, "Undefined header");
}

void scpi_process_line(char *line, char *out, size_t out_size)
{
  size_t used = 0;
  out[0] = '\0';

  char *save = NULL;
  for (char *cmd = strtok_r(line, ";", &save); cmd; cmd = strtok_r(NULL, ";", &save)) {
    char resp[96] = "";
    execute(cmd, resp, sizeof resp);
    if (!resp[0]) continue;
    int n = snprintf(out + used, out_size - used, "%s%s", used ? ";" : "", resp);
    if (n < 0 || (size_t)n >= out_size - used) {
      scpi_push_error(-223, "Too much data");
      break;
    }
    used += (size_t)n;
  }
}

void scpi_reset(void)
{
  err_count = 0;
  dio_init();
}
