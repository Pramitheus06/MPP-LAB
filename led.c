/*
 * led.c
 *
 *  Created on: Sep 24, 2026
 *      Author: asus
 */

#include "stm32f4xx.h"
#include "led.h"
#include <stdint.h>

void GPIOC_LED_setup(uint8_t PCx) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER &= ~(3U << (2 * PCx)); // Clear bits
    GPIOC->MODER |=  (1U << (2 * PCx)); // Set 01 = Output
}

void GPIOC_LED_ON(uint8_t PCx) {
	GPIOC->ODR |= (1U << PCx);
}

void GPIOC_LED_OFF(uint8_t PCx) {
	GPIOC->ODR &= ~(1U << PCx);
}
