#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态验证脚本：
1. 头文件 API 兼容性（旧版 vs 重写版函数声明对比）
2. 函数定义覆盖检查（声明过的非 inline 函数必须存在定义）
3. 重复符号检查（HAL 弱回调 / IRQ Handler / 全局句柄）
4. include 路径解析检查
5. BSP 宏引用检查（引用的宏必须已定义）
6. 关键 CMSIS / FreeRTOS 依赖检查
"""
import io
import os
import re
import subprocess

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INC_PATHS = [  # eide.yml incList（STM32F407）
    BASE, os.path.join(BASE, 'app'), os.path.join(BASE, 'app', 'task'),
    os.path.join(BASE, 'bsp'), os.path.join(BASE, 'common'), os.path.join(BASE, 'core', 'system'),
    os.path.join(BASE, 'core', 'CMSIS', 'Device', 'ST', 'STM32F4xx', 'Include'),
    os.path.join(BASE, 'core', 'CMSIS', 'Include'),
    os.path.join(BASE, 'core', 'STM32F4xx_HAL_Driver', 'Inc'),
    os.path.join(BASE, 'driver', 'LED'), os.path.join(BASE, 'driver', 'KEY'),
    os.path.join(BASE, 'driver', 'LCD'), os.path.join(BASE, 'driver', 'ADC'),
    os.path.join(BASE, 'driver', 'IWDG'), os.path.join(BASE, 'driver', 'SRAM'),
    os.path.join(BASE, 'driver', 'TIMER'), os.path.join(BASE, 'driver', 'delay'),
    os.path.join(BASE, 'driver', 'usart'), os.path.join(BASE, 'driver', 'IIC'),
    os.path.join(BASE, 'driver', '24CXX'), os.path.join(BASE, 'driver', 'TOUCH'),
    os.path.join(BASE, 'middleware', 'MALLOC'),
    os.path.join(BASE, 'utility'), os.path.join(BASE, 'test', 'stubs'),
    os.path.join(BASE, 'os', 'os_wrapper'), os.path.join(BASE, 'os', 'FreeRTOS'),
    os.path.join(BASE, 'os', 'FreeRTOS', 'include'),
    os.path.join(BASE, 'os', 'FreeRTOS', 'portable', 'RVDS', 'ARM_CM3'),
    os.path.join(BASE, 'os', 'FreeRTOS', 'portable', 'MemMang'),
]

REWRITTEN = [
    'driver/delay/delay.h', 'driver/LED/drv_led.h', 'driver/KEY/drv_key.h',
    'driver/usart/usart.h', 'driver/SRAM/sram.h', 'driver/TIMER/btim.h',
    'driver/TIMER/gtim.h', 'driver/TIMER/atim.h', 'driver/TIMER/timer_callback.h',
    'driver/LCD/lcd.h', 'middleware/MALLOC/malloc.h',
]

def read(path, ref=None):
    try:
        if ref:
            return subprocess.run(['git', 'show', f'{ref}:{path}'], capture_output=True).stdout.decode('gbk', errors='replace')
        return io.open(path, encoding='utf-8', errors='replace').read()
    except Exception as e:
        return ''

def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'//[^\n]*', '', text)
    # 去掉预处理指令行（含 #define / #include / #if 等），避免宏定义被当成函数声明
    text = re.sub(r'^[ \t]*#[^\n]*', '', text, flags=re.MULTILINE)
    return text

def strip_comments_keep_pp(text):
    """只去注释、保留预处理指令行（用于收集 #define / 枚举成员）"""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    return re.sub(r'//[^\n]*', '', text)

def collect_defined_names(text):
    """收集文件里定义的宏名与枚举成员名"""
    names = set(re.findall(r'#define\s+([A-Za-z_]\w*)', text))
    plain = strip_comments_keep_pp(text)
    for m in re.finditer(r'\benum\b[^;{]*\{([^}]*)\}', plain, re.DOTALL):
        names |= set(re.findall(r'([A-Za-z_]\w*)\s*(?:=[^,}]*)?,', m.group(1) + ','))
    return names

