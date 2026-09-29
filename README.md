# STM32F407 嵌入式工程模板

基于 **STM32F407ZGT6（M144Z-M4 最小系统板）+ HAL 库 + FreeRTOS** 的分层嵌入式项目模板，采用「软硬件分离 + OS 抽象」设计，目标是让驱动逻辑可复用、可移植、可在 PC 上做单元测试。

> 工程由 STM32F103（正点原子）原始驱动系统性重写并完成 F1 → F4 迁移而来，git 历史记录了完整演进过程（重写 → 算法优化 → 主机测试 → 越界检测 → 线程安全 → F4 迁移 → 烧录验证）。

## 硬件资源（本板实测引脚，来源：`STM32F407/M144Z-M4最小系统板IO引脚分配表`）

| 外设 | 引脚 / 总线 | 说明 |
|------|------------|------|
| LCD | FSMC NE4（PG12）+ RS=FSMC_A6（PF12） | 8080-16 并口，驱动 IC 自动识别（NT35510 480×800 为 `0x5510`）；WR=PD5、RD=PD4；背光 **PB15** |
| 外部 SRAM | FSMC NE3（PG10） | XM8A51216V33A，512K×16 = 1MB，15ns 异步 XRAM |
| LED0 / LED1 | PF9 / PF10 | 低电平点亮 |
| KEY0 / WKUP | PE4 / PA0 | 板上仅有这两个按键（KEY1 为 PE3 空脚，外接可用） |
| 串口 | USART1：PA9/PA10（AF7） | 经跳线帽接 CH340C；DMA2_Stream7(TX)/Stream2(RX)，Channel4 |
| EEPROM | AT24C02：PB8/PB9 | 软件 IIC（`driver/IIC`） |
| 触摸 | PB0(SCL/T_CLK)、PF11(SDA/T_MOSI)、PB1(T_PEN)、PB2(T_MISO)、PC13(T_CS) | 兼容电阻（XPT2046）与电容（GT9xxx/FT5206）两类模组 |
| 时钟 | HSE 8MHz（PH0/PH1）→ PLL → 168MHz | SysTick 与 FreeRTOS 节拍 1ms |

> 注意：F1 精英板那套映射（LCD 背光 PB0、RS=A10/PG0、LED0=PB5、LED1=PE5）在本板是触摸时钟 / SRAM 地址线 / SPI Flash MOSI / DCMI 引脚，**不能沿用**。

## 特性

- **清晰分层**：`app / bsp / common / core / driver / middleware / utility / os / test / tool` 十层，职责单一
- **软硬件分离**：硬件描述（引脚 / 外设实例 / 中断号 / DMA 通道 / 复用功能号）集中在 `bsp/`，驱动层不感知具体板卡
- **OS 抽象层**：`os/os_wrapper` 屏蔽 RTOS 差异，`OS_USE_FREERTOS` 一键切换 FreeRTOS / 裸机
- **DMA 环形缓冲串口**：CPU 零拷贝接收、IDLE 帧检测、信号量唤醒、printf 与数据共用一条发送流
- **FSMC 时序按数据手册核算**：SRAM（XM8A51216V33A，15ns）与 LCD（NT35510，80 系列）的 ADDSET/DATAST 均由手册 AC 参数算出并留裕量
- **主机侧单元测试**：malloc 分配器、环形缓冲区、按键状态机等纯逻辑可在 PC 上回归
- **静态校验脚本**：函数定义覆盖、重复符号、include 路径、BSP 宏引用、F1 残留等 6 项自动检查

## 目录结构

```
├── app/            应用层：业务逻辑与 RTOS 任务，不接触底层
│   └── task/       任务实现（freertos_demo.c 为烧录验证程序）
├── bsp/            板级支持包：板卡专属硬件描述（不可移植，与板卡强绑定）
├── common/         公共层：类型定义、状态码、统一配置头
├── core/           核心层：CMSIS + HAL + system（芯片级依赖，不可移植）
├── driver/         驱动层：通用外设驱动（软硬件分离，可跨板复用）
│   ├── ADC/ IWDG/ KEY/ LED/ LCD/ SRAM/ TIMER/ delay/ usart/
│   └── IIC/ 24CXX/ TOUCH/            （EEPROM 与触摸，已入编译）
├── middleware/     中间件层：跨模块通用能力（分块内存管理 MALLOC）
├── utility/        工具层：基础工具函数（环形缓冲区 ringbuf）
├── os/             操作系统层：FreeRTOS 内核 + os_wrapper 抽象
├── test/           主机侧单元测试 + 测试桩
├── tool/           脚本工具（静态校验等）
├── STM32F407/      官方资料（芯片 / 液晶 / 板卡手册与原理图）
└── EIDE/           EIDE 工程配置（源码清单 eide.yml、Keil pack 等）
```

## 分层依赖规则

依赖方向自上而下：**app → bsp/driver/middleware/utility/os → core**。

- 上层可调用下层，下层不得反向依赖上层（如 `os` 不得 include `driver`）
- `bsp` 只放板卡相关的宏与初始化，`driver` 通过 `bsp_xxx.h` 的宏访问硬件
- 换板 / 换外设时通常只需修改 `bsp/`，驱动代码不变
- **F4 上使用复用功能的外设必须在 `HAL_GPIO_Init` 前设置 `Alternate`**（FSMC=AF12、USART1=AF7、TIM3=AF2、TIM5=AF2、TIM2=AF1、TIM1=AF1、TIM8=AF3），否则引脚停在 AF0，信号出不去

