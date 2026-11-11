#include "bench.h"
#include "integer_kernels.h"

static uint32_t a[INTEGER_ITEMS], b[INTEGER_ITEMS];
static int32_t signed_a[INTEGER_ITEMS], signed_b[INTEGER_ITEMS];
static int16_t matrix_a[MATRIX_DIM * MATRIX_DIM], matrix_b[MATRIX_DIM * MATRIX_DIM];
static int32_t matrix_result[MATRIX_DIM * MATRIX_DIM];
static uint64_t result;

static void mul32_work(void) { result = mul32_kernel(a, b); }
static void mul64_work(void) { result = mul64_kernel(a, b); }
static void udivmod_work(void) { result = udivmod32_kernel(a, b); }
static void sdiv_work(void) { result = sdiv32_kernel(signed_a, signed_b); }
static void gcd_work(void) { result = gcd32_kernel(a, b); }
static void matmul_work(void) { matmul_i16_kernel(matrix_a, matrix_b, matrix_result); }

/* Reference checksums were calculated with host arbitrary-precision integers. */
static int mul32_valid(void) { return result == 0x9f75f7bau; }
static int mul64_valid(void) { return result == UINT64_C(0x9c5a7a1c9f75f7ba); }
static int udivmod_valid(void) { return result == 0x4ad0bdf0u; }
static int sdiv_valid(void) { return result == 0x000db5f5u; }
static int gcd_valid(void) { return result == 0x1746u; }

static int matmul_valid(void)
{
    uint32_t checksum = 2166136261u;
    unsigned i;
    for (i = 0; i < MATRIX_DIM * MATRIX_DIM; ++i) {
        checksum = (checksum ^ (uint32_t)matrix_result[i]) * 16777619u;
    }
    return checksum == 0x1cdaf95bu;
}

void bench_integer(void)
{
    unsigned i;
    for (i = 0; i < INTEGER_ITEMS; ++i) {
        a[i] = ((i + 1u) * 0x9e3779b9u) ^ 0x735a2d97u;
        b[i] = (((i + 7u) * 0x85ebca6bu) >> (i % 20u)) | 1u;
        signed_a[i] = (int32_t)(a[i] & 0x7fffffffu);
        signed_b[i] = (int32_t)(b[i] & 0x7fffffffu);
        if ((i & 1u) != 0u) { signed_a[i] = -signed_a[i]; }
        if ((i & 2u) != 0u) { signed_b[i] = -signed_b[i]; }
    }
    for (i = 0; i < MATRIX_DIM * MATRIX_DIM; ++i) {
        matrix_a[i] = (int16_t)((int32_t)((i * 37u + 11u) % 257u) - 128);
        matrix_b[i] = (int16_t)((int32_t)((i * 53u + 7u) % 251u) - 125);
    }
    bench_measure("mul32", mul32_work, mul32_valid);
    bench_measure("mul64", mul64_work, mul64_valid);
    bench_measure("udivmod32", udivmod_work, udivmod_valid);
    bench_measure("sdiv32", sdiv_work, sdiv_valid);
    bench_measure("gcd32", gcd_work, gcd_valid);
    bench_measure("matmul_i16", matmul_work, matmul_valid);
}
