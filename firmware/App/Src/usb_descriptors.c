#include <string.h>
#include "tusb.h"
#include "app.h"
#include "version.h"

/* pid.codes test VID/PID, fine for development. Get a real PID before distributing. */
#define USB_VID 0x1209
#define USB_PID 0x0001

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL, STRID_CDC, STRID_USBTMC };

static const tusb_desc_device_t desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0002, /* firmware 0.2 */
  .iManufacturer      = 1,
  .iProduct           = 2,
  .iSerialNumber      = 3,
  .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void)
{
  return (const uint8_t *)&desc_device;
}

/* USBTMC must be interface 0: pyvisa-py detaches the kernel driver from interface 0
 * when it opens the instrument, which would otherwise disconnect the CDC serial port. */
enum { ITF_NUM_USBTMC = 0, ITF_NUM_CDC, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82

#define EPNUM_TMC_OUT   0x03
#define EPNUM_TMC_IN    0x83
#define EPNUM_TMC_INT   0x84

#define TUD_USBTMC_DESC_LEN \
  (TUD_USBTMC_IF_DESCRIPTOR_LEN + TUD_USBTMC_BULK_DESCRIPTORS_LEN + TUD_USBTMC_INT_DESCRIPTOR_LEN)

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_USBTMC_DESC_LEN)

static const uint8_t desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_USBTMC_IF_DESCRIPTOR(ITF_NUM_USBTMC, 3, STRID_USBTMC, TUD_USBTMC_PROTOCOL_USB488),
  TUD_USBTMC_BULK_DESCRIPTORS(EPNUM_TMC_OUT, EPNUM_TMC_IN, 64),
  TUD_USBTMC_INT_DESCRIPTOR(EPNUM_TMC_INT, 8, 16),
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return desc_configuration;
}

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void)langid;
  const char *str;

  switch (index) {
    case STRID_LANGID:
      desc_str[1] = 0x0409; /* English (US) */
      desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
      return desc_str;
    case STRID_MANUFACTURER: str = DEV_MANUFACTURER; break;
    case STRID_PRODUCT:      str = DEV_MODEL; break;
    case STRID_SERIAL:       str = app_serial(); break;
    case STRID_CDC:          str = DEV_MODEL " serial"; break;
    case STRID_USBTMC:       str = DEV_MODEL " USBTMC"; break;
    default: return NULL;
  }

  size_t n = strlen(str);
  if (n > 32) n = 32;
  for (size_t i = 0; i < n; i++) desc_str[1 + i] = (uint8_t)str[i];
  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return desc_str;
}
