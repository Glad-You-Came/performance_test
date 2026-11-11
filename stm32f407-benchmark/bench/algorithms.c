#include "bench.h"

enum { BATCHES = 15, CALLS_PER_BATCH = 32 };

static uint8_t crc_input[CRC_BYTES];
static int16_t int_input[FIR_INPUTS], int_taps[FIR_TAPS];
static int32_t int_output[FIR_OUTPUTS], reference[FIR_OUTPUTS];
static float float_input[FIR_INPUTS], float_taps[FIR_TAPS];
static float float_output[FIR_OUTPUTS];
static uint32_t crc_result;

/* The memory barriers make each wrapper a potential memory writer, so LTO
   cannot prove the kernels pure and merge bench_measure's 32 identical calls
   into one execution (armclang -O2 -flto did that to the CRC wrapper: the
   batch then timed a single kernel run instead of 32). The barrier itself
   emits no code. */

static void crc_work(void)
{
    __asm__ volatile ("" ::: "memory");
    crc_result = crc32_ieee(crc_input);
}

static void int_work(void)
{
    __asm__ volatile ("" ::: "memory");
    fir_i16(int_input, int_taps, int_output);
}

static void float_work(void)
{
    __asm__ volatile ("" ::: "memory");
    fir_f32(float_input, float_taps, float_output);
}

static int crc_valid(void)
{
    return crc_result == 0x28e2568eu;
}

static int int_valid(void)
{
    unsigned i;
    for (i = 0; i < FIR_OUTPUTS; ++i) {
        if (int_output[i] != reference[i]) {
            return 0;
        }
    }
    return 1;
}

static int float_valid(void)
{
    unsigned i;
    for (i = 0; i < FIR_OUTPUTS; ++i) {
        if (float_output[i] != (float)reference[i] * (1.0f / 2048.0f)) {
            return 0;
        }
    }
    return 1;
}

void bench_measure(const char *name, void (*work)(void), int (*valid)(void))
{
    uint32_t samples[BATCHES];
    unsigned batch, call;

    work();
    if (!valid()) {
        bench_error(name, "reference", 0);
        return;
    }
    for (batch = 0; batch < BATCHES; ++batch) {
        uint32_t saved_mask = __get_PRIMASK();
        uint32_t start;
        __disable_irq();
        __DSB();
        __ISB();
        start = DWT->CYCCNT;
        for (call = 0; call < CALLS_PER_BATCH; ++call) {
            work();
        }
        samples[batch] = DWT->CYCCNT - start;
        __set_PRIMASK(saved_mask);
        if (!valid() || samples[batch] == 0) {
            bench_error(name, "sample", batch);
            return;
        }
    }
    for (batch = 0; batch < BATCHES; ++batch) {
        uart_text("ALG,");
        uart_text(name);
        uart_field(batch);
        uart_field(samples[batch]);
        uart_field(CALLS_PER_BATCH);
        uart_text("\r\n");
    }
}

void bench_algorithms(void)
{
    unsigned i, tap;

    for (i = 0; i < CRC_BYTES; ++i) {
        crc_input[i] = (uint8_t)(i * 37u + (i >> 3) * 19u + 11u);
    }
    for (i = 0; i < FIR_INPUTS; ++i) {
        int_input[i] = (int16_t)((int)((i * 73u + 19u) % 251u) - 125);
        float_input[i] = (float)int_input[i] * (1.0f / 128.0f);
    }
    /* Tap-major reference. Small dyadic values make every float sum exact. */
    for (tap = 0; tap < FIR_TAPS; ++tap) {
        int_taps[tap] = (int16_t)((int)((tap * 11u + 5u) % 31u) - 15);
        float_taps[tap] = (float)int_taps[tap] * (1.0f / 16.0f);
        for (i = 0; i < FIR_OUTPUTS; ++i) {
            reference[i] += (int32_t)int_input[i + tap] * int_taps[tap];
        }
    }
    bench_measure("crc32", crc_work, crc_valid);
    bench_measure("fir_i16", int_work, int_valid);
    bench_measure("fir_f32", float_work, float_valid);
}