def extract_decls(text):
    """提取函数声明：返回 (函数名, 规范化参数签名) 列表"""
    text = strip_comments(text)
    decls = {}
    # 匹配 "rettype name(args);" 形式（name 前可带 *）
    for m in re.finditer(r'\b(\w[\w\s\*]*?)\s+([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*;', text, re.DOTALL):
        ret, name, args = m.group(1), m.group(2), m.group(3)
        if name in ('if', 'while', 'for', 'switch', 'return', 'sizeof', 'typedef', 'struct',
                    'union', 'enum', 'void', 'int', 'char', 'const'):
            continue
        # 跳过宏调用误匹配（参数中含逗号表达式的宏），仅保留参数为类型名形式的
        if re.search(r'\b(do|while)\b\s*[;{]', ret):
            continue
        norm = re.sub(r'\s+', ' ', args).strip()
        norm = re.sub(r'\b(uint8_t|uint16_t|uint32_t|int|char|void|unsigned)\s+\w+\s*$', r'\1', norm)  # 忽略参数名
        decls[name] = (norm, ret.strip())
    return decls

print('=' * 60)
print('1. 头文件 API 差异（对比 HEAD，信息性：有差异会列出，不影响最终结论）')
print('=' * 60)
api_ok = True
for h in REWRITTEN:
    old = extract_decls(read(h, 'HEAD'))
    new = extract_decls(read(h))
    missing = [n for n in old if n not in new]
    extra = [n for n in new if n not in old]
    sig_diff = [n for n in old if n in new and old[n][0] != new[n][0]]
    if missing or extra or sig_diff:
        api_ok = False
        print(f'[X] {h}')
        for n in missing: print(f'    缺失声明: {n}{old[n]}')
        for n in extra: print(f'    新增声明: {n}{new[n]}')
        for n in sig_diff: print(f'    签名变化: {n} 旧{old[n]} -> 新{new[n]}')
    else:
        print(f'[OK] {h} ({len(old)} 个声明一致)')

print()
print('=' * 60)
print('2. 函数定义覆盖检查')
print('=' * 60)
c_files = [f for f in REWRITTEN if f.endswith('.h')]
c_files = [f.replace('.h', '.c') for f in c_files if os.path.exists(os.path.join(BASE, f.replace('.h', '.c')))]
# 白名单：外部提供的符号
EXT_PROVIDED = {
    'HAL_UART_Receive_IT', 'HAL_UART_Init', 'HAL_UART_IRQHandler', 'HAL_GPIO_Init',
    'HAL_GPIO_WritePin', 'HAL_GPIO_TogglePin', 'HAL_GPIO_ReadPin', 'HAL_IncTick',
    'HAL_SYSTICK_CLKSourceConfig', 'HAL_TIM_Base_Init', 'HAL_TIM_Base_Start_IT',
    'HAL_TIM_PWM_Init', 'HAL_TIM_PWM_ConfigChannel', 'HAL_TIM_PWM_Start',
    'HAL_TIM_IC_Init', 'HAL_TIM_IC_ConfigChannel', 'HAL_TIM_IC_Start_IT',
    'HAL_TIM_OC_Init', 'HAL_TIM_OC_ConfigChannel', 'HAL_TIM_OC_Start',
    'HAL_TIM_ReadCapturedValue', 'HAL_TIM_SlaveConfigSynchro', 'HAL_TIMEx_ConfigBreakDeadTime',
    'HAL_TIMEx_PWMN_Start', 'HAL_SRAM_Init', 'HAL_NVIC_SetPriority', 'HAL_NVIC_EnableIRQ',
    'HAL_RCC_GetHCLKFreq', 'HAL_Init', 'xPortSysTickHandler', 'xTaskGetSchedulerState',
    'os_enter_critical', 'os_exit_critical', 'printf', 'memset', 'SystemCoreClockUpdate',
    'HAL_RCC_OscConfig', 'HAL_RCC_ClockConfig', 'HAL_RCC_DeInit', 'HAL_PWR_EnterSTANDBYMode',
    'NVIC_SystemReset', 'assert_param', 'HAL_Delay',
}
defs_ok = True
for h in REWRITTEN:
    c = h.replace('.h', '.c')
    if not os.path.exists(os.path.join(BASE, c)):
        continue
    decls = extract_decls(read(h))
    impl = strip_comments(read(c))
    # 函数定义：name(...) { 且不在注释里（已剥离）
    defined = set(re.findall(r'\b([A-Za-z_]\w*)\s*\([^;{}]*?\)\s*\{', impl))
    for name in decls:
        if name in EXT_PROVIDED:
            continue
        if 'inline' in decls[name][1]:
            continue
        if name not in defined:
            defs_ok = False
            print(f'[X] {h} 声明的 {name} 在 {c} 中无定义')
    print(f'[OK] {h} -> {c}')

