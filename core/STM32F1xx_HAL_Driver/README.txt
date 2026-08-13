//>> HAL库文件<<//
stm32f1xx_hal.c / stm32f1xx_hal.h
	|->实现 / 声明 HAL 库的基础功能，包括 HAL 库初始化、系统滴答配置、HAL 库延时等核心通用函数。
stm32f1xx_hal_conf.h
	|->HAL 库的用户配置文件，可用于裁剪 HAL 库功能（关闭不需要的外设驱动）、配置晶振参数等自定义设置。
stm32f1xx_hal_def.h
	|->定义 HAL 库通用的枚举类型、宏定义等基础数据类型，为 HAL 库各模块提供统一的类型规范。
stm32f1xx_hal_cortex.c / stm32f1xx_hal_cortex.h
	|->封装 Cortex-M3 内核相关的通用函数（如 NVIC 配置、MPU 管理、系统软复位、SysTick 操作等），是对core_cm3.h底层接口的上层封装。
stm32f1xx_hal_ppp.c / stm32f1xx_hal_ppp.h
	|->对应 “PPP” 外设（如 GPIO、USART、SPI 等）的标准驱动源码 / 头文件，实现该外设的基础功能接口。
stm32f1xx_hal_ppp_ex.c / stm32f1xx_hal_ppp_ex.h
	|->对应 “PPP” 外设的扩展（特殊）功能驱动源码 / 头文件，实现该外设的进阶、非通用功能。
stm32f1xx_ll_ppp.c / stm32f1xx_ll_ppp.h
	|->对应 “PPP” 外设的 LL 库（底层库）驱动源码 / 头文件，属于更接近硬件的轻量级驱动，会被部分 HAL 库外设驱动调用。

//>> HAL 库寄存器位操作宏定义（PPP 代表具体外设，如 USART、GPIO）<<//
HAL_PPP_ENABLE_IT(__HANDLE__, __INTERRUPT__)
	->作用：使能指定外设的某一中断
HAL_PPP_DISABLE_IT(__HANDLE__, __INTERRUPT__)
	->作用：禁用指定外设的某一中断
HAL_PPP_GET_IT(__HANDLE__, __INTERRUPT__)
	->作用：获取指定外设的某一中断源状态
HAL_PPP_CLEAR_IT(__HANDLE__, __INTERRUPT__)
	->作用：清除指定外设的某一中断标记
HAL_PPP_GET_FLAG(__HANDLE__, __FLAG__)
	->作用：获取指定外设的某一状态标记
HAL_PPP_CLEAR_FLAG(__HANDLE__, __FLAG__)
	->作用：清除指定外设的某一状态标记
HAL_PPP_ENABLE(__HANDLE__)
	->作用：使能指定外设
HAL_PPP_DISABLE(__HANDLE__)
	->作用：禁用指定外设
HAL_PPP_XXXX(__HANDLE__, __PARAM__)
	->作用：执行指定外设的特殊操作（XXXX 为具体操作标识）
HAL_PPP_GET_IT_SOURCE(__HANDLE__, __INTERRUPT__)
	->作用：检查指定外设的某一中断源是否使能

//>> HAL 库回调函数（PPP 代表具体外设）<<//
HAL_PPP_MspInit() / HAL_PPP_MspDeInit()
	->作用：被HAL_PPP_Init()调用，存放外设对应的 GPIO、时钟、NVIC、DMA 等硬件相关初始化 / 反初始化代码
	->举例：HAL_USART_MspInit()（USART 外设的硬件初始化）
HAL_PPP_ProcessCpltCallback
	->作用：由外设中断 / DMA 中断触发调用，HAL 库已自动处理中断标记位的读写 / 清除，只需实现业务逻辑
	->举例：HAL_USART_TxCpltCallback（USART 发送完成后的逻辑处理）
HAL_PPP_ErrorCallback
	->作用：处理外设 / DMA 中断中出现的错误
	->举例：HAL_USART_ErrorCallback（USART 通信错误的处理）