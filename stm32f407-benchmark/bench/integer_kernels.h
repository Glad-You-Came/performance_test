#ifndef INTEGER_KERNELS_H
#define INTEGER_KERNELS_H

#include <stdint.h>

enum { INTEGER_ITEMS = 256, MATRIX_DIM = 16 };

uint32_t mul32_kernel(const uint32_t *a, const uint32_t *b);
uint64_t mul64_kernel(const uint32_t *a, const uint32_t *b);
uint32_t udivmod32_kernel(const uint32_t *a, const uint32_t *b);
uint32_t sdiv32_kernel(const int32_t *a, const int32_t *b);
uint32_t gcd32_kernel(const uint32_t *a, const uint32_t *b);
void matmul_i16_kernel(const int16_t *a, const int16_t *b, int32_t *result);

#endif
