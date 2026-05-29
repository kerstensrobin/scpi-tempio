/*
 * app.c - Simple SCPI firmware for STM32G070 USB GPIO + SHT41 module
 *
 * This file is intentionally written in a simple style.
 * It is meant to be easy to read and modify, not maximally abstract.
 *
 * Hardware assumed:
 *   - STM32G070KBTx
 *   - USB CDC enabled in STM32CubeMX
 *   - SHT41 temperature/humidity sensor on I2C1
 *   - 8 user GPIO pins on PA0..PA7
 *
 * User GPIO mapping:
 *   DIO0 = PA0
 *   DIO1 = PA1
 *   DIO2 = PA2
 *   DIO3 = PA3
 *   DIO4 = PA4
 *   DIO5 = PA5
 *   DIO6 = PA6
 *   DIO7 = PA7
 *
 * Supported SCPI-like commands:
 *   *IDN?
 *   MEAS:TEMP?
 *   MEAS:HUM?
 *   MEAS:ALL?
 *   DIG:PIN0:MODE IN
 *   DIG:PIN0:MODE OUT
 *   DIG:PIN0?
 *   DIG:PIN0 1
 *   DIG:PIN0 0
 *   DIG:PORT?
 *   SYST:ERR?
 *
 * Integration overview:
 *   1. Generate a CubeMX project with USB CDC and I2C1 enabled.
 *   2. Add this file as Core/Src/app.c.
 *   3. Add app.h with the three function prototypes shown at the bottom.
 *   4. Call APP_Init() once in main.c.
 *   5. Call APP_Task() inside while(1).
 *   6. Forward received USB CDC bytes to APP_CDC_Receive().
 */

#include "main.h"
#include "usbd_cdc_if.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* CubeMX creates this I2C handle when I2C1 is enabled. */
extern I2C_HandleTypeDef hi2c1;

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define FIRMWARE_VERSION        "0.1.0"
#define DEVICE_ID_STRING        "Robin,STM32G070-SCPI-IO,0001," FIRMWARE_VERSION

#define RX_BUFFER_SIZE          128
#define TX_BUFFER_SIZE          128

/* SHT41 uses 7-bit I2C address 0x44 by default.
 * STM32 HAL expects the address shifted left by one bit.
 */
#define SHT41_ADDR              (0x44 << 1)
#define SHT41_CMD_MEASURE       0xFD    /* High precision measurement */
#define SHT41_CMD_RESET         0x94

#define GPIO_COUNT              8

/* The 8 user pins are PA0..PA7. */
static GPIO_TypeDef *gpio_port[GPIO_COUNT] = {
    GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOA
};

static uint16_t gpio_pin[GPIO_COUNT] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7
};

/* Remember whether each GPIO is currently configured as input or output.
 * This is useful for MODE? queries.
 */
typedef enum {
    PIN_MODE_INPUT,
    PIN_MODE_OUTPUT
} PinMode;

static PinMode pin_mode[GPIO_COUNT];

/* -------------------------------------------------------------------------- */
/* USB receive buffer                                                         */
/* -------------------------------------------------------------------------- */

/* USB CDC receives chunks of bytes.
 * SCPI commands are text lines, so we collect bytes until \n or \r arrives.
 */
static char command_buffer[RX_BUFFER_SIZE];
static uint16_t command_length = 0;

/* Last SCPI-style error.
 * SYST:ERR? returns this and then clears it.
 */
static char last_error[64] = "0,\"No error\"";

/* -------------------------------------------------------------------------- */
/* Small helper functions                                                     */
/* -------------------------------------------------------------------------- */

static void send_text(const char *text)
{
    CDC_Transmit_FS((uint8_t *)text, strlen(text));
}

static void send_line(const char *text)
{
    char tx[TX_BUFFER_SIZE];
    snprintf(tx, sizeof(tx), "%s\r\n", text);
    send_text(tx);
}

