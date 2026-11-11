#include "bench.h"

uint32_t crc32_ieee(const uint8_t input[CRC_BYTES])
{
    uint32_t crc = 0xffffffffu;
    unsigned i, bit;

    for (i = 0; i < CRC_BYTES; ++i) {
        crc ^= input[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void fir_i16(const int16_t input[FIR_INPUTS], const int16_t taps[FIR_TAPS],
             int32_t output[FIR_OUTPUTS])
{
    unsigned i, tap;

    for (i = 0; i < FIR_OUTPUTS; ++i) {
        int32_t sum = 0;
        for (tap = 0; tap < FIR_TAPS; ++tap) {
            sum += (int32_t)input[i + tap] * taps[tap];
        }
        output[i] = sum;
    }
}

void fir_f32(const float input[FIR_INPUTS], const float taps[FIR_TAPS],
             float output[FIR_OUTPUTS])
{
    unsigned i, tap;

    for (i = 0; i < FIR_OUTPUTS; ++i) {
        float sum = 0.0f;
        for (tap = 0; tap < FIR_TAPS; ++tap) {
            sum += input[i + tap] * taps[tap];
        }
        output[i] = sum;
    }
}
