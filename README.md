# STM32F407 嵌入式工程模板

基于 **STM32F407ZGT6（M144Z-M4 最小系统板）+ HAL 库 + FreeRTOS** 的分层嵌入式项目模板，采用「软硬件分离 + OS 抽象」设计，目标是让驱动逻辑可复用、可移植、可在 PC 上做单元测试。

> 工程由 STM32F103 原始驱动系统性重写并完成 F1 → F4 迁移而来，git 历史记录了完整演进过程（重写 → 算法优化 → 主机测试 → 越界检测 → 线程安全 → F4 迁移 → 烧录验证）。

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

> 注意：F1 板卡那套映射（LCD 背光 PB0、RS=A10/PG0、LED0=PB5、LED1=PE5）在本板是触摸时钟 / SRAM 地址线 / SPI Flash MOSI / DCMI 引脚，**不能沿用**。

## 特性

- **清晰分层**：`app / bsp / common / core / driver / middleware / utility / os / test / tool` 十层，职责单一
- **软硬件分离**：硬件描述（引脚 / 外设实例 / 中断号 / DMA 通道 / 复用功能号）集中在 `bsp/`，驱动层不感知具体板卡
- **OS 抽象层**：`os/os_wrapper` 屏蔽 RTOS 差异，`OS_USE_FREERTOS` 一键切换 FreeRTOS / 裸机
- **DMA 环形缓冲串口**：CPU 零拷贝接收、IDLE 帧检测、信号量唤醒、printf 与数据共用一条发送流
- **FSMC 时序按数据手册核算**：SRAM（XM8A51216V33A，15ns）与 LCD（NT35510，80 系列）的 ADDSET/DATAST 均由手册 AC 参数算出并留裕量
- **LVGL 图形库**：LVGL 8.2.0 由独立任务驱动（`lv_timer_handler` 按需休眠），适配层（显示 / 触摸 / 验证界面）为工程自有代码；绘图缓冲放外部 SRAM 并按面板实际分辨率自适应
- **主机侧单元测试**：malloc 分配器、环形缓冲区、按键状态机等纯逻辑可在 PC 上回归（bash / PowerShell 双入口）
- **静态校验脚本**：函数定义覆盖、重复符号、include 路径、BSP 宏引用、F4 关键依赖、F1 残留、厂商标记残留 共 7 项自动检查，带退出码可直接接入 CI
- **厂商无关**：源码与文档中的原厂商标 / 站点 / 产品名标记已全部清除，`verify_rewrite.py` 第 7 项会持续拦截回归

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
├── middleware/     中间件层：跨模块通用能力
│   ├── MALLOC/     分块内存管理（内部/外部 SRAM 内存池）
│   ├── LVGL/       LVGL 8.2.0 图形库 + GUI_APP（lv_mainstart 屏上验证界面）
│   └── XMRAM/      外部 SRAM 器件库（XMRAM.lib）
├── utility/        工具层：基础工具函数（环形缓冲区 ringbuf）
├── os/             操作系统层：FreeRTOS 内核 + os_wrapper 抽象
├── test/           主机侧单元测试 + 测试桩（bash / PowerShell 两个入口）
├── tool/           脚本工具（静态校验等）
├── STM32F407/      官方资料（芯片 / 液晶 / 板卡手册与原理图）
└── EIDE/           EIDE 工程配置（源码清单 eide.yml、Keil pack 等）
```

## 分层依赖规则

依赖方向自上而下：**app → bsp/driver/middleware/utility/os → core**。

- 上层可调用下层，下层不得反向依赖上层（如 `os` 不得 include `driver`）
- `bsp` 只放板卡相关的宏与初始化，`driver` 通过 `bsp_xxx.h` 的宏访问硬件
- 换板 / 换外设时通常只需修改 `bsp/`，驱动代码不变
- `middleware/LVGL` 属第三方代码，其内部包含约定自成一套，不参与 include 自检；**只有** `lv_conf.h` 与 `examples/porting/lv_port_*.c` 是本工程需要维护的适配层
- **F4 上使用复用功能的外设必须在 `HAL_GPIO_Init` 前设置 `Alternate`**（FSMC=AF12、USART1=AF7、TIM3=AF2、TIM5=AF2、TIM2=AF1、TIM1=AF1、TIM8=AF3），否则引脚停在 AF0，信号出不去
- **中断优先级不得高于 5**（`configMAX_SYSCALL_INTERRUPT_PRIORITY`）：高于它的中断不会被 `os_enter_critical()` 屏蔽，会破坏临界区原子性；需要用 FreeRTOS 的 `FromISR` API 的中断必须落在 5~15

## 构建与烧录

1. 使用 **VSCode + [EIDE](https://em-ide.com/)** 打开 `EIDE/Project.code-workspace`
2. 工具链为 **ARM Compiler 5（AC5）**，目标器件 `STM32F407ZGT6`（Cortex-M4，168MHz，SoftVFP）
3. 源码清单与包含路径见 `EIDE/.eide/eide.yml`
4. 烧录默认走 J-Link（`EIDE/.eide/eide.yml` → `uploadConfigMap`）
5. 散载文件：`EIDE/build/Debug/Project.sct`（自定义，只描述片上 FLASH/RAM；外部 SRAM 由 `malloc.c` 用指针访问，不做绝对地址段）

> 外部内存池：外部 SRAM 960KB（`0x68000000~0x680EFFFF`）+ 状态表 60KB（`0x680F0000~0x680FFFFF`），正好用满 1MB；板卡无 SRAM 时 `sram_probe` 会自动跳过外部池。

## 主机侧单元测试（无需硬件）

依赖任意主机 gcc（Windows 上 mingw gcc 即可）。有 bash：

```bash
bash test/run_host_tests.sh
```

Windows 上没有 bash 时用批处理版（不受 PowerShell 执行策略限制）：

```bat
test\run_host_tests.cmd
```

覆盖：

| 测试 | 目标模块 | 方式 |
|------|---------|------|
| `test_malloc.c` | 分块内存分配器 | 20 万轮随机分配/释放与参考模型对拍 + 越界检测 |
| `test_ringbuf.c` | 环形缓冲区 | 回绕 / 满空判定 / 连续段 / 单字节与批量读写 |
| `test_key.c` | 按键状态机 | 短按 / 消抖 / 长按 / 连发事件的黑盒驱动，含多事件不丢失 |

## 静态校验

```bash
python tool/verify_rewrite.py
```

退出码 0 = 全部通过，1 = 存在未通过项（可直接接入 CI）。检查 7 项：

1. 头文件 API 差异（与 git HEAD 对比，仅作信息展示，不参与结论）
2. 函数定义覆盖（22 个驱动/BSP/工具头文件声明的函数都必须有定义）
3. 重复 IRQ / HAL 回调符号
4. include 路径可解析（工程自有代码；第三方 LVGL 子树不参与）
5. BSP 宏引用（驱动引用的硬件宏必须已定义）
6. STM32F4 关键依赖 + **F1 残留符号**（`stm32f1` / `AFIO->` / `GPIO_MODE_AF_INPUT` / `DMA1_Channelx` / `TIM8_UP_IRQn` …）
7. **厂商标记残留**（源码与 README 中不得出现原厂商标 / 站点 / 产品名）

## 代码约定

- 统一包含路径与头文件见 `common/project_config.h`
- 状态码使用 `status_t`（`common/common_types.h`），驱动接口把失败原因向上传递，不静默吞掉
- 非平凡函数须带 `@brief / @note / @param / @retval` 注释，`@note` 说明「为什么」
- **`os_enter_critical()` / `os_exit_critical()` 只能在任务上下文调用**。中断里一律用 `..._from_isr` 系列（信号量/队列）。FreeRTOS 的 `taskENTER_CRITICAL()` 最终走到 `vPortEnterCritical()`，其中有一条 `configASSERT`（`port.c` 检查 ICSR 的 `VECTACTIVE`）断言"当前不在异常处理中"，从 ISR 调用必然触发
- 既要被任务调用、又要被中断调用的函数，**把加锁与干活拆开**：加锁版只给任务用，中断里直接调用不加锁的内核版（例：`driver/usart/usart.c` 的 `usart1TxKick()` / `usart1TxStartDma()`）。中断内不加锁的前提是"该中断优先级不高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`"——这样任务的临界区(BASEPRI)能把它挡住，中断就不可能看到半更新的状态；同时中断不会被任务抢占，同级中断之间也不互相抢占
- 中断中调用 OS API 前确认优先级在 FreeRTOS 管理范围内（`configMAX_SYSCALL_INTERRUPT_PRIORITY`）
- 带 `?:` 的 IO 宏必须给参数加括号（`do{ (x) ? A : B; }while(0)`），否则 `T_MOSI(a ? 1 : 0)` 会因 `?:` 右结合而改变语义
- Cortex-M4F 只有单精度 FPU：驱动内避免 `double`（会退化到软件浮点），几何/校准运算统一用 `float` 或整数
- 换行统一 LF；第三方厂商代码不参与规范化（见 `.gitattributes`）