static void set_error(int code, const char *message)
{
    snprintf(last_error, sizeof(last_error), "%d,\"%s\"", code, message);
}

static void clear_error(void)
{
    snprintf(last_error, sizeof(last_error), "0,\"No error\"");
}

static void make_uppercase(char *s)
{
    while (*s != '\0') {
        *s = (char)toupper((unsigned char)*s);
        s++;
    }
}

static void trim_whitespace(char *s)
{
    /* Remove leading whitespace. */
    while (*s == ' ' || *s == '\t') {
        memmove(s, s + 1, strlen(s));
    }

    /* Remove trailing whitespace. */
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

/* Extract the pin number from commands like DIG:PIN3? or DIG:PIN3 1.
 * Returns true if a valid pin number 0..7 was found.
 */
static bool get_pin_number(const char *command, uint8_t *pin_number)
{
    const char *p = strstr(command, "DIG:PIN");

    if (p == NULL) {
        return false;
    }

    p += strlen("DIG:PIN");

    if (*p < '0' || *p > '7') {
        return false;
    }

    *pin_number = (uint8_t)(*p - '0');
    return true;
}

/* -------------------------------------------------------------------------- */
/* GPIO functions                                                             */
/* -------------------------------------------------------------------------- */

static void configure_pin_as_input(uint8_t index)
{
    GPIO_InitTypeDef init = {0};

    init.Pin = gpio_pin[index];
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(gpio_port[index], &init);
    pin_mode[index] = PIN_MODE_INPUT;
}

static void configure_pin_as_output(uint8_t index)
{
    GPIO_InitTypeDef init = {0};

    init.Pin = gpio_pin[index];
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(gpio_port[index], &init);
    pin_mode[index] = PIN_MODE_OUTPUT;
}

static void configure_all_pins_as_inputs(void)
{
    for (uint8_t i = 0; i < GPIO_COUNT; i++) {
        configure_pin_as_input(i);
    }
}

static void write_pin(uint8_t index, bool high)
{
    HAL_GPIO_WritePin(gpio_port[index], gpio_pin[index], high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool read_pin(uint8_t index)
{
    return HAL_GPIO_ReadPin(gpio_port[index], gpio_pin[index]) == GPIO_PIN_SET;
}

/* -------------------------------------------------------------------------- */
/* SHT41 temperature/humidity functions                                       */
/* -------------------------------------------------------------------------- */

/* Sensirion sensors add an 8-bit CRC after each 16-bit measurement value.
 * This checks that the received data is valid.
 */
static uint8_t sht41_crc8(uint8_t byte1, uint8_t byte2)
{
    uint8_t data[2] = {byte1, byte2};
    uint8_t crc = 0xFF;

    for (uint8_t i = 0; i < 2; i++) {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }

    return crc;
}

static void sht41_reset(void)
{
    uint8_t command = SHT41_CMD_RESET;
    HAL_I2C_Master_Transmit(&hi2c1, SHT41_ADDR, &command, 1, 100);
    HAL_Delay(2);
}

/* Read temperature and humidity from the SHT41.
 * Returns true if the measurement succeeded.
 */
static bool sht41_read(float *temperature_c, float *humidity_rh)
{
    uint8_t command = SHT41_CMD_MEASURE;
    uint8_t rx[6];

    /* Ask the sensor to take a high-precision measurement. */
    if (HAL_I2C_Master_Transmit(&hi2c1, SHT41_ADDR, &command, 1, 100) != HAL_OK) {
        set_error(-230, "SHT41 write failed");
        return false;
    }

    /* High-precision measurement takes a few milliseconds. */
    HAL_Delay(10);

    /* Read 6 bytes:
     *   temperature MSB
     *   temperature LSB
     *   temperature CRC
     *   humidity MSB
     *   humidity LSB
     *   humidity CRC
     */
    if (HAL_I2C_Master_Receive(&hi2c1, SHT41_ADDR, rx, 6, 100) != HAL_OK) {
        set_error(-231, "SHT41 read failed");
        return false;
    }

    /* Check CRC bytes. */
    if (sht41_crc8(rx[0], rx[1]) != rx[2]) {
        set_error(-232, "SHT41 temperature CRC failed");
        return false;
    }

    if (sht41_crc8(rx[3], rx[4]) != rx[5]) {
        set_error(-233, "SHT41 humidity CRC failed");
        return false;
    }

    uint16_t raw_temperature = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t raw_humidity    = ((uint16_t)rx[3] << 8) | rx[4];

    /* Convert raw values using the SHT4x datasheet formulas. */
    *temperature_c = -45.0f + 175.0f * ((float)raw_temperature / 65535.0f);
    *humidity_rh   =  -6.0f + 125.0f * ((float)raw_humidity    / 65535.0f);

    /* Clamp humidity to the physical 0..100 %RH range. */
    if (*humidity_rh < 0.0f) {
        *humidity_rh = 0.0f;
    }
    if (*humidity_rh > 100.0f) {
        *humidity_rh = 100.0f;
    }

    clear_error();
    return true;
}

/* -------------------------------------------------------------------------- */
/* SCPI command handling                                                      */
/* -------------------------------------------------------------------------- */

static void handle_measurement_command(const char *command)
{
    float temperature;
    float humidity;
    char response[TX_BUFFER_SIZE];

    if (!sht41_read(&temperature, &humidity)) {
        send_line(last_error);
        return;
    }

    if (strcmp(command, "MEAS:TEMP?") == 0) {
        snprintf(response, sizeof(response), "%.3f", temperature);
        send_line(response);
    }
    else if (strcmp(command, "MEAS:HUM?") == 0) {
        snprintf(response, sizeof(response), "%.3f", humidity);
        send_line(response);
    }
    else if (strcmp(command, "MEAS:ALL?") == 0 || strcmp(command, "READ?") == 0) {
        snprintf(response, sizeof(response), "%.3f,%.3f", temperature, humidity);
        send_line(response);
    }
}

static void handle_gpio_command(char *command)
{
    uint8_t pin;
    char response[TX_BUFFER_SIZE];

    if (!get_pin_number(command, &pin)) {
        set_error(-224, "Invalid pin number");
        send_line(last_error);
        return;
    }

    /* Mode query: DIG:PIN0:MODE? */
    if (strstr(command, ":MODE?") != NULL) {
        if (pin_mode[pin] == PIN_MODE_INPUT) {
            send_line("IN");
        } else {
            send_line("OUT");
        }
        return;
    }

    /* Set input mode: DIG:PIN0:MODE IN */
    if (strstr(command, ":MODE IN") != NULL) {
        configure_pin_as_input(pin);
        send_line("OK");
        return;
    }

    /* Set output mode: DIG:PIN0:MODE OUT */
    if (strstr(command, ":MODE OUT") != NULL) {
        configure_pin_as_output(pin);
        send_line("OK");
        return;
    }

    /* Pin read: DIG:PIN0? */
    if (command[strlen(command) - 1] == '?') {
        send_line(read_pin(pin) ? "1" : "0");
        return;
    }

    /* Pin write: DIG:PIN0 1 or DIG:PIN0 0 */
    if (strstr(command, " 1") != NULL) {
        write_pin(pin, true);
        send_line("OK");
        return;
    }

    if (strstr(command, " 0") != NULL) {
        write_pin(pin, false);
        send_line("OK");
        return;
    }

    set_error(-109, "Missing or invalid parameter");
    send_line(last_error);
}

static void handle_port_query(void)
{
    char response[GPIO_COUNT + 1];

    for (uint8_t i = 0; i < GPIO_COUNT; i++) {
        response[i] = read_pin(i) ? '1' : '0';
    }

    response[GPIO_COUNT] = '\0';
    send_line(response);
}

static void handle_command(char *command)
{
    trim_whitespace(command);
    make_uppercase(command);

    if (strlen(command) == 0) {
        return;
    }

    /* Standard identification query. */
    if (strcmp(command, "*IDN?") == 0) {
        send_line(DEVICE_ID_STRING);
        return;
    }

    /* Reset the application state, but not the whole MCU. */
    if (strcmp(command, "*RST") == 0) {
        configure_all_pins_as_inputs();
        sht41_reset();
        clear_error();
        send_line("OK");
        return;
    }

    /* Return and clear the last error. */
    if (strcmp(command, "SYST:ERR?") == 0) {
        send_line(last_error);
        clear_error();
        return;
    }

    /* Environmental measurement commands. */
    if (strcmp(command, "MEAS:TEMP?") == 0 ||
        strcmp(command, "MEAS:HUM?")  == 0 ||
        strcmp(command, "MEAS:ALL?")  == 0 ||
        strcmp(command, "READ?")      == 0) {
        handle_measurement_command(command);
        return;
    }

    /* Read all digital pins at once. */
    if (strcmp(command, "DIG:PORT?") == 0) {
        handle_port_query();
        return;
    }

    /* Individual GPIO commands. */
    if (strncmp(command, "DIG:PIN", 7) == 0) {
        handle_gpio_command(command);
        return;
    }

    set_error(-113, "Undefined header");
    send_line(last_error);
}

/* -------------------------------------------------------------------------- */
/* Public functions called by main.c and usbd_cdc_if.c                        */
/* -------------------------------------------------------------------------- */

void APP_Init(void)
{
    configure_all_pins_as_inputs();
    sht41_reset();
    clear_error();
}

/* This function receives bytes from the USB CDC driver.
 * It is called from CDC_Receive_FS() in usbd_cdc_if.c.
 *
 * It does not execute the command immediately.
 * It only collects characters until a line ending arrives.
 */
void APP_CDC_Receive(uint8_t *buffer, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++) {
        char c = (char)buffer[i];

        if (c == '\n' || c == '\r') {
            if (command_length > 0) {
                command_buffer[command_length] = '\0';
                handle_command(command_buffer);
                command_length = 0;
            }
        }
        else {
            if (command_length < RX_BUFFER_SIZE - 1) {
                command_buffer[command_length] = c;
                command_length++;
            }
            else {
                command_length = 0;
                set_error(-350, "Input buffer overflow");
                send_line(last_error);
            }
        }
    }
}

/* This is here for future expansion.
 * Right now all work happens when USB bytes arrive.
 * Still call APP_Task() in while(1), because later you may add blinking LEDs,
 * periodic measurement, watchdog servicing, etc.
 */
void APP_Task(void)
{
    /* Nothing to do yet. */
}

/* -------------------------------------------------------------------------- */
/* Minimal app.h                                                              */
/* -------------------------------------------------------------------------- */

/*
Create Core/Inc/app.h with this content:

#pragma once

#include <stdint.h>

void APP_Init(void);
void APP_Task(void);
void APP_CDC_Receive(uint8_t *buffer, uint32_t length);
*/

/* -------------------------------------------------------------------------- */
/* main.c integration                                                         */
/* -------------------------------------------------------------------------- */

/*
In Core/Src/main.c:

#include "app.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USB_DEVICE_Init();

    APP_Init();

    while (1)
    {
        APP_Task();
    }
}
*/

/* -------------------------------------------------------------------------- */
/* USB CDC integration                                                        */
/* -------------------------------------------------------------------------- */

/*
In Core/Src/usbd_cdc_if.c:

1. Add this include near the top:

#include "app.h"

2. Replace CDC_Receive_FS() with:

static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
    APP_CDC_Receive(Buf, *Len);

    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);

    return (USBD_OK);
}
*/

