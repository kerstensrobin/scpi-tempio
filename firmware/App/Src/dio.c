#include "main.h"
#include "dio.h"

#define DIO_PORT GPIOA

/* Timer channel per DIO pin, from the STM32G0B1 alternate function table.
 * PA5 only has TIM2_CH1, the same channel as PA0. */
typedef struct {
  TIM_TypeDef *tim;
  uint8_t ch;
  uint8_t af;
} pwm_map_t;

static const pwm_map_t pwm_map[DIO_COUNT] = {
  {TIM2,  1, GPIO_AF2_TIM2},  /* DIO0 PA0 */
  {TIM2,  2, GPIO_AF2_TIM2},  /* DIO1 PA1 */
  {TIM15, 1, GPIO_AF5_TIM15}, /* DIO2 PA2 */
  {TIM15, 2, GPIO_AF5_TIM15}, /* DIO3 PA3 */
  {TIM14, 1, GPIO_AF4_TIM14}, /* DIO4 PA4 */
  {TIM2,  1, GPIO_AF2_TIM2},  /* DIO5 PA5 */
  {TIM16, 1, GPIO_AF5_TIM16}, /* DIO6 PA6 */
  {TIM17, 1, GPIO_AF5_TIM17}, /* DIO7 PA7 */
};

static dio_mode_t modes[DIO_COUNT];

/* ---------- timer helpers ---------- */

static uint32_t timer_clock(void)
{
  uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  return (RCC->CFGR & RCC_CFGR_PPRE_2) ? 2 * pclk : pclk; /* x2 when APB is divided */
}

static void timer_clk_enable(TIM_TypeDef *t)
{
  if (t == TIM2) __HAL_RCC_TIM2_CLK_ENABLE();
  else if (t == TIM14) __HAL_RCC_TIM14_CLK_ENABLE();
  else if (t == TIM15) __HAL_RCC_TIM15_CLK_ENABLE();
  else if (t == TIM16) __HAL_RCC_TIM16_CLK_ENABLE();
  else if (t == TIM17) __HAL_RCC_TIM17_CLK_ENABLE();
}

static volatile uint32_t *ccr_reg(TIM_TypeDef *t, unsigned ch)
{
  return &t->CCR1 + (ch - 1);
}

/* Another pin (not `pin`) running PWM on the same timer, and optionally the same channel. */
static int other_pwm_user(unsigned pin, bool same_channel)
{
  for (unsigned j = 0; j < DIO_COUNT; j++) {
    if (j == pin || modes[j] != DIO_MODE_PWM || pwm_map[j].tim != pwm_map[pin].tim) continue;
    if (!same_channel || pwm_map[j].ch == pwm_map[pin].ch) return (int)j;
  }
  return -1;
}

static void channel_start(TIM_TypeDef *t, unsigned ch, uint32_t ccr)
{
  volatile uint32_t *ccmr = ch <= 2 ? &t->CCMR1 : &t->CCMR2;
  unsigned sh = ((ch - 1) & 1) * 8;
  uint32_t clear = (0x3u << sh) | (0x7u << (4 + sh)) | (1u << (16 + sh)); /* CCxS, OCxM */
  *ccmr = (*ccmr & ~clear) | (6u << (4 + sh)) | (1u << (3 + sh));         /* PWM mode 1, preload */
  *ccr_reg(t, ch) = ccr;
  t->CCER |= 1u << ((ch - 1) * 4);
}

static void channel_stop(TIM_TypeDef *t, unsigned ch)
{
  t->CCER &= ~(1u << ((ch - 1) * 4));
}

static void pwm_release(unsigned pin)
{
  const pwm_map_t *m = &pwm_map[pin];
  modes[pin] = DIO_MODE_IN; /* so other_pwm_user() no longer counts this pin */
  if (other_pwm_user(pin, true) < 0) channel_stop(m->tim, m->ch);
  if (other_pwm_user(pin, false) < 0) m->tim->CR1 &= ~TIM_CR1_CEN;
}

