#include "main.h"
#include "sht4x.h"

extern I2C_HandleTypeDef hi2c1;

#define SHT4X_ADDR          (0x44 << 1)
#define SHT4X_CMD_MEAS_HIGH 0xFD
#define SHT4X_TIMEOUT_MS    20

static uint8_t crc8(const uint8_t *data, int len)
{
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool sht4x_present(void)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, SHT4X_ADDR, 2, SHT4X_TIMEOUT_MS) == HAL_OK;
}

bool sht4x_measure(int32_t *temp_c100, int32_t *rh_c100)
{
  uint8_t cmd = SHT4X_CMD_MEAS_HIGH;
  uint8_t rx[6];

  if (HAL_I2C_Master_Transmit(&hi2c1, SHT4X_ADDR, &cmd, 1, SHT4X_TIMEOUT_MS) != HAL_OK)
    return false;
  HAL_Delay(10); /* max 8.3 ms for high repeatability */
  if (HAL_I2C_Master_Receive(&hi2c1, SHT4X_ADDR, rx, sizeof rx, SHT4X_TIMEOUT_MS) != HAL_OK)
    return false;
  if (crc8(&rx[0], 2) != rx[2] || crc8(&rx[3], 2) != rx[5])
    return false;

  uint32_t t_raw = ((uint32_t)rx[0] << 8) | rx[1];
  uint32_t rh_raw = ((uint32_t)rx[3] << 8) | rx[4];

  /* Datasheet: T = -45 + 175 * raw / 65535, RH = -6 + 125 * raw / 65535 */
  *temp_c100 = -4500 + (int32_t)((17500u * t_raw + 32767u) / 65535u);
  int32_t rh = -600 + (int32_t)((12500u * rh_raw + 32767u) / 65535u);
  if (rh < 0) rh = 0;
  if (rh > 10000) rh = 10000;
  *rh_c100 = rh;
  return true;
}
