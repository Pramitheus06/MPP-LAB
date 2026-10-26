/*
 * uart.c
 *
 *  Created on: Sep 23, 2026
 *      Author: asus
 */


#include "stm32f4xx.h"
#include "uart.h"

/* Interrupt receive keeps controls responsive while foreground sends/draws.
 * Single producer / single consumer; one command byte per event. */
static volatile uint8_t rx_buffer[16];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
volatile uint32_t uart_rx_errors;
volatile uint32_t uart_tx_bytes;
/* Digital pin observations during transmit waits, for VCP wiring diagnostics. */
volatile uint32_t uart_tx_pin_low;
volatile uint32_t uart_tx_pin_high;

void USART2_IRQHandler(void)
{
    const uint32_t status = USART2->SR;
    const uint8_t value = (uint8_t)USART2->DR;
    if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        ++uart_rx_errors;
    }
    if (((status & USART_SR_RXNE) != 0U) &&
        ((status & (USART_SR_FE | USART_SR_NE)) == 0U)) {
        const uint8_t next = (rx_head + 1U) & 15U;
        if (next != rx_tail) {
            rx_buffer[rx_head] = value;
            rx_head = next;
        } else { ++uart_rx_errors; }
    }
}

void UART2_Init(void) {
    // Enable GPIOA clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Enable USART2 clock
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // PA2 = USART2_TX
    // PA3 = USART2_RX
    GPIOA->MODER &= ~((3U << 4) | (3U << 6));
    GPIOA->MODER |=  ((2U << 4) | (2U << 6));
    GPIOA->OTYPER &= ~((1UL << 2U) | (1UL << 3U));
    GPIOA->OSPEEDR |= (3UL << 4U) | (3UL << 6U);
    GPIOA->PUPDR &= ~((3UL << 4U) | (3UL << 6U));

    // AF7 = USART2
    GPIOA->AFR[0] &= ~((0xFU << 8) | (0xFU << 12));
    GPIOA->AFR[0] |=  ((7U << 8) | (7U << 12));

    /*
     * USART2 baud rate = 115200
     *
     * This BRR assumes:
     * PCLK1 = 42 MHz; derive BRR from the configured clock tree.
     */
    USART2->CR1 = 0U;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->BRR = (APB1_CLOCK_HZ + UART_BAUD_HZ / 2U) / UART_BAUD_HZ;

    // Enable USART2
    // Enable transmitter
    // Enable receiver
    rx_head = 0U;
    rx_tail = 0U;
    NVIC_SetPriority(USART2_IRQn, 1U);
    NVIC_EnableIRQ(USART2_IRQn);
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
}

int32_t UART2_GetChar(void)
{
    if (rx_head == rx_tail) { return -1; }
    const uint8_t value = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1U) & 15U;
    return value;
}


void UART2_SendChar(char c) {
    // Wait until transmit data register is empty
    while (!(USART2->SR & USART_SR_TXE)) {
        if (GPIOA->IDR & (1UL<<2U)) { ++uart_tx_pin_high; }
        else { ++uart_tx_pin_low; }
    }

    USART2->DR = (uint8_t)c;
    ++uart_tx_bytes;
}


void UART2_SendString(const char *str) {
    while (*str) {
        UART2_SendChar(*str);
        str++;
    }
}


/*
 * Send one ADC value as 2 raw bytes.
 *
 * Example:
 *
 * ADC value = 0x0ABC
 *
 * Sends:
 * 0xBC   -> low byte
 * 0x0A   -> high byte
 */
void UART2_SendADC(uint16_t value) {
    UART2_SendChar((char)(value & 0xFFU));
    UART2_SendChar((char)(value >> 8U));
}



/*
 * Send the complete ADC DMA buffer with Frame Sync Header.
 */
void UART2_SendBuffer(const volatile uint16_t *buffer, uint16_t size) {
    // 1. Send 4-Byte Frame Sync Header
    UART2_SendChar(0xAA);
    UART2_SendChar(0x55);
    UART2_SendChar(0xAA);
    UART2_SendChar(0x55);

    // 2. Send ADC payload bytes
    for (uint16_t i = 0; i < size; i++) {
        UART2_SendADC(buffer[i]);
    }
}

void UART2_WriteU32(uint32_t value)
{
    char digits[10];uint32_t count=0U;
    do { digits[count++]=(char)('0'+value%10U);value/=10U; } while (value);
    while (count) { UART2_SendChar(digits[--count]); }
}

