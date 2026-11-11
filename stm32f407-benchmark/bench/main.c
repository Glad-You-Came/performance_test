#include "bench.h"
#include "SYSTEM/sys/sys.h"

#ifdef __CC_ARM
#define TOOLCHAIN "armcc"
#else
#define TOOLCHAIN "gcc"
#endif
#ifdef BENCH_SIZE
#define PROFILE "size"
#else
#define PROFILE "speed"
#endif

static void uart_init(uint32_t pclk_hz)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    sys_gpio_set(GPIOB, SYS_GPIO_PIN6, SYS_GPIO_MODE_AF, SYS_GPIO_OTYPE_PP,
                 SYS_GPIO_SPEED_MID, SYS_GPIO_PUPD_NONE);
    sys_gpio_af_set(GPIOB, SYS_GPIO_PIN6, 7);
    USART1->CR1 = 0;
    USART1->BRR = (pclk_hz + 57600u) / 115200u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}

void uart_text(const char *text)
{
    while (*text != '\0') {
        while ((USART1->SR & USART_SR_TXE) == 0u) {
        }
        USART1->DR = (uint8_t)*text++;
    }
}

void uart_u32(uint32_t value)
{
    char digits[11];
    char *digit = &digits[10];
    *digit = '\0';
    do {
        *--digit = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    uart_text(digit);
}

void uart_field(uint32_t value)
{
    uart_text(",");
    uart_u32(value);
}

void bench_error(const char *scenario, const char *reason, uint32_t detail)
{
    uart_text("ERROR,");
    uart_text(scenario);
    uart_text(",");
    uart_text(reason);
    uart_field(detail);
    uart_text("\r\n");
}

static int clock_valid(void)
{
    const uint32_t bus_mask = RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2;
    const uint32_t buses = RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    const uint32_t pll_mask = RCC_PLLCFGR_PLLM | RCC_PLLCFGR_PLLN |
                              RCC_PLLCFGR_PLLP | RCC_PLLCFGR_PLLSRC | RCC_PLLCFGR_PLLQ;
    const uint32_t pll = 8u | (336u << 6) | RCC_PLLCFGR_PLLSRC_HSE | (7u << 24);
    const uint32_t flash_mask = FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN |
                                FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    const uint32_t flash = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN |
                           FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    return (RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL &&
           (RCC->CFGR & bus_mask) == buses &&
           (RCC->PLLCFGR & pll_mask) == pll &&
           (PWR->CR & PWR_CR_VOS) != 0u &&
           (FLASH->ACR & flash_mask) == flash;
}

static int cycle_counter_init(void)
{
    uint32_t start;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u) {
        return 0;
    }
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    start = DWT->CYCCNT;
    __DSB();
    __ISB();
    return DWT->CYCCNT != start;
}

int main(void)
{
    const char *const image = TOOLCHAIN "-" PROFILE;
    int clocks_ok;

    SCB->CPACR |= 0xful << 20;
    __DSB();
    __ISB();
    sys_stm32_clock_init(336, 8, 2, 7);
    clocks_ok = clock_valid();
    if (!clocks_ok) {
        /* Restore the reset bus divisors so a clock failure can be logged. */
        RCC->CFGR = 0;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
        }
    }
    uart_init(clocks_ok ? TIMER_HZ : 16000000u);
    uart_text("BEGIN,");
    uart_text(image);
    uart_field(clocks_ok ? CORE_HZ : 0u);
    uart_field(clocks_ok ? TIMER_HZ : 0u);
    uart_text("\r\n");
    uart_text("SUITE,2\r\n");
    if (!clocks_ok) {
        bench_error("board", "clock", RCC->CR);
    } else if (!cycle_counter_init()) {
        bench_error("board", "dwt", DWT->CTRL);
    } else {
        bench_algorithms();
        bench_integer();
        /* Peripheral ISRs run with the same inactive thread FP context. */
        __set_CONTROL(__get_CONTROL() & ~CONTROL_FPCA_Msk);
        __ISB();
        bench_peripherals();
        bench_coremark();
    }
    uart_text("END,");
    uart_text(image);
    uart_text("\r\n");
    for (;;) {
        /* Leave the core clocked for the next SWD reset and capture. */
        __NOP();
    }
}
