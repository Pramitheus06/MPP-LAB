///*
// * spi.c
// *
// *  Created on: Sep 25, 2026
// *      Author: asus
// */



#include "stm32f4xx.h"
#include "stdint.h"
#include "spi.h"

volatile uint32_t spi_fault;

static uint32_t spi_wait(uint32_t flag, uint32_t set)
{
    const uint32_t start = DWT->CYCCNT;
    while (((SPI2->SR & flag) != 0U) != (set != 0U)) {
        if ((uint32_t)(DWT->CYCCNT - start) > CORE_CLOCK_HZ / 10000UL) {
            spi_fault = 1U;
            SPI2->CR1 &= ~SPI_CR1_SPE;
            return 0U;
        }
    }
    return 1U;
}


void SPI_Init(void) {

    spi_fault = 0U;

	// 1. Enable SPI2 (APB1) and GPIOB (AHB1) clocks
	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->APB1ENR;
    RCC->APB1RSTR |= RCC_APB1RSTR_SPI2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI2RST;

	// 2. Set PB13 (SCK) and PB15 (MOSI) to Alternate Function mode (10)
	GPIOB->MODER &= ~((3U << (2 * 13)) | (3U << (2 * 15)));
	GPIOB->MODER |=  ((2U << (2 * 13)) | (2U << (2 * 15)));

	// 3. Map PB13 and PB15 to AF5 (SPI2)
	// PB13 and PB15 use AFR[1] (High Register)
	GPIOB->AFR[1] &= ~((15U << (4 * (13 - 8))) | (15U << (4 * (15 - 8))));
	GPIOB->AFR[1] |=  ((5U  << (4 * (13 - 8))) | (5U  << (4 * (15 - 8))));

	// 4. Set Very High Speed (11) for PB13 and PB15
	GPIOB->OSPEEDR |= ((3U << (2 * 13)) | (3U << (2 * 15)));

	// 5. Configure SPI2 peripheral
	SPI2->CR1 = 0;
	SPI2->CR1 &= ~SPI_CR1_CPHA;  // CPHA = 0
	SPI2->CR1 &= ~SPI_CR1_CPOL;  // CPOL = 0
	SPI2->CR1 |= SPI_CR1_MSTR;   // Master Mode
	SPI2->CR1 |= TFT_SPI_BR << SPI_CR1_BR_Pos;
	SPI2->CR1 &= ~SPI_CR1_DFF;   // 8-bit frame format
	SPI2->CR1 &= ~SPI_CR1_LSBFIRST; // MSB first
	SPI2->CR1 |= SPI_CR1_SSM;    // Software Slave Management
	SPI2->CR1 |= SPI_CR1_SSI;    // Internal NSS high
	SPI2->CR1 |= SPI_CR1_SPE;    // Enable SPI2

}


void SPI_SendByte(uint8_t data)
{
    if (spi_fault != 0U) { return; }
    // Wait until transmit register is empty
    if (!spi_wait(SPI_SR_TXE, 1U)) { return; }

    // Send data through SPI2
    *(volatile uint8_t *)&SPI2->DR = data;

    if (!spi_wait(SPI_SR_RXNE, 1U)) { return; }
    (void)*(volatile uint8_t *)&SPI2->DR;
    if (!spi_wait(SPI_SR_BSY, 0U)) { return; }
    (void)SPI2->SR;
}
