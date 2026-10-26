#include "board_config.h"
#include "scope_ui.h"
#include "capture.h"
#include "ili9341.h"
#include "dma.h"

static uint16_t plot[ADC_BUFFER_SIZE];
static char line[41];
static uint32_t position;
static void begin(void) { position=0U;line[0]='\0'; }
static void append(const char *text) {
    while (*text && position<sizeof(line)-1U) { line[position++]=*text++; }
    line[position]='\0';
}
static void number(uint32_t value) {
    char digits[10];uint32_t count=0U;
    do { digits[count++]=(char)('0'+value%10U);value/=10U; } while (value);
    while (count && position<sizeof(line)-1U) { line[position++]=digits[--count]; }
    line[position]='\0';
}
static void volts(uint32_t mv) {
    number(mv/1000U);append(".");
    if (mv%1000U<100U) { append("0"); }
    if (mv%1000U<10U) { append("0"); }
    number(mv%1000U);append("V");
}
static void row(uint16_t y,uint16_t color) {
    TFT_FillRect(0U,y,LCD_WIDTH,9U,0U);TFT_Text(2U,y,line,color);
}
void scope_ui_init(void) {
    TFT_Text(2U,5U,"F446RE DIGITAL OSCILLOSCOPE",0xFFFFU);
    TFT_FillRect(0U,75U,LCD_WIDTH,2U,0x07FFU);
    TFT_FillRect(0U,255U,LCD_WIDTH,2U,0x07FFU);
}
void scope_ui_status(const signal_measurements_t *m,uint32_t valid,uint32_t zoom,uint32_t menu) {
    begin();append(capture_mode_name());append("  ");
    append(capture_state==CAP_COMPLETE && capture_mode()!=MODE_SINGLE ? "RUN" : capture_state_name());
    append("  CH");number(capture_channel()+1U);row(22U,0xFFFFU);
    begin();append("FS ");number(capture_rate());append(" SPS  N240");row(37U,0xFFFFU);
    begin();append("VPP ");if (valid) { volts(m->peak_to_peak_mv); } else { append("---"); }
    append("  F ");if (valid && m->frequency_hz) { number(m->frequency_hz);append("HZ"); }
    else { append("---"); }row(52U,0xFFE0U);
    begin();append("TRIG ");volts(capture_threshold_mv());append(" RISING");row(264U,0xFFFFU);
    static const char *menus[]={"RUN: TAP STOP/ARM","RATE: TAP NEXT FS","TRIGGER: TAP NEXT MODE","VIEW: TAP FULL/ZOOM"};
    begin();append(menus[menu&3U]);row(280U,0xFFFFU);
    begin();append(zoom ? "ZOOM  " : "FULL  ");
    number(((zoom ? 119U : 239U)*1000000U)/capture_rate());append("US  HOLD B1:MENU");row(296U,0xFFFFU);
    begin();append("0-3.3V INPUT  TEST ");append(test_pwm_enabled ? "100HZ ON" : "OFF");row(309U,0xFFFFU);
}
void scope_ui_plot(const uint16_t *samples,uint32_t zoom) {
    for (uint32_t i=0U;i<ADC_BUFFER_SIZE;++i) { plot[i]=samples[zoom ? i/2U : i]; }
    TFT_DrawWaveform(plot);
}