### 断言（`configASSERT`）

断言输出走 `vAssertCalled()`（实现在 `os/os_wrapper/os_wrapper.c`），**只打印第一次**，后续只累加 `g_assertCount`，
并记录首次/最近一次的 `__FILE__` 与 `__LINE__`。这样设计是因为原实现（宏里直接 `printf`）在中断中触发断言时会
形成"断言 → 打印 → 触发 TX 中断 → 断言"的自激刷屏，把正常日志全部冲掉。串口上看到

```
Error: .\..\os\FreeRTOS\portable\RVDS\ARM_CM3\port.c, 396
```

的含义是：**在中断里调用了 `taskENTER_CRITICAL`/`os_enter_critical`**（`port.c:396` 就是那条断言）。

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

> 上表是**未接入 LVGL 时**的实测记录。接入 LVGL 任务后串口会多出两行
> （`[LVGL] 800x480 disp+indev ready` 与 `[LVGL] UI ready`），屏上则是上文描述的验证界面。
> 这两行属于预期输出，**尚未在硬件上实测**；若 `lv_port_disp_init()` 申请不到外部 SRAM 缓冲，
> 会打印 `LVGL: external SRAM draw buffer alloc failed (N bytes)` 并跳过界面（不会死机）。

## LVGL 集成状态

