#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU              OPT_MCU_STM32G0
#define CFG_TUSB_OS               OPT_OS_NONE
#define CFG_TUSB_DEBUG            0

#define BOARD_TUD_RHPORT          0
#define CFG_TUD_ENABLED           1
#define CFG_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE    64

#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_NOTIFY        1
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256
#define CFG_TUD_CDC_RX_EPSIZE     64
#define CFG_TUD_CDC_TX_EPSIZE     64

#endif
