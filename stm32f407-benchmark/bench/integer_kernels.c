#include "integer_kernels.h"

#if !__ARM_FEATURE_IDIV || !__ARM_FEATURE_DSP
#error The compiler must target hardware integer division and DSP multiplication.
#endif

uint32_t mul32_kernel(const uint32_t *a, const uint32_t *b)
{
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < INTEGER_ITEMS; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

uint64_t mul64_kernel(const uint32_t *a, const uint32_t *b)
{
    uint64_t sum = 0;
    unsigned i;
    for (i = 0; i < INTEGER_ITEMS; ++i) {
        sum += (uint64_t)a[i] * b[i];
    }
    return sum;
}

uint32_t udivmod32_kernel(const uint32_t *a, const uint32_t *b)
{
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < INTEGER_ITEMS; ++i) {
        sum += a[i] / b[i] + a[i] % b[i];
    }
    return sum;
}

uint32_t sdiv32_kernel(const int32_t *a, const int32_t *b)
{
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < INTEGER_ITEMS; ++i) {
        sum += (uint32_t)(a[i] / b[i]);
    }
    return sum;
}

uint32_t gcd32_kernel(const uint32_t *a, const uint32_t *b)
{
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < INTEGER_ITEMS; ++i) {
        uint32_t x = a[i], y = b[i];
        while (y != 0u) {
            uint32_t remainder = x % y;
            x = y;
            y = remainder;
        }
        sum += x;
    }
    return sum;
}

void matmul_i16_kernel(const int16_t *a, const int16_t *b, int32_t *result)
{
    unsigned row, col, k;
    for (row = 0; row < MATRIX_DIM; ++row) {
        for (col = 0; col < MATRIX_DIM; ++col) {
            int32_t sum = 0;
            for (k = 0; k < MATRIX_DIM; ++k) {
                sum += (int32_t)a[row * MATRIX_DIM + k] * b[k * MATRIX_DIM + col];
            }
            result[row * MATRIX_DIM + col] = sum;
        }
    }
}
