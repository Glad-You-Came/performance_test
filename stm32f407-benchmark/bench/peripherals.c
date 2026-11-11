#include "bench.h"

enum {
    PERIOD_TICKS = TIMER_HZ / 10000,
    PERIOD_CYCLES = CORE_HZ / 10000,
    PWM_COMPARE = PERIOD_TICKS / 2,
    IRQ_WARMUP = 16,
    IRQ_SAMPLES = 1024,
    DMA_SAMPLES = 128,
    DMA_WORDS = 4096,
    IRQ_PRIORITY = 5
};

#define DMA_STREAM DMA2_Stream0
#define DMA_ERRORS (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)
#define DMA_CLEAR (DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 | \
                   DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0)

typedef struct {
    uint32_t latency;
    uint32_t body;
    uint32_t stamp;
    uint32_t dma_active;
} irq_sample;

/* Named objects also make the measurement storage visible in the linker maps. */
volatile irq_sample irq_samples[IRQ_SAMPLES];
uint32_t dma_source[DMA_WORDS];
volatile uint32_t dma_target[DMA_WORDS];

static volatile uint32_t observed;
static volatile uint32_t timer_missed;
static volatile uint32_t dma_started, dma_flags, dma_finished;

static void save_irq(uint32_t latency, uint32_t body, uint32_t stamp, uint32_t active)
{
    uint32_t index = observed;
    if (index >= IRQ_WARMUP + IRQ_SAMPLES) {
        return;
    }
    if (index >= IRQ_WARMUP) {
        volatile irq_sample *sample = &irq_samples[index - IRQ_WARMUP];
        sample->latency = latency;
        sample->body = body;
        sample->stamp = stamp;
        sample->dma_active = active;
    }
    observed = index + 1;
}

void TIM2_IRQHandler(void)
{
    uint32_t entered = TIM2->CNT;
    uint32_t start = DWT->CYCCNT;
    uint32_t active = DMA_STREAM->CR & DMA_SxCR_EN;
    uint32_t target = TIM2->CCR1;
    uint32_t elapsed = entered - target;
    uint32_t missed = elapsed / PERIOD_TICKS;
    uint32_t stop;

    TIM2->SR = (uint32_t)~TIM_SR_CC1IF;
    TIM2->CCR1 = target + (missed + 1u) * PERIOD_TICKS;
    __DSB();
    stop = DWT->CYCCNT;
    if (observed >= IRQ_WARMUP && observed < IRQ_WARMUP + IRQ_SAMPLES) {
        timer_missed += missed;
    }
    save_irq(elapsed, stop - start, start, active);
}

void EXTI4_IRQHandler(void)
{
    uint32_t entered = TIM2->CNT;
    uint32_t start = DWT->CYCCNT;
    uint32_t stop;

    EXTI->PR = EXTI_PR_PR4;
    __DSB();
    stop = DWT->CYCCNT;
    save_irq((entered + PERIOD_TICKS - PWM_COMPARE) % PERIOD_TICKS,
             stop - start, start, 0);
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t entered = DWT->CYCCNT;
    uint32_t flags = DMA2->LISR;
    uint32_t stop;

    DMA2->LIFCR = DMA_CLEAR;
    __DSB();
    stop = DWT->CYCCNT;
    dma_flags = flags;
    irq_samples[observed].latency = entered - dma_started;
    irq_samples[observed].body = stop - entered;
    irq_samples[observed].stamp = entered;
    irq_samples[observed].dma_active = 0;
    dma_finished = 1;
}

static void enable_irq(IRQn_Type irq)
{
    NVIC_ClearPendingIRQ(irq);
    NVIC_SetPriority(irq, IRQ_PRIORITY);
    NVIC_EnableIRQ(irq);
}

static void dma_configure(int interrupt)
{
    uint32_t control = DMA_SxCR_DIR_1 | DMA_SxCR_PINC | DMA_SxCR_MINC |
                       DMA_SxCR_PSIZE_1 | DMA_SxCR_MSIZE_1;
    if (interrupt) {
        control |= DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    }
    DMA_STREAM->CR = control;
    DMA_STREAM->FCR = DMA_SxFCR_DMDIS | DMA_SxFCR_FTH |
                      (interrupt ? DMA_SxFCR_FEIE : 0u);
    DMA_STREAM->PAR = (uint32_t)(uintptr_t)dma_source;
    DMA_STREAM->M0AR = (uint32_t)(uintptr_t)dma_target;
}

static void dma_start(void)
{
    uint32_t control = DMA_STREAM->CR | DMA_SxCR_EN;
    DMA2->LIFCR = DMA_CLEAR;
    DMA_STREAM->NDTR = DMA_WORDS;
    __DSB();
    dma_started = DWT->CYCCNT;
    DMA_STREAM->CR = control;
}

static int dma_wait_idle(void)
{
    uint32_t start = DWT->CYCCNT;
    while ((DMA_STREAM->CR & DMA_SxCR_EN) != 0u) {
        if (DWT->CYCCNT - start >= CORE_HZ / 50u) {
            return 0;
        }
    }
    return 1;
}

static int dma_valid(void)
{
    unsigned i;
    for (i = 0; i < DMA_WORDS; ++i) {
        if (dma_target[i] != dma_source[i]) {
            return 0;
        }
    }
    return 1;
}

