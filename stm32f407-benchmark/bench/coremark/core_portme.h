#ifndef F407_CORE_PORTME_H
#define F407_CORE_PORTME_H

#include <stddef.h>
#include <stdint.h>

#define HAS_FLOAT 0
#define HAS_STDIO 0
#define HAS_PRINTF 0
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0
#define MULTITHREAD 1
#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MEM_LOCATION "Flash code, SRAM1 data"
#define align_mem(x) ((void *)(((uintptr_t)(x) + 3u) & ~(uintptr_t)3u))

/* The build passes -DBENCH_TIER_<name>; map it to the exact flag string. */
#ifdef __ARMCC_VERSION
#define F407_STR2(x) #x
#define F407_STR(x) F407_STR2(x)
#define COMPILER_VERSION "ArmClang " F407_STR(__ARMCC_VERSION)
#if defined(BENCH_TIER_O0)
#define COMPILER_FLAGS "-O0 Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#elif defined(BENCH_TIER_O3)
#define COMPILER_FLAGS "-O3 Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#elif defined(BENCH_TIER_O2LTO)
#define COMPILER_FLAGS "-O2 -flto Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#elif defined(BENCH_TIER_SIZE)
#define COMPILER_FLAGS "-Oz Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#else
#define COMPILER_FLAGS "-O2 Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#endif
#else
#define COMPILER_VERSION "GCC " __VERSION__
#if defined(BENCH_TIER_O0)
#define COMPILER_FLAGS "-O0 Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#elif defined(BENCH_TIER_O3)
#define COMPILER_FLAGS "-O3 Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#elif defined(BENCH_TIER_O2LTO)
#define COMPILER_FLAGS "-O2 -flto Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#elif defined(BENCH_TIER_SIZE)
#define COMPILER_FLAGS "-Os Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#else
#define COMPILER_FLAGS "-O2 Cortex-M4 Thumb FPv4-SP-D16 hard-float"
#endif
#endif

typedef uint8_t ee_u8;
typedef int16_t ee_s16;
typedef uint16_t ee_u16;
typedef int32_t ee_s32;
typedef uint32_t ee_u32;
typedef uintptr_t ee_ptr_int;
typedef size_t ee_size_t;
typedef uint32_t CORE_TICKS;
typedef struct { ee_u8 portable_id; } core_portable;
extern ee_u32 default_num_contexts;

void portable_init(core_portable *port, int *argc, char *argv[]);
void portable_fini(core_portable *port);
void ee_printf(const char *format, ...);
int coremark_entry(void);

/* Namespace the standalone entry point without editing upstream files. */
#define main coremark_entry
#endif