## 构建与烧录

1. 使用 **VSCode + [EIDE](https://em-ide.com/)** 打开 `EIDE/Project.code-workspace`
2. 工具链为 **ARM Compiler 5（AC5）**，目标器件 `STM32F407ZGT6`（Cortex-M4，168MHz，SoftVFP）
3. 源码清单与包含路径见 `EIDE/.eide/eide.yml`
4. 烧录默认走 J-Link（`EIDE/.eide/eide.yml` → `uploadConfigMap`）
5. 散载文件：`EIDE/build/Debug/Project.sct`（自定义，只描述片上 FLASH/RAM；外部 SRAM 由 `malloc.c` 用指针访问，不做绝对地址段）

> 外部内存池：外部 SRAM 960KB（`0x68000000~0x680EFFFF`）+ 状态表 60KB（`0x680F0000~0x680FFFFF`），正好用满 1MB；板卡无 SRAM 时 `sram_probe` 会自动跳过外部池。

## 主机侧单元测试（无需硬件）

依赖任意主机 gcc（Windows 上 mingw gcc 即可）；有 bash 时直接跑脚本：

```bash
bash test/run_host_tests.sh
```

覆盖：

| 测试 | 目标模块 | 方式 |
|------|---------|------|
| `test_malloc.c` | 分块内存分配器 | 20 万轮随机分配/释放与参考模型对拍 + 越界检测 |
| `test_ringbuf.c` | 环形缓冲区 | 回绕 / 满空判定 / 连续段 / 单字节与批量读写 |
| `test_key.c` | 按键状态机 | 短按 / 消抖 / 长按 / 连发事件的黑盒驱动 |

## 静态校验

```bash
python tool/verify_rewrite.py
```

检查：函数定义覆盖、重复 IRQ/Callback 符号、include 路径、BSP 宏引用、STM32F4 关键依赖、**F1 残留符号**（`stm32f1` / `AFIO->` / `GPIO_MODE_AF_INPUT` / `DMA1_Channelx` / `TIM8_UP_IRQn` …）。

## 代码约定

- 统一包含路径与头文件见 `common/project_config.h`
- 状态码使用 `status_t`（`common/common_types.h`）
- 非平凡函数须带 `@brief / @note / @param / @retval` 注释，`@note` 说明「为什么」
- 中断中调用 OS API 前确认优先级在 FreeRTOS 管理范围内（`configMAX_SYSCALL_INTERRUPT_PRIORITY`）

## 烧录验证记录（M144Z-M4 最小系统板 + 4.3 寸 480×800 NT35510 屏）

```
[USART1] 115200 8N1 ready
LCD ID:5510

===== Driver Verification =====
[LCD] 480x800 ID=0x5510
[SRAM] self test: PASS (0 errors)
[MALLOC] p1=0x20008624 p2=0x200085c4 p3=0x6806ffe4
[MALLOC] corrupt detect: PASS
[MALLOC] self test: PASS (0 errors, used IN=0% EX=0%)
[ADC] temp=15.4C VDDA=3.27V
UART echo ready (ring buffer + DMA)
```

逐项含义：

- `[USART1] ... ready`：PA9/PA10（AF7）+ CH340C 链路正常；该横幅为寄存器轮询直发，不依赖 DMA/中断
- `LCD ID:5510` + `[LCD] 480x800`：FSMC NE4、RS=FSMC_A6(PF12) 接线与 NT35510 读时序（ADDSET=15/DATAST=60）有效，驱动 IC 识别成功
- `[SRAM] self test: PASS`：FSMC NE3 + ADDSET=1/DATAST=3 对该 15ns XRAM 满足全部 AC 参数（覆盖 32 位/16 位/字节三条拷贝路径）
- `p3=0x6806FFE4`：单次 512KB 分配正好落在池尾（`0x6806FFE4 + 512KB = 0x680EFFE4`），证明地址线 A0~A18 全量可用、960KB 池完整
- `[MALLOC] corrupt detect / self test PASS`：越界检测与分配/释放记账正常
- `VDDA=3.27V`：由 Vrefint（典型 1.21V）反推与 3.3V 轨一致，反证 ADC 时钟分频（PCLK2/4 = 21MHz）与采样配置正确
- `temp=15.4C`：F4 内部温度传感器绝对精度仅 ±45°C（数据手册），此值只作趋势参考
- `UART echo ready`：IDLE 中断 + DMA2_Stream2 环形接收就绪（PC 发送数据可原样回显）

## 已知待改进

- 触摸（`driver/TOUCH`）与 EEPROM（`driver/24CXX`）已入编译，但尚未接入 `bsp_init`，需要时调用 `tp_init()` / `at24cxx_init()` 即可
- LCD 读时序的 `tRDHFM`（RDX 高电平 ≥250ns）受模式 A 的 ADDSET≤15 限制只能做到 ~101ns，与正点原子官方例程一致；若某块屏读点异常，可在两次读之间插入短延时
- 栈溢出检测 `configCHECK_FOR_STACK_OVERFLOW` 当前为 0，量产前建议开启
- 主机测试尚未接入 CI，建议加 GitHub Actions / 本地 git hook
