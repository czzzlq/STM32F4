CMSIS：
Device：
	->stm32f1xx.h：F1系列的顶层头文件，通过条件编译来包含某个芯片的头文件，定义通用的枚举类型，定义通用的宏定义
	->stm32f103xe.h：包含中断编号定义，外设寄存器结构体类型定义，寄存器映射寄存器位定义，外设判定。
	->system_stm32f1xx.c
	->system_stm32f1xx.h：定义了系统初始化函数SystemInit()和系统时钟更新函数SystemCoreClockUpdate()
	->startup_stm32f103xe.s：大容量F103系列芯片的启动文件
Include:
	->cmsis_armcc.h：适配ARM 编译器（armcc） 的 CMSIS 头文件，定义该编译器专属的宏、数据类型（如内联标识）、内联汇编等，让代码能利用 armcc 的特性。
	->cmsis_armclang.h：适配ARM Compiler 6（armclang） 的 CMSIS 头文件，提供寄存器操作（如控制寄存器、栈指针）、中断 / 异常相关的内联函数，是 armclang 编译环境下的底层接口。
	->cmsis_compiler.h：CMSIS 的编译器抽象层头文件，统一不同编译器（armcc、GCC、IAR 等）的接口，屏蔽编译器差异，让代码能跨工具链兼容。
	->cmsis_version.h：记录当前CMSIS 库的版本信息，用于版本管理、组件兼容性校验（避免不同版本组件冲突）。
	->core_cm3.h：Cortex-M3 内核专属头文件，包含该内核的寄存器定义、异常处理、NVIC（嵌套中断控制器）、SysTick 定时器等底层配置接口，是 STM32 等 Cortex-M3 芯片开发的基础。
	->mpu_armv7.h：ARMv7 架构（如 Cortex-M4/R5）的内存保护单元（MPU） 头文件，提供 MPU 区域配置、使能 / 禁用等 API，用于管理内存的访问权限、缓存属性。