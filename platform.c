#include "board_config.h"
#include "timer.h"

uint32_t SystemCoreClock = CORE_CLOCK_HZ;

void SystemInit(void)
{
    SCB->CPACR |= 0xFUL << 20U;
    SCB->VTOR = 0x08000000UL;
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) {}
    RCC->CFGR &= ~RCC_CFGR_SW;
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0U) {}
    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0U) {}
    /* HSI / 16 * 336 / 4 = 84 MHz. APB1=42 MHz, APB2=84 MHz. */
    RCC->PLLCFGR = 16UL | (336UL << 6U) | (1UL << 16U) | (7UL << 24U);
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_PRFTEN;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2)) | RCC_CFGR_PPRE1_DIV2;
    RCC->DCKCFGR &= ~RCC_DCKCFGR_TIMPRE;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SystemCoreClock = CORE_CLOCK_HZ;
    __DSB();
    __ISB();
}

void platform_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_ms(uint32_t milliseconds)
{
    const uint32_t start = tick_ms;
    while ((uint32_t)(tick_ms - start) < milliseconds) {}
}

void delay_us(uint32_t microseconds)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = microseconds * (CORE_CLOCK_HZ / 1000000UL);
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {}
}

static void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    const uint32_t shift = pin * 2U;
    port->MODER = (port->MODER & ~(3UL << shift)) | (mode << shift);
}

void gpio_mode_output(GPIO_TypeDef *port, uint32_t pin, uint32_t open_drain)
{
    gpio_set_mode(port, pin, 1U);
    if (open_drain != 0U) {
        port->OTYPER |= 1UL << pin;
    } else {
        port->OTYPER &= ~(1UL << pin);
    }
    port->OSPEEDR = (port->OSPEEDR & ~(3UL << (pin * 2U))) |
                     (2UL << (pin * 2U));
}

void gpio_mode_input(GPIO_TypeDef *port, uint32_t pin, uint32_t pull)
{
    gpio_set_mode(port, pin, 0U);
    port->PUPDR = (port->PUPDR & ~(3UL << (pin * 2U))) |
                  ((pull & 3UL) << (pin * 2U));
}

void gpio_mode_alternate(GPIO_TypeDef *port, uint32_t pin, uint32_t af,
                         uint32_t open_drain)
{
    gpio_set_mode(port, pin, 2U);
    if (open_drain != 0U) {
        port->OTYPER |= 1UL << pin;
    } else {
        port->OTYPER &= ~(1UL << pin);
    }
    port->OSPEEDR = (port->OSPEEDR & ~(3UL << (pin * 2U))) |
                     (3UL << (pin * 2U));
    const uint32_t index = pin >> 3U;
    const uint32_t shift = (pin & 7U) * 4U;
    port->AFR[index] = (port->AFR[index] & ~(0xFUL << shift)) |
                       ((af & 0xFUL) << shift);
}

void gpio_mode_analog(GPIO_TypeDef *port, uint32_t pin)
{
    gpio_set_mode(port, pin, 3U);
    port->PUPDR &= ~(3UL << (pin * 2U));
}

void gpio_write(GPIO_TypeDef *port, uint32_t pin, uint32_t high)
{
    if (high != 0U) {
        port->BSRR = 1UL << pin;
    } else {
        port->BSRR = 1UL << (pin + 16U);
    }
}

uint32_t gpio_read(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR >> pin) & 1UL;
}
