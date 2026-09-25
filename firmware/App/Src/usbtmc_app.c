/* USBTMC / USB488 interface: gives the device a VISA address
 * USB0::0x1209::0x0001::<serial>::INSTR next to the CDC serial port.
 *
 * Callbacks only buffer data and set flags; the SCPI command itself runs from
 * usbtmc_task() in the main loop, so a slow command (SHT41 measurement) never
 * blocks inside the USB stack. */
#include <stdio.h>
#include <string.h>

#include "tusb.h"

#include "app.h"
#include "scpi.h"

#define STB_EAV 0x04u /* error queue not empty */
#define STB_MAV 0x10u /* message available */

static const usbtmc_response_capabilities_488_t capabilities = {
  .USBTMC_status = USBTMC_STATUS_SUCCESS,
  .bcdUSBTMC = USBTMC_VERSION,
  .bmIntfcCapabilities = {
    .listenOnly = 0,
    .talkOnly = 0,
    .supportsIndicatorPulse = 1,
  },
  .bmDevCapabilities = {
    .canEndBulkInOnTermChar = 0,
  },
  .bcdUSB488 = USBTMC_488_VERSION,
  .bmIntfcCapabilities488 = {
    .supportsTrigger = 0,
    .supportsREN_GTL_LLO = 0,
    .is488_2 = 1,
  },
  .bmDevCapabilities488 = {
    .SCPI = 1,
    .SR1 = 0,
    .RL1 = 0,
    .DT1 = 0,
  },
};

static char rx_buf[APP_LINE_MAX];
static size_t rx_len;
static bool rx_overflow;
static bool rx_eom;            /* EOM flag of the transfer being received */
static volatile bool cmd_pending;

static char tx_buf[APP_RESP_MAX + 1];
static size_t tx_len, tx_ix;
static bool bulk_in_requested;
static uint32_t bulk_in_max;

static void reset_state(void)
{
  rx_len = 0;
  rx_overflow = false;
  cmd_pending = false;
  tx_len = tx_ix = 0;
  bulk_in_requested = false;
}

static void send_chunk(void)
{
  size_t n = tu_min32((uint32_t)(tx_len - tx_ix), bulk_in_max);
  bool last = tx_ix + n == tx_len;
  tud_usbtmc_transmit_dev_msg_data(&tx_buf[tx_ix], n, last, false);
  tx_ix += n;
  bulk_in_requested = false;
}

void usbtmc_task(void)
{
  if (cmd_pending) {
    cmd_pending = false;
    if (rx_overflow) {
      scpi_push_error(-363, "Input buffer overrun");
    } else {
      static char out[APP_RESP_MAX];
      rx_buf[rx_len] = '\0';
      /* VISA usually appends '\n'; strip any trailing whitespace */
      while (rx_len && (rx_buf[rx_len - 1] == '\n' || rx_buf[rx_len - 1] == '\r'))
        rx_buf[--rx_len] = '\0';
      scpi_process_line(rx_buf, out, sizeof out);
      if (out[0]) {
        tx_len = (size_t)snprintf(tx_buf, sizeof tx_buf, "%s\n", out);
        tx_ix = 0;
      }
    }
    rx_len = 0;
    rx_overflow = false;
    app_activity();
    tud_usbtmc_start_bus_read();
  }

  if (bulk_in_requested && tx_ix < tx_len) send_chunk();
}

void tud_usbtmc_open_cb(uint8_t interface_id)
{
  (void)interface_id;
  reset_state();
  tud_usbtmc_start_bus_read();
}

const usbtmc_response_capabilities_488_t *tud_usbtmc_get_capabilities_cb(void)
{
  return &capabilities;
}

bool tud_usbtmc_msgBulkOut_start_cb(const usbtmc_msg_request_dev_dep_out *msgHeader)
{
  rx_eom = msgHeader->bmTransferAttributes.EOM;
  return true;
}

bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete)
{
  if (rx_len + len < sizeof rx_buf) {
    memcpy(&rx_buf[rx_len], data, len);
    rx_len += len;
  } else {
    rx_overflow = true;
  }

  if (transfer_complete) {
    if (rx_eom)
      cmd_pending = true;          /* whole message in, run it from the main loop */
    else
      tud_usbtmc_start_bus_read(); /* message continues in the next transfer */
  }
  return true;
}

bool tud_usbtmc_msgBulkIn_request_cb(const usbtmc_msg_request_dev_dep_in *request)
{
  /* With no response ready the request stays NAKed until usbtmc_task() has one;
   * VISA then times out, as with any instrument read without a query. */
  bulk_in_max = request->TransferSize;
  bulk_in_requested = true;
  if (!cmd_pending && tx_ix < tx_len) send_chunk();
  return true;
}

bool tud_usbtmc_msgBulkIn_complete_cb(void)
{
  if (tx_ix >= tx_len) tx_len = tx_ix = 0;
  tud_usbtmc_start_bus_read();
  return true;
}

uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmcResult)
{
  *tmcResult = USBTMC_STATUS_SUCCESS;
  uint8_t stb = 0;
  if (tx_ix < tx_len) stb |= STB_MAV;
  if (scpi_error_count()) stb |= STB_EAV;
  return stb;
}

bool tud_usbtmc_indicator_pulse_cb(const tusb_control_request_t *msg, uint8_t *tmcResult)
{
  (void)msg;
  app_identify();
  *tmcResult = USBTMC_STATUS_SUCCESS;
  return true;
}

/* Device clear (viClear): drop any half-received command and pending response. */
bool tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult)
{
  reset_state();
  *tmcResult = USBTMC_STATUS_SUCCESS;
  return true;
}

bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp)
{
  reset_state();
  rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
  rsp->bmClear.BulkInFifoBytes = 0u;
  return true;
}

bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult)
{
  tx_len = tx_ix = 0;
  bulk_in_requested = false;
  *tmcResult = USBTMC_STATUS_SUCCESS;
  return true;
}

bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{
  (void)rsp;
  tud_usbtmc_start_bus_read();
  return true;
}

bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult)
{
  rx_len = 0;
  rx_overflow = false;
  *tmcResult = USBTMC_STATUS_SUCCESS;
  return true;
}

bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{
  (void)rsp;
  tud_usbtmc_start_bus_read();
  return true;
}

void tud_usbtmc_bulkIn_clearFeature_cb(void)
{
}

void tud_usbtmc_bulkOut_clearFeature_cb(void)
{
  tud_usbtmc_start_bus_read();
}
