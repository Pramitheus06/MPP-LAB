/* Adapted from Amiruddin Siddiqui's Digital_Oscilloscope dma.c.
 * Normal-mode DMA owns one complete frame, then foreground owns it. */
#include "stm32f4xx.h"
#include "dma.h"

volatile uint16_t adc_buffer[ADC_BUFFER_SIZE];
volatile uint8_t dma_complete;
volatile uint32_t dma_error;
#define DMA_FLAGS (DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CTEIF0 | \
                   DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTCIF0)

void DMA2_Stream0_IRQHandler(void)
{
    const uint32_t status = DMA2->LISR;
    /* Normal mode prevents DMA wrapping even before this ISR runs. */
    TIM2->CR1 &= ~TIM_CR1_CEN;
    ADC1->CR2 &= ~ADC_CR2_ADON;
    DMA2->LIFCR = DMA_FLAGS;
    if ((status & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)) != 0U) {
        dma_error = status;
    } else if ((status & DMA_LISR_TCIF0) != 0U) {
        __DMB();
        dma_complete = 1U;
    }
}

void DMA_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    DMA_stop();
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)adc_buffer;
    DMA2_Stream0->CR = DMA_SxCR_MINC | DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0 |
                      DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE |
                      DMA_SxCR_PL_1;
    DMA2_Stream0->FCR = DMA_SxFCR_FEIE;
    DMA2->LIFCR = DMA_FLAGS;
    NVIC_SetPriority(DMA2_Stream0_IRQn, 0U);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

void DMA_start(void)
{
    DMA_start_record(0U, ADC_BUFFER_SIZE);
}

void DMA_start_record(uint32_t offset, uint32_t count)
{
    if (offset >= ADC_BUFFER_SIZE || count == 0U || count > ADC_BUFFER_SIZE-offset) { return; }
    DMA_stop();
    DMA2->LIFCR = DMA_FLAGS;
    NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    DMA2_Stream0->M0AR = (uint32_t)&adc_buffer[offset];
    DMA2_Stream0->NDTR = count;
    dma_complete = 0U;
    dma_error = 0U;
    __DMB();
    DMA2_Stream0->CR |= DMA_SxCR_EN;
}

void DMA_stop(void)
{
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) {}
}
