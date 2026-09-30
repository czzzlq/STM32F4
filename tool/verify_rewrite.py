#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态验证脚本（退出码：0 全部通过 / 1 存在未通过项）：
1. 头文件 API 兼容性（旧版 vs 重写版函数声明对比）
2. 函数定义覆盖检查（声明过的非 inline 函数必须存在定义）
3. 重复符号检查（HAL 弱回调 / IRQ Handler / 全局句柄）
4. include 路径解析检查
5. BSP 宏引用检查（引用的宏必须已定义）
6. 关键 CMSIS / FreeRTOS 依赖检查 + F1 残留检查
7. 厂商标记残留检查（源码与 README 中不得出现原厂商标/站点）
"""
import io
import os
import re
import subprocess
import sys

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
    os.path.join(BASE, 'middleware', 'LVGL', 'GUI', 'lvgl'),
    os.path.join(BASE, 'middleware', 'LVGL', 'GUI', 'lvgl', 'src'),
    os.path.join(BASE, 'middleware', 'LVGL', 'GUI', 'lvgl', 'examples', 'porting'),
    os.path.join(BASE, 'middleware', 'LVGL', 'GUI_APP', 'demos'),
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
# 覆盖检查范围：REWRITTEN（F1→F4 重写过的头文件）之外的驱动/BSP 头文件
COVERAGE_HEADERS = REWRITTEN + [
    'driver/LED/drv_led.h', 'driver/ADC/drv_adc.h', 'driver/IWDG/drv_iwdg.h',
    'driver/IIC/myiic.h', 'driver/24CXX/24cxx.h',
    'driver/TOUCH/touch.h', 'driver/TOUCH/ctiic.h',
    'driver/TOUCH/gt9xxx.h', 'driver/TOUCH/ft5206.h',
    'bsp/bsp_init.h', 'utility/ringbuf.h', 'os/os_wrapper/os_wrapper.h',
]
_coverage_seen = set()
for h in COVERAGE_HEADERS:
    if h in _coverage_seen:
        continue
    _coverage_seen.add(h)
    c = h.replace('.h', '.c')
    if not os.path.exists(os.path.join(BASE, c)):
        continue
    decls = extract_decls(read(h))
    impl = strip_comments(read(c))
    # 函数定义：name(...) { 且不在注释里（已剥离）
    defined = set(re.findall(r'\b([A-Za-z_]\w*)\s*\([^;{}]*?\)\s*\{', impl))
    missing = []
    for name in decls:
        if name in EXT_PROVIDED:
            continue
        if 'inline' in decls[name][1]:
            continue
        if name not in defined:
            missing.append(name)

    if missing:
        defs_ok = False
        for name in missing:
            print(f'[X] {h} 声明的 {name} 在 {c} 中无定义')
        print(f'[X] {h} -> {c}：{len(missing)}/{len(decls)} 个声明无定义')
    else:
        print(f'[OK] {h} -> {c}（{len(decls)} 个声明均有定义）')

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
# 第三方子树不参与 include 自检：LVGL 用自己的包含约定，且仓库里保留了
# GPU/FreeType/FatFs 等未参与编译的可选后端，它们的头文件本工程并不提供
SKIP_DIRS = [os.path.join(BASE, 'middleware', 'LVGL')]
for d in SCAN_DIRS:
    for root, _, files in os.walk(os.path.join(BASE, d)):
        if any(root.startswith(s) for s in SKIP_DIRS):
            continue
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
                    print(f"[X] {os.path.relpath(p, BASE).replace(os.sep, '/')}: 找不到 include \"{inc}\"")
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
print('=' * 60)
print('7. 厂商标记残留检查（源码与 README 内不得出现原厂商标/站点/产品名）')
print('=' * 60)
# 说明：只扫 .c/.h 与 README.md。工具脚本本身需要写出这些模式，故不参与自检；
# 就业项目方案.md 是方案文档，其中引用的是参考书名，不属工程标记，同样排除
VENDOR_PAT = re.compile(r'正点原子|ALIENTEK|alientek|yuanzige|openedv|星翼电子|战舰|精英板')
vendor_targets = []
for d in ('app', 'bsp', 'common', 'driver', 'middleware', 'utility', 'test',
          os.path.join('os', 'os_wrapper')):
    for root, _, files in os.walk(os.path.join(BASE, d)):
        vendor_targets += [os.path.join(root, f) for f in files if f.endswith(('.c', '.h'))]
vendor_targets.append(os.path.join(BASE, 'README.md'))

vendor_hits = []
for p in vendor_targets:
    for i, line in enumerate(read(p).splitlines(), 1):
        if VENDOR_PAT.search(line):
            vendor_hits.append(f'{os.path.relpath(p, BASE)}:{i}: {line.strip()}')

if vendor_hits:
    for h in vendor_hits:
        print(f'[X] 厂商标记残留: {h}')
else:
    print('[OK] 无厂商标记残留')
vendor_ok = not vendor_hits

print()
failed = []
if not api_ok:
    failed.append('1.API 差异(信息性)')
if not defs_ok:
    failed.append('2.函数定义覆盖')
if dups:
    failed.append('3.重复符号')
if not inc_ok:
    failed.append('4.include 路径')
if not macro_ok:
    failed.append('5.BSP 宏引用')
if not deps_ok:
    failed.append('6.F4/F1 依赖')
if not vendor_ok:
    failed.append('7.厂商标记')

# 第 1 项是「与 git HEAD 的差异」，重写过程中必然有变化，只作信息展示，不参与结论
blocking = [f for f in failed if not f.startswith('1.')]

if blocking:
    print('===== 存在未通过项: ' + ', '.join(blocking) + ' =====')
    sys.exit(1)

print('===== 全部检查通过 =====')
if failed:
    print('（信息性提示: ' + ', '.join(failed) + '）')
sys.exit(0)
