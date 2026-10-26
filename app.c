/* GitHub-based F446 DSO: edge-triggered DMA records and foreground UI/export. */
#include "stm32f4xx.h"
#include "app.h"
#include "adc.h"
#include "analysis.h"
#include "capture.h"
#include "dma.h"
#include "exti.h"
#include "timer.h"
#include "uart.h"
#include "ili9341.h"
#include "scope_ui.h"
#include "spi.h"
#include "export.h"
#if ENABLE_KEYPAD
#include "controls.h"
#endif
#if ENABLE_TM1637
#include "seven_segment.h"
#endif

uint8_t system_on=1U;
uint8_t active_channel=1U;
uint8_t adc_running;
volatile uint32_t scope_frame_count;
static uint16_t saved[ADC_BUFFER_SIZE];
static signal_measurements_t measurements;
static uint32_t valid,processed,completed_at,zoom,menu;
static uint32_t status_dirty=1U,plot_dirty,clear_plot,export_requested;
static uint32_t saved_rate,saved_channel,saved_mode,saved_forced,saved_threshold;
static uint32_t stream_enabled; /* explicit opt-in; snapshot export is preferred */
#if ENABLE_TM1637
static uint32_t segment_refreshed_at,segment_test_started,segment_test_active;
#endif

static void arm(void) {
    processed=0U;capture_arm();status_dirty=1U;
}
static void stop(void) {
    system_on=0U;capture_stop();scope_ui_init();status_dirty=1U;
}
static void start(void) {
    system_on=1U;arm();
}
static void next_rate(int direction) {
    static const uint32_t rates[]={1000U,2000U,5000U,10000U};
    uint32_t index=0U;
    while (index<3U && rates[index]!=capture_rate()) { ++index; }
    index=(index+(direction<0 ? 3U : 1U))&3U;
    capture_stop();(void)capture_set_rate(rates[index]);
    valid=0U;clear_plot=1U;
    if (system_on) { arm(); }
    status_dirty=1U;
#if ENABLE_TM1637
    seven_segment_show_rate(capture_rate());
#endif
}
static void set_rate(uint32_t value) {
    capture_stop();(void)capture_set_rate(value);
    valid=0U;clear_plot=1U;
    if (system_on) { arm(); }
    status_dirty=1U;
#if ENABLE_TM1637
    seven_segment_show_rate(capture_rate());
#endif
}
static void next_mode(void) {
    capture_stop();
    capture_set_mode((capture_mode_t)((capture_mode()+1U)%3U));
    if (system_on) { arm(); }
    status_dirty=1U;
}
static void change_view(void) {
    zoom^=1U;plot_dirty=valid;status_dirty=1U;
}
static void set_channel(uint32_t channel) {
    capture_stop();capture_set_channel(channel);active_channel=(uint8_t)(channel+1U);
    valid=0U;clear_plot=1U;
    if (system_on) { arm(); }
    status_dirty=1U;
}
void BTN_State(uint8_t event) {
    if (event==BTN_MENU) { menu=(menu+1U)&3U;status_dirty=1U; }
    else if (event==BTN_POWER) {
        if (menu==1U) { next_rate(1); }
        else if (menu==2U) { next_mode(); }
        else if (menu==3U) { change_view(); }
        else if (!system_on || (capture_mode()==MODE_SINGLE && capture_state==CAP_COMPLETE)) { start(); }
        else { stop(); }
    } else if (event==BTN_CH1) { set_channel(0U); }
    else if (event==BTN_CH2) { set_channel(1U); }
}
#if ENABLE_KEYPAD
static void keypad(void) {
    const char key=controls_take_key();
    if (key>='1' && key<='4') {
        static const uint32_t rates[]={1000U,2000U,5000U,10000U};
        set_rate(rates[key-'1']);
    } else if (key=='A') { next_mode(); }
    else if (key=='B') { change_view(); }
    else if (key=='D') { export_requested=1U; }
    else if (key=='*') { stop(); }
    else if (key=='#') { start(); }
}
#endif
static void status(void) {
    UART2_SendString("#STATUS,");UART2_SendString(capture_state_name());
    UART2_SendChar(',');UART2_SendString(capture_mode_name());
    UART2_SendChar(',');UART2_WriteU32(capture_rate());
    UART2_SendChar(',');UART2_WriteU32(capture_channel()+1U);
    UART2_SendChar(',');UART2_WriteU32(zoom);
    UART2_SendChar(',');UART2_WriteU32(valid);
    UART2_SendChar(',');UART2_WriteU32(scope_frame_count);
    UART2_SendChar(',');UART2_WriteU32(spi_fault);
    UART2_SendChar(',');UART2_WriteU32(menu);
    UART2_SendChar(',');UART2_WriteU32(capture_threshold_mv());
    UART2_SendChar('\n');
}
static void serial_command(void) {
    const int32_t key=UART2_GetChar();
    if (key=='h') {
        UART2_SendString("#HARDWARE,KEYPAD=");UART2_WriteU32(ENABLE_KEYPAD);
        UART2_SendString(",TM1637=");UART2_WriteU32(ENABLE_TM1637);
#if ENABLE_TM1637
        UART2_SendString(",ACK_ERRORS=");UART2_WriteU32(seven_segment_ack_errors());
        UART2_SendString(",CLK=");UART2_WriteU32((GPIOC->IDR>>8U)&1U);
        UART2_SendString(",DIO=");UART2_WriteU32((GPIOC->IDR>>9U)&1U);
#endif
        UART2_SendChar('\n');
    }
    else if (key=='t') {
#if ENABLE_TM1637
        seven_segment_init();seven_segment_set_brightness(3U);
        const uint8_t test[4]={0x7FU,0x7FU,0x7FU,0x7FU};
        seven_segment_show_raw(test);
        segment_test_started=tick_ms;segment_refreshed_at=tick_ms;
        segment_test_active=1U;
        UART2_SendString("#SEGMENT_TEST,8888_FOR_10_SECONDS\n");
#else
        UART2_SendString("#SEGMENT_DISABLED\n");
#endif
    }
    else if (key=='1') { set_channel(0U); }
    else if (key=='2') { set_channel(1U); }
    else if (key=='s') { stop();UART2_SendString("#STOP\n"); }
    else if (key=='r' || key=='a') { start(); }
    else if (key=='+' || key=='-') { next_rate(key=='+' ? 1 : -1); }
    else if (key=='q') { set_rate(1000U); }
    else if (key=='w') { set_rate(2000U); }
    else if (key=='e') { set_rate(5000U); }
    else if (key=='l') { set_rate(10000U); }
    else if (key=='m') { next_mode(); }
    else if (key=='b' || key=='d') { change_view(); }
    else if (key=='p') { export_requested=1U; }
    else if (key=='u') { stream_enabled^=1U; }
    else if (key=='U') { stream_enabled=1U; }
    else if (key=='j') { stream_enabled=0U; }
    else if (key=='i' || key=='?') { status(); }
    else if (key=='g' || key=='G') { test_pwm_set_enabled(key=='g');status_dirty=1U; }
    else if (key=='7' || key=='8' || key=='0') {
        capture_stop();
        const uint32_t previous=capture_threshold_mv();
        capture_set_threshold_mv(key=='0' ? 1650U :
            key=='8' ? previous+100U : previous>100U ? previous-100U : 50U);
        if (system_on) { arm(); }
        status_dirty=1U;
    }
}
static void send_saved(void) {
    export_requested=0U;
    if (!valid) { UART2_SendString("#NO_CAPTURE\n");return; }
    const uint32_t single_hold=capture_mode()==MODE_SINGLE && processed;
    capture_stop();
    capture_export(saved,saved_rate,saved_channel,saved_mode,saved_forced,
                   saved_threshold,scope_frame_count);
    if (system_on && !single_hold) { arm(); }
    else if (single_hold && system_on) { capture_state=CAP_COMPLETE; }
    status_dirty=1U;
}
void APP(void) {
    TIM3_init();SPI_Init();
    DMA_init();ADC_init();TIM2_init();test_pwm_init();capture_init();
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER=(GPIOA->MODER & ~(3UL<<10U)) | (1UL<<10U);
    EXTI_init();
#if ENABLE_KEYPAD
    controls_init();
#endif
#if ENABLE_TM1637
    seven_segment_init();seven_segment_show_rate(capture_rate());
#endif
    ILI9341_Init();scope_ui_init();
    /* Configure VCP once after GPIO/display startup. Do not toggle UE while
     * the host may be receiving a reply or discard queued commands. */
    UART2_Init();
    UART2_SendString("#READY,F446_GITHUB_SOFTWARE_V2\n");
    arm();
    capture_state_t previous_state=CAP_STOPPED;
    for (;;) {
        BTN_State(Take_Button_Event());
#if ENABLE_KEYPAD
        keypad();
#endif
        serial_command();
#if ENABLE_TM1637
        if ((uint32_t)(tick_ms-segment_refreshed_at)>=1000U) {
            segment_refreshed_at=tick_ms;
            if (segment_test_active && (uint32_t)(tick_ms-segment_test_started)<10000U) {
                const uint8_t test[4]={0x7FU,0x7FU,0x7FU,0x7FU};
                seven_segment_show_raw(test);
            } else {
                segment_test_active=0U;seven_segment_show_rate(capture_rate());
            }
        }
#endif
        capture_service();
        if (capture_state==CAP_COMPLETE && !processed) {
            for (uint32_t i=0U;i<ADC_BUFFER_SIZE;++i) { saved[i]=adc_buffer[i]; }
            saved_rate=capture_rate();saved_channel=capture_channel();
            saved_mode=capture_mode();saved_forced=capture_forced;
            saved_threshold=capture_threshold_mv();
            analysis_compute(saved,ADC_BUFFER_SIZE,saved_rate,
                (uint16_t)((saved_threshold*4095U)/ADC_REFERENCE_MV),&measurements);
            valid=1U;processed=1U;++scope_frame_count;completed_at=tick_ms;
            plot_dirty=1U;status_dirty=1U;
            if (stream_enabled) { UART2_SendBuffer(saved,ADC_BUFFER_SIZE); }
        }
        if (capture_state!=previous_state) {
            previous_state=capture_state;
            if (capture_state!=CAP_RECORDING) { status_dirty=1U; }
        }
        if (capture_state==CAP_ERROR) { system_on=0U; }
        adc_running=(uint8_t)(capture_state==CAP_ARMED || capture_state==CAP_RECORDING);
        GPIOA->BSRR=1UL<<(5U+(adc_running ? 0U : 16U));
        if (clear_plot) { TFT_ClearPlot();clear_plot=0U; }
        if (plot_dirty && valid) { scope_ui_plot(saved,zoom);plot_dirty=0U; }
        if (status_dirty) {
            scope_ui_status(&measurements,valid,zoom,menu);status_dirty=0U;
        }
        if (export_requested) { send_saved(); }
        if (system_on && capture_state==CAP_COMPLETE && capture_mode()!=MODE_SINGLE &&
            (uint32_t)(tick_ms-completed_at)>=80U) { arm(); }
    }
}