`middleware/LVGL` 已完整入编译（LVGL 8.2.0 + `GUI_APP` 应用层），适配层是本工程自己的代码：

| 文件 | 作用 |
|------|------|
| `GUI/lvgl/lv_conf.h` | LVGL 配置：RGB565、46KB 内存池、tick 走 `os_wrapper` |
| `GUI/lvgl/examples/porting/lv_port_disp_template.c` | 显示适配：绘图缓冲取自**外部 SRAM**（按 `lcddev` 实际分辨率整屏），`flush_cb` 走 `lcd_set_window` + 连续写 RAM |
| `GUI/lvgl/examples/porting/lv_port_indev_template.c` | 触摸适配：注册 `tp_dev.scan`，电阻屏可在初始化阶段按住 KEY0 强制重新校准 |
| `GUI_APP/demos/lv_mainstart.c` | 屏上验证界面（见下） |

### LVGL 任务（`app/task/freertos_demo.c`）

`lvgl_task` 在自检之后创建（此时外部池里 512KB 的大块测试分配已归还），初始化顺序为
`lv_init() → lv_port_disp_init() → lv_port_indev_init() → lv_mainstart()`，随后循环
`lv_timer_handler()` 并按它的返回值休眠（夹在 1~20ms）。**只有本任务访问 LVGL**，所以不需要加锁；
将来若要从其它任务改界面，必须用 `lv_async_call` 投递过来。

界面层不 include 任何驱动头文件：需要点灯之类的硬件动作时由应用层实现弱钩子
`lv_ui_led_switch_hook(bool on)`（`freertos_demo.c` 已给出实现，把界面开关映射到 LED0）。
这与 `usart1RxIdleHook()` 是同一套模式——弱属性只加在定义上，声明处不加。

### 屏上验证界面（`lv_mainstart.c`）

按面板分辨率自适应布局（480×320 与 800×480 都能用），自上而下逐项验证一条链路：

| 区域 | 验证内容 | 异常时的典型含义 |
|------|---------|-----------------|
| 标题栏 `LVGL Self-Test` + `800x480 disp:1 indev:1` | 文本渲染；右侧分辨率与已注册设备数 | 分辨率不对 → 移植层没拿到 `lcddev`；`indev:0` → 触摸没注册上 |
| 8 色色带（红绿蓝青品红黄白黑） | RGB565 颜色映射与矩形填充 | 红蓝互换 → `LV_COLOR_16_SWAP` 或 LCD 字节序配置不匹配 |
| 14/16/20/24 四档字号 | 字库取模与列步进 | 某档错乱 → 该字号的每列字节数/行数算错 |
| `SLIDER` + 数值 | 触摸 → 命中测试 → 事件 → 文本刷新 | 滑块能动而数字不变 → 事件回调没触发 |
| `LED0 SWITCH` + ON/OFF | 控件状态 + UI→应用层钩子（真实点灯） | 灯不亮 → 应用层钩子没覆盖或 LED 驱动异常 |
| `TOGGLE OVERLAY` 按钮 | 点击事件、控件的层叠与隐藏标志 | 浮层不出现 → 事件或 `LV_OBJ_FLAG_HIDDEN` 处理异常 |
| `TIMER BAR` + 百分比 | `lv_timer_handler` 周期驱动 | 进度条卡住 → `lv_tick` 源或 handler 没被周期调用 |
| 状态区 `UPTIME`/`TOUCH`/呼吸灯 | 运行时间、实时触摸坐标、动画引擎 | 秒数不走 → tick 源没接通；坐标不动 → 输入设备读取异常 |