print()
print('=' * 60)
print('3. 重复符号检查（编译列表内）')
print('=' * 60)
COMPILED_C = [  # eide.yml 编译列表中的工程 .c 文件
    'app/stm32f4xx_it.c', 'app/main.c', 'app/task/freertos_demo.c',
    'driver/LED/drv_led.c', 'driver/LCD/lcd.c', 'driver/SRAM/sram.c',
    'driver/ADC/drv_adc.c', 'driver/IWDG/drv_iwdg.c',
    'driver/TIMER/atim.c', 'driver/TIMER/btim.c', 'driver/TIMER/gtim.c',
    'driver/TIMER/timer_callback.c', 'driver/KEY/drv_key.c',
    'driver/delay/delay.c', 'driver/usart/usart.c',
    'driver/IIC/myiic.c', 'driver/24CXX/24cxx.c',
    'driver/TOUCH/touch.c', 'driver/TOUCH/ctiic.c', 'driver/TOUCH/gt9xxx.c',
    'driver/TOUCH/ft5206.c', 'middleware/MALLOC/malloc.c',
    'utility/ringbuf.c',
    'bsp/bsp_init.c', 'core/system/system_hal.c', 'os/os_wrapper/os_wrapper.c',
]
symbols = {}  # 强定义符号 -> 文件列表
for c in COMPILED_C:
    p = os.path.join(BASE, c)
    if not os.path.exists(p):
        continue
    text = strip_comments(read(p))
    # IRQ handler 与 HAL 弱回调
    for m in re.finditer(r'\b(void|int|uint32_t|char)\s+([A-Za-z_]\w*)\s*\([^;{}]*?\)\s*\{', text):
        name = m.group(2)
        if ('IRQHandler' in name) or ('Handler' in name) or name.startswith('HAL_') or ('MspInit' in name) or ('Callback' in name):
            symbols.setdefault(name, []).append(c)
dups = {k: v for k, v in symbols.items() if len(v) > 1}
if dups:
    print('[X] 重复符号:')
    for k, v in dups.items():
        print(f'    {k}: {v}')
else:
    print('[OK] 无重复 IRQ/Callback 符号')
    for k, v in symbols.items():
        print(f'    {k} <- {v[0]}')

print()
print('=' * 60)
print('4. include 路径解析检查（仅工程自有代码）')
print('=' * 60)
inc_ok = True
SCAN_DIRS = ['app', 'bsp', 'common', 'driver', 'middleware', 'utility', 'test',
             os.path.join('os', 'os_wrapper')]
for d in SCAN_DIRS:
    for root, _, files in os.walk(os.path.join(BASE, d)):
        for f in files:
            if not f.endswith(('.c', '.h')):
                continue
            p = os.path.join(root, f)
            text = read(p)
            for m in re.finditer(r'#include\s+"([^"]+)"', text):
                inc = m.group(1)
                cand = [os.path.normpath(os.path.join(os.path.dirname(p), inc))]
                cand += [os.path.normpath(os.path.join(i, inc)) for i in INC_PATHS]
                if not any(os.path.exists(c) for c in cand):
                    inc_ok = False
                    print(f'[X] {os.path.relpath(p, BASE)}: 找不到 include "{inc}"')
print('[OK] include 全部可解析' if inc_ok else '')

print()
print('=' * 60)
print('5. BSP 宏引用检查（driver 引用的硬件宏必须已定义）')
print('=' * 60)
macro_src = {}
MACRO_HEADERS = [
    'bsp/bsp_uart.h', 'bsp/bsp_sram.h', 'bsp/bsp_lcd.h', 'bsp/bsp_timer.h',
    'bsp/bsp_gpio.h', 'bsp/bsp_adc.h', 'bsp/bsp_init.h',
    'driver/usart/usart.h', 'driver/LCD/lcd.h', 'driver/KEY/drv_key.h',
    'driver/LED/drv_led.h', 'driver/ADC/drv_adc.h', 'driver/IWDG/drv_iwdg.h',
    'driver/SRAM/sram.h', 'driver/TIMER/timer_callback.h', 'driver/TIMER/atim.h',
    'driver/TIMER/btim.h', 'driver/TIMER/gtim.h', 'driver/delay/delay.h',
    'middleware/MALLOC/malloc.h', 'common/common_types.h', 'common/project_config.h',
    'core/system/system_hal.h', 'os/os_wrapper/os_wrapper.h', 'utility/ringbuf.h',
]
for h in MACRO_HEADERS:
    text = read(h)
    for name in collect_defined_names(text):
        macro_src.setdefault(name, h)

