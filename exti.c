/* Adapted from Amiruddin Siddiqui's Digital_Oscilloscope exti.c.
 * Default control uses B1; upstream switches are optional. */
#include "stm32f4xx.h"
#include "exti.h"
#include "timer.h"
#include "button_filter.h"

static volatile uint8_t button_event;
static volatile uint32_t wake_pending;
static button_filter_t b1;

#if USE_UPSTREAM_BUTTONS
static void post_button(uint8_t event)
{
    button_event = event;
}
#endif

void buttons_tick_1ms(void)
{
    if (wake_pending || b1.stable || b1.raw) {
        const uint32_t event=button_filter_update(&b1,tick_ms,
            (GPIOC->IDR & (1UL<<13U))==0U);
        if (event) { button_event=(uint8_t)event;wake_pending=0U; }
    }
}

void EXTI_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    GPIOC->MODER &= ~(3UL << 26U);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3UL << 26U)) | (1UL << 26U);
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~(15UL << 4U)) | (2UL << 4U);
    EXTI->RTSR |= EXTI_RTSR_TR13;
    EXTI->FTSR |= EXTI_FTSR_TR13;
    EXTI->PR = EXTI_PR_PR13;
    EXTI->IMR |= EXTI_IMR_MR13;
    NVIC_SetPriority(EXTI15_10_IRQn, 1U);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    b1=(button_filter_t){0};
    wake_pending=(GPIOC->IDR & (1UL<<13U))==0U;
#if USE_UPSTREAM_BUTTONS
    for (int pin = 5; pin <= 7; ++pin) { BTNEXTI_Set_GPIOA_PA5to7_EXTIx(pin); }
    NVIC_SetPriority(EXTI9_5_IRQn, 1U);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
#endif
}

void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR13) != 0U) {
        EXTI->PR = EXTI_PR_PR13;
        wake_pending=1U;
    }
}

#if USE_UPSTREAM_BUTTONS && !ENABLE_KEYPAD
void EXTI9_5_IRQHandler(void)
{
    const uint32_t pending = EXTI->PR & (7UL << 5U);
    EXTI->PR = pending;
    if ((pending & (1UL << 7U)) != 0U) { post_button(BTN_POWER); }
    else if ((pending & (1UL << 5U)) != 0U) { post_button(BTN_CH1); }
    else if ((pending & (1UL << 6U)) != 0U) { post_button(BTN_CH2); }
}
#endif

uint8_t Take_Button_Event(void)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    const uint8_t event = button_event;
    button_event = 0U;
    __set_PRIMASK(mask);
    return event;
}

void BTNEXTI_Set_GPIOA_PA5to7_EXTIx(int pin)
{
    if ((pin < 5) || (pin > 7)) { return; }
    GPIOA->MODER &= ~(3UL << (2 * pin));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3UL << (2 * pin))) | (2UL << (2 * pin));
    SYSCFG->EXTICR[1] &= ~(15UL << (4 * (pin - 4)));
    EXTI->PR = 1UL << pin;
    EXTI->FTSR &= ~(1UL << pin);
    EXTI->RTSR |= 1UL << pin;
    EXTI->IMR |= 1UL << pin;
}
