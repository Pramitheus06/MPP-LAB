#include "export.h"
#include "dma.h"
#include "uart.h"
uint32_t capture_crc32(const uint16_t *samples,uint32_t count)
{
    uint32_t crc=0xFFFFFFFFUL;
    for (uint32_t i=0U;i<count;++i) {
        for (uint32_t byte=0U;byte<2U;++byte) {
            crc^=(samples[i]>>(8U*byte))&0xFFU;
            for (uint32_t bit=0U;bit<8U;++bit) {
                crc=(crc>>1U)^((crc&1U) ? 0xEDB88320UL : 0U);
            }
        }
    }
    return ~crc;
}
void capture_export(const uint16_t *samples,uint32_t rate,uint32_t channel,
                    uint32_t mode,uint32_t forced,uint32_t threshold,uint32_t sequence)
{
    const uint32_t fields[]={rate,ADC_BUFFER_SIZE,channel+1U,mode,forced,threshold,sequence};
    UART2_SendString("#CAPTURE");
    for (uint32_t i=0U;i<sizeof(fields)/sizeof(fields[0]);++i) {
        UART2_SendChar(',');UART2_WriteU32(fields[i]);
    }
    UART2_SendChar('\n');
    UART2_SendBuffer(samples,ADC_BUFFER_SIZE);
    UART2_SendString("\n#END,");UART2_WriteU32(capture_crc32(samples,ADC_BUFFER_SIZE));
    UART2_SendChar('\n');
}
