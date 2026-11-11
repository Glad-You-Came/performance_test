#ifndef F407_BENCH_H
#define F407_BENCH_H

#include <stdint.h>
#include "stm32f4xx.h"

enum {
    CORE_HZ = 168000000,
    TIMER_HZ = 84000000,
    CRC_BYTES = 1024,
    FIR_TAPS = 16,
    FIR_OUTPUTS = 256,
    FIR_INPUTS = FIR_OUTPUTS + FIR_TAPS - 1
};

uint32_t crc32_ieee(const uint8_t input[CRC_BYTES]);
void fir_i16(const int16_t input[FIR_INPUTS], const int16_t taps[FIR_TAPS],
             int32_t output[FIR_OUTPUTS]);
void fir_f32(const float input[FIR_INPUTS], const float taps[FIR_TAPS],
             float output[FIR_OUTPUTS]);

void uart_text(const char *text);
void uart_u32(uint32_t value);
void uart_field(uint32_t value);
void bench_error(const char *scenario, const char *reason, uint32_t detail);
void bench_algorithms(void);
void bench_measure(const char *name, void (*work)(void), int (*valid)(void));
void bench_integer(void);
void bench_coremark(void);
void bench_peripherals(void);

#endif
