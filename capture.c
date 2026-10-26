/* ADC EOC IRQ detects a real rising crossing; prepared DMA stores the tail. */
#include "stm32f4xx.h"
#include "adc.h"
#include "capture.h"
#include "dma.h"
#include "timer.h"

volatile capture_state_t capture_state;
volatile uint32_t capture_forced;
static capture_mode_t mode;
static uint32_t rate = SAMPLE_RATE_HZ;
static uint32_t channel;
static uint32_t threshold_mv = 1650U;
static volatile uint32_t threshold_raw = 2048U;
static volatile uint32_t seen_low;
static uint32_t armed_at;

void capture_init(void)
{
    capture_state = CAP_STOPPED;
    NVIC_SetPriority(ADC_IRQn, 0U);
    NVIC_EnableIRQ(ADC_IRQn);
}

void capture_stop(void)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    ADC_DMA_stop();
    NVIC_ClearPendingIRQ(ADC_IRQn);
    capture_state = CAP_STOPPED;
    __set_PRIMASK(mask);
}

void capture_arm(void)
{
    capture_stop();
    ADC_Set_GPIOA_PA0to5_DMA((uint8_t)channel);
    TIM2_set_rate(rate);
    /* DMA is ready, but ADC DMA requests remain off while EOC monitors edges. */
    DMA_start_record(1U, ADC_BUFFER_SIZE-1U);
    ADC1->CR2 &= ~(ADC_CR2_ADON | ADC_CR2_DMA);
    ADC1->CR2 |= ADC_CR2_EOCS | ADC_CR2_DDS;
    (void)ADC1->DR;
    ADC1->SR = 0U;
    seen_low = 0U;
    capture_forced = 0U;
    armed_at = tick_ms;
    capture_state = CAP_ARMED;
    NVIC_ClearPendingIRQ(ADC_IRQn);
    ADC1->CR1 |= ADC_CR1_EOCIE;
    ADC1->CR2 |= ADC_CR2_ADON;
    delay_us(3U);
    TIM2->CNT = 0U;
    TIM2->CR1 |= TIM_CR1_CEN;
}

void ADC_IRQHandler(void)
{
    const uint32_t status = ADC1->SR;
    const uint16_t sample = (uint16_t)ADC1->DR;
    if ((status & ADC_SR_OVR) != 0U) {
        TIM2->CR1 &= ~TIM_CR1_CEN;
        ADC1->CR1 &= ~ADC_CR1_EOCIE;
        ADC1->CR2 &= ~ADC_CR2_ADON;
        ADC1->SR = 0U;
        capture_state = CAP_ERROR;
        return;
    }
    if (capture_state != CAP_ARMED || !(status & ADC_SR_EOC)) { return; }
    if (sample + 16U <= threshold_raw) { seen_low = 1U; }
    if (seen_low && sample >= threshold_raw + 16U) {
        adc_buffer[0] = sample;
        capture_state = CAP_RECORDING;
        ADC1->CR1 &= ~ADC_CR1_EOCIE;
        __DMB();
        /* The next TIM2 trigger fills sample 1; timer phase is preserved. */
        ADC1->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS;
    }
}

void capture_service(void)
{
    if (capture_state == CAP_ARMED && mode == MODE_AUTO &&
        (uint32_t)(tick_ms-armed_at) >= AUTO_TRIGGER_TIMEOUT_MS) {
        const uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (capture_state == CAP_ARMED) {
            capture_forced = 1U;
            capture_state = CAP_RECORDING;
            ADC_DMA_start();
        }
        __set_PRIMASK(mask);
    }
    if (dma_error || (capture_state==CAP_RECORDING && (ADC1->SR & ADC_SR_OVR))) {
        TIM2->CR1 &= ~TIM_CR1_CEN;
        ADC1->CR2 &= ~ADC_CR2_ADON;
        capture_state = CAP_ERROR;
    }
    else if (capture_state == CAP_RECORDING && dma_complete) {
        __DMB();
        capture_state = CAP_COMPLETE;
    }
}

void capture_set_mode(capture_mode_t value) { if (value<=MODE_SINGLE) { mode=value; } }
capture_mode_t capture_mode(void) { return mode; }
uint32_t capture_rate(void) { return rate; }
uint32_t capture_set_rate(uint32_t value)
{
    if (value!=1000U && value!=2000U && value!=5000U && value!=10000U) { return 0U; }
    rate=value; return 1U;
}
void capture_set_threshold_mv(uint32_t value)
{
    if (value<50U) { value=50U; }
    if (value>3250U) { value=3250U; }
    threshold_mv=value;
    threshold_raw=(value*4095U+ADC_REFERENCE_MV/2U)/ADC_REFERENCE_MV;
}
uint32_t capture_threshold_mv(void) { return threshold_mv; }
void capture_set_channel(uint32_t value) { if (value<2U) { channel=value; } }
uint32_t capture_channel(void) { return channel; }
const char *capture_mode_name(void)
{
    static const char *names[]={"AUTO","NORMAL","SINGLE"};
    return names[mode];
}
const char *capture_state_name(void)
{
    static const char *names[]={"STOP","WAIT","CAPTURE","HOLD","ERROR"};
    return names[capture_state];
}