右下角的 **FPS/CPU 浮层**由 `LV_USE_PERF_MONITOR` 提供，用来观察刷新性能。

> 触摸坐标只挂在屏幕对象上，所以只有按在**控件之外的空白处**才会更新——按到控件上会被控件
> 消费掉，这同时验证了命中测试。行容器刻意清掉了 `LV_OBJ_FLAG_CLICKABLE`
> （`lv_obj` 默认是可点击的），否则按在行背景上的触摸会被行容器吃掉。

### 资源占用（当前实测）

| 项 | 占用 | 备注 |
|----|------|------|
| ROM | 329.7KB / 1024KB | LVGL 约占 291KB |
| RAM | 104.4KB / 128KB | 其中 LVGL 内存池 `LV_MEM_SIZE` = 46KB、FreeRTOS 堆 16KB |
| 外部 SRAM | 750KB / 960KB | LVGL 整屏绘图缓冲（800×480×2），其余留给应用 |

## 已知待改进

- 触摸（`driver/TOUCH`）与 EEPROM（`driver/24CXX`）已入编译，但尚未接入 `bsp_init`；触摸现已由 LVGL 任务在运行时调用 `tp_init()`，EEPROM 需要时调用 `at24cxx_init()` 即可
- LVGL 整屏绘图缓冲占掉外部池的 750KB/960KB（78%）。若还要在外部 SRAM 里做大块应用数据，应把 `USE_SRAM` 改为 0 走内部 SRAM 的 30 行缓冲，或把缓冲改成部分刷新（更省内存，代价是可能出现撕裂）
- LVGL 内存池 46KB 对当前界面偏宽裕（实际约 8KB）；若要压缩 RAM，可从 `LV_MEM_SIZE` 里回收 20~30KB
- **ADC 未做自校准**：本工程使用的这份 F4 HAL 既没有 `HAL_ADCEx_Calibration_Start()`，`HAL_ADC_Init()` 内部也不做校准。当前只用于内部温度/Vrefint 这类粗测量（温度传感器绝对精度本身仅 ±45°C）故影响可忽略；若要用 ADC 做精密测量，需按 RM0090 §11.13 补：`ADON=1 → 等 tSTAB → CAL=1 → 等 CAL 被硬件清 0`
- `my_mem_perused()` 每次都全量扫描状态表（外部池 30720 项）且在临界区内完成，属于 O(表项数) 操作；目前只在自检与打印时调用，高频使用前建议改为在分配/释放处维护已用块计数
- `usart1SendBytes()` 用 `systickDelay_us` 忙等排空（为兼容调度器启动前调用），任务上下文下更合适的是等 TX 完成信号量
- `bsp_timer.h` 中 TIM5 同时被 btim（按键扫描）与 gtim（输入捕获）使用，二者互斥目前只靠注释约束，建议改成宏开关 + 编译期报错
- LCD 读时序的 `tRDHFM`（RDX 高电平 ≥250ns）受模式 A 的 ADDSET≤15 限制只能做到 ~101ns，与屏厂例程一致；若某块屏读点异常，可在两次读之间插入短延时
- 界面文字全为 ASCII：`LV_FONT_SIMSUN_16_CJK` 当前为 0。要显示中文需打开该宏（或换自带的中文字库），会显著增加 Flash 占用
- 栈溢出检测 `configCHECK_FOR_STACK_OVERFLOW` 当前为 0，量产前建议开启；LVGL 任务栈 4KB 是按官方示例量级给的，建议开启后实测水位再定
- 主机测试与静态校验尚未接入 CI，建议加 GitHub Actions / 本地 git hook（`verify_rewrite.py` 已返回退出码）
