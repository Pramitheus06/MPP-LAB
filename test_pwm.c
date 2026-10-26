#include "stm32f4xx.h"
volatile uint32_t test_pwm_enabled;

void test_pwm_init(void)
{
#if ENABLE_TEST_PWM
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;
    /* PA8 AF1 = TIM1_CH1. APB2 timer clock is 84 MHz. */
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << 16U)) | (2UL << 16U);
    GPIOA->PUPDR &= ~(3UL << 16U);
    GPIOA->OTYPER &= ~(1UL << 8U);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~15UL) | 1UL;
    TIM1->CR1 = 0U;
    TIM1->PSC = CORE_CLOCK_HZ / 1000000UL - 1UL;
    TIM1->ARR = 9999U;
    TIM1->CCR1 = 5000U;
    TIM1->CCMR1 = (6UL << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    test_pwm_enabled=1U;
#endif
}

void test_pwm_set_enabled(uint32_t on)
{
#if ENABLE_TEST_PWM
    if (on) { test_pwm_init(); }
    else {
        TIM1->CR1 &= ~TIM_CR1_CEN;
        TIM1->CCER &= ~TIM_CCER_CC1E;
        GPIOA->BSRR=1UL<<24U;
        GPIOA->MODER=(GPIOA->MODER & ~(3UL<<16U)) | (1UL<<16U);
        test_pwm_enabled=0U;
    }
#else
    (void)on;
#endif
}