# 只检查「通过 bsp 宏访问硬件」的驱动（触摸/EEPROM 的引脚宏在各自头文件里定义）
SECTION5_C = [c for c in COMPILED_C
              if not (c.startswith('driver/TOUCH/') or c.startswith('driver/IIC/')
                      or c.startswith('driver/24CXX/'))]

macro_ok = True
IGNORE_MEMBERS = {'LCD_REG', 'LCD_RAM'}     # bsp_lcd.h 里的结构体成员，不是宏
for c in SECTION5_C:
    p = os.path.join(BASE, c)
    raw = read(p)
    text = strip_comments(raw)
    # 文件自身定义的宏 / 枚举成员也算已定义（如 freertos_demo.c 的 SRAM_TEST_*）
    own = collect_defined_names(raw)
    used = set(re.findall(r'\b(BSP_[A-Z_0-9]+|USART1_[A-Z_0-9]+|LCD_[A-Z_0-9]+|SRAM_[A-Z_0-9]+|BTIM_[A-Z_0-9]+|GTIM_[A-Z_0-9]+|ATIM_[A-Z_0-9]+|DRV_[A-Z_0-9]+|TIMER_[A-Z_0-9]+|SRAMBANK|MEM[12]_[A-Z_0-9]+|MT_TYPE|SYS_SUPPORT_OS)\b', text))
    for u in sorted(used):
        if u in own or u in IGNORE_MEMBERS:
            continue
        if u not in macro_src:
            macro_ok = False
            print(f'[X] {c} 引用了未定义的宏 {u}')
print('[OK] 所有硬件宏引用均已定义' if macro_ok else '')

print()
print('=' * 60)
print('6. 关键依赖检查（STM32F4）与 F1 残留检查')
print('=' * 60)

cmsis = read('core/CMSIS/Device/ST/STM32F4xx/Include/stm32f407xx.h')
gpio_ex = read('core/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_gpio_ex.h')

# 工程源码里不应再出现 F1 专有符号（只扫工程自有代码，注释先剥离）
F1_PAT = re.compile(r'stm32f1|STM32F1|__HAL_RCC_AFIO|__HAL_AFIO_REMAP|AFIO->|'
                    r'GPIO_MODE_AF_INPUT|DMA1_Channel\d|TIM8_UP_IRQn\b|GPIO_Mode_Out_PP')
f1_targets = []
for d in ('app', 'bsp', 'common', 'driver', 'middleware', 'utility'):
    for root, _, files in os.walk(os.path.join(BASE, d)):
        f1_targets += [os.path.join(root, f) for f in files if f.endswith(('.c', '.h'))]
for root, _, files in os.walk(os.path.join(BASE, 'os', 'os_wrapper')):
    f1_targets += [os.path.join(root, f) for f in files if f.endswith(('.c', '.h'))]
f1_targets.append(os.path.join(BASE, 'os', 'FreeRTOS', 'FreeRTOSConfig.h'))

f1_hits = []
for p in f1_targets:
    for i, line in enumerate(strip_comments(read(p)).splitlines(), 1):
        if F1_PAT.search(line):
            f1_hits.append(f'{os.path.relpath(p, BASE)}:{i}: {line.strip()}')

deps = [
    ('stm32f407xx.h 定义 FSMC 外设', 'FSMC' in cmsis),
    ('stm32f4xx_hal_gpio_ex.h 定义 GPIO_AF12_FSMC', 'GPIO_AF12_FSMC' in gpio_ex),
    ('工程源码无 F1 专有符号', not f1_hits),
]
for name, ok in deps:
    print(f'[OK] {name}' if ok else f'[X] {name}')
for h in f1_hits[:20]:
    print(f'    F1 残留: {h}')
deps_ok = all(ok for _, ok in deps)

fc = read('os/FreeRTOS/FreeRTOSConfig.h')
for key in ['configTICK_RATE_HZ', 'configMAX_SYSCALL_INTERRUPT_PRIORITY']:
    m = re.search(r'#define\s+' + key + r'\s+(.+)', fc)
    print(f'[OK] {key} = {m.group(1).strip()}' if m else f'[X] {key} 未找到')

print()
if defs_ok and not dups and inc_ok and macro_ok and deps_ok:
    print('===== 全部检查通过 =====')
else:
    print('===== 存在未通过项 =====')
