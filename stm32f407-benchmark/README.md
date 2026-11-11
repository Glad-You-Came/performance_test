# STM32F407IGT6 ARMCC 与 GCC 基准

原子 F407 串口示例的副本位于 `User`、`Drivers` 和 `Projects`。在 µVision 中打开 `Projects/MDK-ARM/atk_f407.uvprojx` 可构建原串口 `Template`。同目录的 `benchmark.uvprojx` 包含 `Benchmark-speed` 和 `Benchmark-size`。采集脚本使用命令行构建产物。

## 构建

需要 PowerShell、ARMCC 5.06 update 7 build 960、Arm GNU Toolchain 14.3.Rel1（Windows、`arm-none-eabi`）和 Node.js 20 或更高版本。GCC 可以放在 PATH、通过 `-GccBin` 指定，或从官方 ZIP 解压到项目的 `.tools` 下。解压后应存在 `.tools/arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gcc.exe`。

```powershell
./build.ps1
# 按实际安装位置指定工具链
./build.ps1 -ArmBin D:/Keil_v5/ARM/ARM_Compiler_5.06u7/bin -GccBin D:/arm-gnu/bin
node tests/report.test.cjs
node tests/isa.test.cjs
node tools/report.cjs
```

四版固件位于 `build/<image>`，包含 `.bin`、`.elf`、`.map`、`.asm`、编译命令和版本记录。速度档为 ARMCC `-O2 -Otime` 与 GCC `-O2`，体积档为 ARMCC `-O2 -Ospace` 与 GCC `-Os`。两边使用 hard-float 和单精度硬件 FPU。构建报告会检查 BIN 的栈顶、复位和三条 IRQ 向量，以及 DMA 和采样缓冲区的实际 SRAM1 地址。

GCC 启动文件来自 Keil STM32F4xx DFP 2.8.0 中 ST 的 STM324xG_EVAL TrueSTUDIO 模板。该基准使用纯 C 和 freestanding 链接，启动汇编保留数据复制、BSS 清零和 `SystemInit`，省略 C++ 构造器入口。GCC 所需的 CMSIS 编译器头文件来自工作区现有 Arm CMSIS CM4 副本。原示例源码和供应商文件保留原版权声明。

## 接线与采集

- 确认目标板使用 8 MHz HSE，正常供电。按探针定义连接 SWDIO、SWCLK、VTref、GND，建议接 NRST。
- 断电后找到丝印 `DAC`、`JP5` 的 2×2 排针，取下原有的两个跳帽。用一根跳线竖接丝印 `PA5`（第 4 脚，TIM2_CH1 输出）和 `PA4`（第 2 脚，EXTI4 输入）。`PM2_VBUS` 和 `PM2_AMPW` 一列留空，以隔离电机采样电路。首次上板确认 PA4 收到约 0–3.3 V、周期 100 µs 的信号。恢复电机测试前，断电拔下回环跳线并复原两个横向跳帽。
- 将 PB6 接到 3.3 V UART 转接器的 RX，接公共 GND。串口使用 115200 8N1。关闭占用该 COM 口的串口监视器。

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
./capture.ps1 -Port COM5
# 有多只探针时指定序列号。也可仅重测一版。
./capture.ps1 -Port COM5 -ProbeSerial 123456789 -Images gcc-speed
# 每版只采一轮，用于快速比较
./capture.ps1 -Port COM5 -RunNumbers 1
# 重新生成某一轮的报告
node tools/report.cjs runs/20261111-120000-000
```

采集脚本为每版烧录并校验一次，默认执行三次复位。每轮默认保持 J-Link 会话 90 秒，覆盖串口输出和两次 CoreMark 运行。脚本保存串口日志、固件 SHA-256、J-Link 日志和失败原因，各轮保存在独立目录。报告更新到 `build/comparison.md`。通过板载四针 CN23 连接时，J-Link 无法控制 NRST，必要时用板上的 RESET 键复位。

## 指标口径

- CoreMark 1.0 使用 EEMBC 上游源码（提交 `1f483d5b8316753a742cbf5590caf5bd0a4e4777`），共用单线程裸机移植和 SRAM1 里的 2000 字节缓冲区。两套种子各执行 10000 次，均须通过上游 CRC 校验且计时至少 10 秒。分数以 TIM5 的 1 MHz 原始 tick 计算，串口输出在计时区间外。 [EEMBC 运行规则](https://github.com/eembc/coremark/tree/1f483d5b8316753a742cbf5590caf5bd0a4e4777)
- 新增 16×16 整数矩阵乘法、欧几里得 GCD，以及 32 位乘加、32×32→64 位乘加、有符号除法和无符号商余数测试。每项固定输入，校验在计时区间外。算术批次单位是整个输入数组的一次调用，包含访存、循环和调用开销。
- 两边用 Cortex-M4/ARMv7E-M 的 Thumb、DSP、32 位整数硬件除法、单精度 FPU 和 hard-float ABI。构建时检查 ARMCC 的 `__TARGET_FEATURE_DIVIDE`、`__TARGET_FEATURE_DSPMUL` 以及 GCC 的 `__ARM_FEATURE_IDIV`、`__ARM_FEATURE_DSP`。再从实际链接产物逐个核对乘除法内核，生成 `isa.json`，出现软件除法辅助函数或缺少预期硬件指令时构建失败。

- 算法耗时是每批 32 次调用的平均周期，含循环和调用开销。CRC 输入为 1024 字节，使用预先生成的参考校验值。整数 FIR 与浮点 FIR 各校验 256 个输出。两种 FIR 的系数和输入取自同一组整数。
- TIM2 每 100 µs 产生一次比较事件。EXTI 使用 TIM2 PWM 的上升沿经 JP5 的 PA5 和 PA4 回环。响应区间从硬件比较事件到 ISR 读取 TIM2 计数器，包含 GPIO 路径、异常进入、ISR 序言及首次寄存器读取。各版用同一根跳线，周期间隔检查可发现整周期漏触发。响应分辨率为一个 TIM2 tick，约 11.9 ns。
- 处理耗时从 ISR 内首次 DWT 采样到清标志和必要寄存器更新完成，含测量窗口内的读写及屏障。异常返回和日志记录位于窗口之后。抖动为相邻 ISR 首次采样间隔相对 100 µs 的绝对偏差，漏触发依据周期间隔和 TIM2 比较重排计算，采集提前结束单列缺少的样本数。
- DMA 行中的首列耗时从写入启动寄存器前到完成 ISR 首次采样，包含传输和异常进入。TIM2 DMA 负载行使用同一段 ISR，通过采样时的 DMA 使能位记录总线负载覆盖情况。
- Flash 按从 `0x08000000` 开始的 BIN 字节数计算。静态 RAM 按链接占用的 SRAM1 地址跨度计算，包含对齐、1 KiB 栈、32 KiB DMA 缓冲区和 16 KiB IRQ 采样缓冲区。实测最大值对应当前板卡、配置和采样量。
