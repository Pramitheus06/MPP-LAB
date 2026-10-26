/* Adapted from Amiruddin Siddiqui's Digital_Oscilloscope timer.c. */
#include "stm32f4xx.h"
#include "timer.h"
#include "exti.h"
#if ENABLE_KEYPAD
#include "controls.h"
#endif

volatile uint32_t tick_ms;

void TIM2_set_rate(uint32_t rate_hz)
{
    if (rate_hz != 1000UL && rate_hz != 2000UL && rate_hz != 5000UL &&
        rate_hz != 10000UL && rate_hz != 500000UL) { return; }
    TIM2->CR1 = 0U;
    TIM2->PSC = 0U;
    TIM2->ARR = TIMER_CLOCK_HZ / rate_hz - 1UL;
    /* ADC is disabled by caller while issuing UG. */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    TIM2->CR2 = TIM_CR2_MMS_1;
}

void TIM2_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2_set_rate(SAMPLE_RATE_HZ);
}

void TIM3_IRQHandler(void)
{
    if ((TIM3->SR & TIM_SR_UIF) != 0U) {
        TIM3->SR = 0U;
        ++tick_ms;
        buttons_tick_1ms();
#if ENABLE_KEYPAD
        controls_tick_1ms();
#endif
    }
}

void TIM3_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    TIM3->CR1 = 0U;
    TIM3->PSC = TIMER_CLOCK_HZ / 1000000UL - 1UL;
    TIM3->ARR = 999UL;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM3_IRQn, 2U);
    NVIC_EnableIRQ(TIM3_IRQn);
    TIM3->CR1 = TIM_CR1_CEN;
}
