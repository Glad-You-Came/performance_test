#include "bench.h"
#include "coremark.h"
#include <stdarg.h>

enum { COREMARK_ITERATIONS = 10000, TICKS_PER_SECOND = 1000000 };

volatile ee_s32 seed1_volatile;
volatile ee_s32 seed2_volatile;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = COREMARK_ITERATIONS;
volatile ee_s32 seed5_volatile;
ee_u32 default_num_contexts = 1;

static uint32_t start_tick, stop_tick;

void portable_init(core_portable *port, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;
    port->portable_id = 1;
}

void portable_fini(core_portable *port) { port->portable_id = 0; }
void start_time(void) { start_tick = TIM5->CNT; }
void stop_time(void) { stop_tick = TIM5->CNT; }
CORE_TICKS get_time(void) { return stop_tick - start_tick; }
secs_ret time_in_secs(CORE_TICKS ticks) { return ticks / TICKS_PER_SECOND; }

/* The upstream bare-metal report only uses these integer and string formats. */
void ee_printf(const char *format, ...)
{
    va_list args;
    char character[2] = { 0, 0 };
    va_start(args, format);
    uart_text("CORELOG,");
    while (*format) {
        char code = *format++;
        if (code == '%') {
            code = *format++;
            if (code == 'l') {
                ++format;
                uart_u32((uint32_t)va_arg(args, unsigned long));
            } else if (code == 'u') {
                uart_u32(va_arg(args, unsigned int));
            } else if (code == 'd') {
                int value = va_arg(args, int);
                if (value < 0) { uart_text("-"); }
                uart_u32(value < 0 ? 0u - (uint32_t)value : (uint32_t)value);
            } else if (code == 's') {
                uart_text(va_arg(args, const char *));
            } else if (code == '0' && format[0] == '4' && format[1] == 'x') {
                unsigned value = va_arg(args, unsigned int);
                int shift;
                format += 2;
                for (shift = 12; shift >= 0; shift -= 4) {
                    character[0] = "0123456789abcdef"[(value >> shift) & 15u];
                    uart_text(character);
                }
            } else {
                uart_text(" [unsupported format] ");
            }
        } else {
            character[0] = code;
            uart_text(character);
        }
    }
    va_end(args);
}

static void run_coremark(const char *mode, ee_s32 seed)
{
    seed1_volatile = seed;
    seed2_volatile = seed;
    uart_text("COREBEGIN,");
    uart_text(mode);
    uart_text("\r\n");
    coremark_entry();
    uart_text("COREEND,");
    uart_text(mode);
    uart_text("\r\n");
}

void bench_coremark(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    TIM5->CR1 = 0;
    TIM5->PSC = TIMER_HZ / TICKS_PER_SECOND - 1u;
    TIM5->ARR = UINT32_MAX;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->SR = 0;
    TIM5->CR1 = TIM_CR1_CEN;
    run_coremark("performance", 0);
    run_coremark("validation", 0x3415);
    TIM5->CR1 = 0;
}