/* ---------- public API ---------- */

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
  if (modes[pin] == DIO_MODE_PWM) pwm_release(pin);

  GPIO_InitTypeDef init = {0};
  init.Pin = 1u << pin;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  switch (mode) {
    case DIO_MODE_OUT: init.Mode = GPIO_MODE_OUTPUT_PP; break;
    case DIO_MODE_OD:  init.Mode = GPIO_MODE_OUTPUT_OD; break;
    default:           init.Mode = GPIO_MODE_INPUT; mode = DIO_MODE_IN; break;
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

dio_err_t dio_set_pwm(unsigned pin, uint64_t freq_mhz, uint32_t duty_c100)
{
  if (freq_mhz < DIO_PWM_MIN_MHZ || freq_mhz > DIO_PWM_MAX_MHZ) return DIO_ERR_RANGE;
  if (duty_c100 > DIO_PWM_DUTY_MAX) return DIO_ERR_RANGE;

  const pwm_map_t *m = &pwm_map[pin];
  TIM_TypeDef *t = m->tim;

  /* Timer counts per period, split into prescaler and auto-reload so ARR is as large as
   * possible (best duty resolution). Integer math: frequency is in millihertz. */
  uint64_t clk_mhz = (uint64_t)timer_clock() * 1000u;
  uint64_t ticks = (clk_mhz + freq_mhz / 2) / freq_mhz;
  uint64_t span = (t == TIM2) ? (1ull << 32) : (1ull << 16); /* TIM2 is 32-bit, the rest 16-bit */
  uint32_t psc = ticks > span ? (uint32_t)((ticks - 1) / span) : 0; /* ceil(ticks / span) - 1 */
  uint32_t period = (uint32_t)((ticks + (psc + 1) / 2) / (psc + 1));
  if (period < 2) period = 2;
  uint32_t arr = period - 1;
  uint32_t ccr = (uint32_t)(((uint64_t)duty_c100 * period + DIO_PWM_DUTY_MAX / 2) / DIO_PWM_DUTY_MAX);

  /* Pins sharing the timer must agree on the period; DIO0/DIO5 also on the duty. */
  if (other_pwm_user(pin, false) >= 0 && (t->PSC != psc || t->ARR != arr)) return DIO_ERR_CONFLICT;
  if (other_pwm_user(pin, true) >= 0 && *ccr_reg(t, m->ch) != ccr) return DIO_ERR_CONFLICT;

  if (other_pwm_user(pin, false) < 0) {
    /* This pin is the timer's only user: (re)program the time base. The update event comes
     * after channel_start() so the preloaded duty applies from the first period on. */
    timer_clk_enable(t);
    t->CR1 = 0;
    t->PSC = psc;
    t->ARR = arr;
    t->CR1 = TIM_CR1_ARPE;
    channel_start(t, m->ch, ccr);
    t->EGR = TIM_EGR_UG; /* load PSC, ARR and CCR now */
    if (IS_TIM_BREAK_INSTANCE(t)) t->BDTR |= TIM_BDTR_MOE;
  } else {
    /* Timer already running for another pin: the new duty takes effect at the next period. */
    channel_start(t, m->ch, ccr);
  }
  t->CR1 |= TIM_CR1_CEN;

  GPIO_InitTypeDef init = {0};
  init.Pin = 1u << pin;
  init.Mode = GPIO_MODE_AF_PP;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  init.Alternate = m->af;
  HAL_GPIO_Init(DIO_PORT, &init);
  modes[pin] = DIO_MODE_PWM;
  return DIO_OK;
}

void dio_get_pwm(unsigned pin, uint64_t *freq_mhz, uint32_t *duty_c100)
{
  if (modes[pin] != DIO_MODE_PWM) {
    *freq_mhz = 0;
    *duty_c100 = 0;
    return;
  }
  const pwm_map_t *m = &pwm_map[pin];
  uint64_t period = (uint64_t)m->tim->ARR + 1u;
  uint64_t div = ((uint64_t)m->tim->PSC + 1u) * period;
  *freq_mhz = ((uint64_t)timer_clock() * 1000u + div / 2) / div;
  *duty_c100 = (uint32_t)(((uint64_t)*ccr_reg(m->tim, m->ch) * DIO_PWM_DUTY_MAX + period / 2) / period);
}