static void emit_samples(const char *scenario, uint32_t count, uint32_t missed)
{
    uint32_t i, active = 0;
    for (i = 0; i < count; ++i) {
        const volatile irq_sample *sample = &irq_samples[i];
        uart_text("IRQ,");
        uart_text(scenario);
        uart_field(i);
        uart_field(sample->latency);
        uart_field(sample->body);
        uart_field(sample->stamp);
        uart_field(sample->dma_active);
        uart_text("\r\n");
        active += sample->dma_active;
    }
    uart_text("STATUS,");
    uart_text(scenario);
    uart_field(count);
    uart_field(missed);
    uart_field(active);
    uart_text("\r\n");
}

static void measure_dma(void)
{
    uint32_t count, i;
    uint32_t detail = 0;
    const char *error = 0;

    dma_configure(1);
    enable_irq(DMA2_Stream0_IRQn);
    for (count = 0; count < DMA_SAMPLES; ++count) {
        for (i = 0; i < DMA_WORDS; ++i) {
            dma_target[i] = ~dma_source[i];
        }
        observed = count;
        dma_finished = 0;
        dma_start();
        while (!dma_finished && DWT->CYCCNT - dma_started < CORE_HZ / 50u) {
        }
        if (!dma_finished) {
            error = "timeout";
            detail = count;
            break;
        }
        if ((dma_flags & (DMA_ERRORS | DMA_LISR_TCIF0)) != DMA_LISR_TCIF0 ||
            !dma_wait_idle() || !dma_valid()) {
            error = "transfer";
            detail = dma_flags;
            break;
        }
    }
    NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    DMA_STREAM->CR &= ~DMA_SxCR_EN;
    if (!dma_wait_idle()) {
        error = "dma-busy";
        detail = count;
    }
    DMA2->LIFCR = DMA_CLEAR;
    if (error) {
        bench_error("dma", error, detail);
    }
    emit_samples("dma", count, 0);
}

static void timer_configure(int external)
{
    TIM2->CR1 = 0;
    TIM2->DIER = 0;
    TIM2->CCER = 0;
    TIM2->PSC = 0;
    TIM2->ARR = external ? PERIOD_TICKS - 1u : UINT32_MAX;
    TIM2->CCR1 = external ? PWM_COMPARE : PERIOD_TICKS;
    /* PWM mode 2 raises CH1 at CCR1 and lowers it at rollover. */
    TIM2->CCMR1 = external ? (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 |
                              TIM_CCMR1_OC1M_0) : 0u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    if (external) {
        /* DMF407 JP5: remove both ADC shunts and link PA5 (4) to PA4 (2). */
        GPIOA->MODER &= ~(0xfu << 8);
        GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xfu << 20)) | (1u << 20);
        GPIOA->PUPDR = (GPIOA->PUPDR & ~(0xfu << 8)) | (2u << 8);
        GPIOA->OTYPER &= ~(1u << 5);
        SYSCFG->EXTICR[1] &= ~0xfu;
        EXTI->FTSR &= ~EXTI_FTSR_TR4;
        EXTI->RTSR |= EXTI_RTSR_TR4;
        TIM2->CCER = TIM_CCER_CC1E;
        GPIOA->MODER |= 2u << 10;
        EXTI->PR = EXTI_PR_PR4;
        EXTI->IMR |= EXTI_IMR_MR4;
    } else {
        TIM2->DIER = TIM_DIER_CC1IE;
    }
}

static void measure_timer(const char *name, int external, int dma_load)
{
    IRQn_Type irq = external ? EXTI4_IRQn : TIM2_IRQn;
    uint32_t start, count;
    uint32_t transfer_flags = 0;
    int idle = 1;

    observed = 0;
    timer_missed = 0;
    timer_configure(external);
    if (dma_load) {
        idle = dma_wait_idle();
        if (idle) {
            dma_configure(0);
            dma_target[DMA_WORDS - 1u] = ~dma_source[DMA_WORDS - 1u];
            dma_start();
        }
    }
    if (idle) {
        enable_irq(irq);
        TIM2->CR1 = TIM_CR1_CEN;
    }
    start = DWT->CYCCNT;
    while (idle && observed < IRQ_WARMUP + IRQ_SAMPLES &&
           DWT->CYCCNT - start < CORE_HZ / 4u) {
        if (dma_load && (DMA_STREAM->CR & DMA_SxCR_EN) == 0u) {
            transfer_flags = DMA2->LISR;
            if ((transfer_flags & (DMA_ERRORS | DMA_LISR_TCIF0)) != DMA_LISR_TCIF0) {
                break;
            }
            dma_start();
        }
    }
    NVIC_DisableIRQ(irq);
    TIM2->CR1 = 0;
    TIM2->DIER = 0;
    TIM2->CCER = 0;
    if (external) {
        EXTI->IMR &= ~EXTI_IMR_MR4;
        EXTI->PR = EXTI_PR_PR4;
        GPIOA->MODER &= ~(3u << 10);
    }
    if (!idle) {
        bench_error(name, "dma-busy", 0);
    } else if (dma_load && (!dma_wait_idle() ||
                     ((transfer_flags | DMA2->LISR) & DMA_ERRORS) != 0u ||
                     !dma_valid())) {
        bench_error(name, "dma-transfer", transfer_flags | DMA2->LISR);
    }
    count = observed > IRQ_WARMUP ? observed - IRQ_WARMUP : 0u;
    if (count != IRQ_SAMPLES) {
        bench_error(name, "incomplete", count);
    }
    emit_samples(name, count, timer_missed);
}

void bench_peripherals(void)
{
    unsigned i;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_DMA2EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    for (i = 0; i < DMA_WORDS; ++i) {
        dma_source[i] = i * 0x9e3779b9u + 0xa5a55a5au;
    }
    measure_timer("tim2", 0, 0);
    measure_timer("exti", 1, 0);
    measure_dma();
    measure_timer("tim2_dma", 0, 1);
}
