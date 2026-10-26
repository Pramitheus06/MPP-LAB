/* Adapted from Amiruddin Siddiqui's Digital_Oscilloscope adc.c. */
#include "stm32f4xx.h"
#include "adc.h"
#include "dma.h"

void ADC_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->APB2ENR;
    ADC1->CR2 = 0U;
    /* APB2 / 4 = 21 MHz. 15 sample + 12 conversion cycles = 1.286 us,
     * within the 2 us trigger period at 500 ksample/s. */
    ADC123_COMMON->CCR = ADC_CCR_ADCPRE_0;
    ADC1->CR1 = 0U;
    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_EXTEN_0 |
                 (6UL << ADC_CR2_EXTSEL_Pos); /* TIM2 TRGO */
    ADC_Set_GPIOA_PA0to5_DMA(0U);
}

void ADC_Set_GPIOA_PA0to5_DMA(uint8_t pin)
{
    if (pin > 1U) { return; }
    GPIOA->MODER |= 3UL << (2U * pin);
    GPIOA->PUPDR &= ~(3UL << (2U * pin));
    ADC1->SMPR2 = (ADC1->SMPR2 & ~(7UL << (3U * pin))) |
                    (1UL << (3U * pin));
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = pin;
}

void ADC_DMA_start(void)
{
    ADC1->CR1 &= ~ADC_CR1_EOCIE;
    TIM2->CR1 &= ~TIM_CR1_CEN;
    ADC1->CR2 &= ~(ADC_CR2_ADON | ADC_CR2_DMA);
    (void)ADC1->DR;
    ADC1->SR = 0U;
    /* Load PSC/ARR with ADC disabled: UG itself generates a TRGO. */
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    DMA_start();
    ADC1->CR2 |= ADC_CR2_DMA | ADC_CR2_ADON;
    delay_us(3U);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void ADC_DMA_stop(void)
{
    ADC1->CR1 &= ~ADC_CR1_EOCIE;
    TIM2->CR1 &= ~TIM_CR1_CEN;
    ADC1->CR2 &= ~ADC_CR2_ADON;
    DMA_stop();
    DMA2->LIFCR = 0x3DUL;
    NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    (void)ADC1->DR;
    ADC1->SR = 0U;
    dma_complete = 0U;
}
