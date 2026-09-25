#include "main.h"
#include "dio.h"

#define DIO_PORT GPIOA

static dio_mode_t modes[DIO_COUNT];

void dio_init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  for (unsigned i = 0; i < DIO_COUNT; i++) {
    HAL_GPIO_WritePin(DIO_PORT, 1u << i, GPIO_PIN_RESET);
    dio_set_mode(i, DIO_MODE_IN);
  }
}

void dio_set_mode(unsigned pin, dio_mode_t mode)
{
  GPIO_InitTypeDef init = {0};
  init.Pin = 1u << pin;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  switch (mode) {
    case DIO_MODE_OUT: init.Mode = GPIO_MODE_OUTPUT_PP; break;
    case DIO_MODE_OD:  init.Mode = GPIO_MODE_OUTPUT_OD; break;
    default:           init.Mode = GPIO_MODE_INPUT; break;
  }
  /* ODR keeps its value across mode changes, so outputs come up at the last written level. */
  HAL_GPIO_Init(DIO_PORT, &init);
  modes[pin] = mode;
}

dio_mode_t dio_get_mode(unsigned pin)
{
  return modes[pin];
}

void dio_write(unsigned pin, bool level)
{
  HAL_GPIO_WritePin(DIO_PORT, 1u << pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool dio_read(unsigned pin)
{
  return HAL_GPIO_ReadPin(DIO_PORT, 1u << pin) == GPIO_PIN_SET;
}

uint8_t dio_read_port(void)
{
  return (uint8_t)(DIO_PORT->IDR & 0xFF);
}
