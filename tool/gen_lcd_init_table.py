#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一次性工具：把 lcd_ex.c 中 7 个 IC 的寄存器初始化序列（函数调用流）
转换为紧凑表 + 统一执行器，节省 Flash。
流程：解析现有调用流 -> 编码为表 -> 重放解码并与原调用流逐操作比对 -> 生成新文件
"""
import io
import re
import os

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(BASE, 'driver', 'LCD', 'lcd_ex.c')

# SSD1963 分辨率宏（lcd.h 中定义，生成器内求值表达式用）
NS = {
    'SSD_HOR_RESOLUTION': 800, 'SSD_VER_RESOLUTION': 480,
    'SSD_HOR_PULSE_WIDTH': 1, 'SSD_HOR_BACK_PORCH': 46, 'SSD_HOR_FRONT_PORCH': 210,
    'SSD_VER_PULSE_WIDTH': 1, 'SSD_VER_BACK_PORCH': 23, 'SSD_VER_FRONT_PORCH': 22,
}
NS['SSD_HT'] = NS['SSD_HOR_RESOLUTION'] + NS['SSD_HOR_BACK_PORCH'] + NS['SSD_HOR_FRONT_PORCH']
NS['SSD_HPS'] = NS['SSD_HOR_BACK_PORCH']
NS['SSD_VT'] = NS['SSD_VER_RESOLUTION'] + NS['SSD_VER_BACK_PORCH'] + NS['SSD_VER_FRONT_PORCH']
NS['SSD_VPS'] = NS['SSD_VER_BACK_PORCH']

# 只允许：数字、NS 中的常量标识符、算术/移位/括号运算符
EXPR_WHITELIST = re.compile(r'^[\w\s()+\-*/%<>&|~]+$')

def eval_expr(s):
    s = s.strip()
    if re.fullmatch(r'0[xX][0-9a-fA-F]+|[0-9]+', s):
        return int(s, 0)
    if not EXPR_WHITELIST.match(s):
        raise ValueError(f'表达式包含不允许的字符: {s!r}')
    # 输入为本地受控源码中的常量表达式，白名单已排除函数调用/属性访问，
    # builtins 关闭，仅暴露 NS 常量命名空间
    return int(eval(s, {'__builtins__': {}}, dict(NS)))

def parse_ops(text):
    """提取每个 reginit 函数的操作序列: (op, value)（花括号配对定位函数体）"""
    funcs = {}
    pos = 0
    while True:
        m = re.search(r'void\s+(lcd_ex_\w+)\s*\(void\)\s*\{', text[pos:])
        if not m:
            break
        name = m.group(1)
        start = pos + m.end()
        depth = 1
        i = start
        while depth > 0:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        body = text[start:i - 1]
        pos = i
        ops = []
        for line in body.split('\n'):
            line = line.strip()
            if not line:
                continue
            if m2 := re.fullmatch(r'lcd_wr_regno\((.+)\);', line):
                ops.append(('regno', eval_expr(m2.group(1))))
            elif m2 := re.fullmatch(r'lcd_wr_data\((.+)\);', line):
                ops.append(('data', eval_expr(m2.group(1))))
            elif m2 := re.fullmatch(r'lcd_write_reg\((.+?),\s*(.+?)\);', line):
                ops.append(('regno', eval_expr(m2.group(1))))
                ops.append(('data', eval_expr(m2.group(2))))
            elif m2 := re.fullmatch(r'systickDelay_ms\((\d+)\)\s*;', line):
                ops.append(('delay_ms', int(m2.group(1))))
            elif m2 := re.fullmatch(r'systickDelay_us\((\d+)\)\s*;', line):
                ops.append(('delay_us', int(m2.group(1))))
            else:
                raise ValueError('parse error: %s -> %r' % (name, line))
        funcs[name] = ops
    return funcs

def encode(ops):
    """编码为 uint16 表：
    [cnt, regno, data...]      cnt: 0x8000=纯寄存器号; 否则=data个数(<=255)
    [0xFFFF, ms, us]           延时（ms/us 只一个非零）"""
    table = []
    i = 0
    while i < len(ops):
        op, val = ops[i]
        if op == 'regno':
            if i + 1 < len(ops) and ops[i + 1][0] == 'data':
                datas = []
                i += 1
                while i < len(ops) and ops[i][0] == 'data':
                    datas.append(ops[i][1]); i += 1
                assert len(datas) <= 255
                table += [len(datas), val] + datas
            else:
                table += [0x8000, val]; i += 1
        elif op in ('delay_ms', 'delay_us'):
            ms, us = (val, 0) if op == 'delay_ms' else (0, val)
            table += [0xFFFF, ms, us]; i += 1
        else:
            raise ValueError(f'数据流中孤立 data: {ops[i]}')
    table += [0xFFFF, 0, 0]     # 序列结束
    return table

def decode(table):
    """重放表得到操作序列（用于与原调用流比对）"""
    ops = []
    i = 0
    while True:
        cnt = table[i]; i += 1
        if cnt == 0xFFFF:
            ms, us = table[i], table[i + 1]; i += 2
            if ms == 0 and us == 0:
                break
            ops.append(('delay_ms' if ms else 'delay_us', ms if ms else us))
        else:
            reg = table[i]; i += 1
            ops.append(('regno', reg))
            if not (cnt & 0x8000):
                for _ in range(cnt):
                    ops.append(('data', table[i])); i += 1
    return ops

def fmt_table(t, name):
    lines = []
    row = []
    for v in t:
        row.append('0x%04X' % v)
        if len(row) == 8:
            lines.append('    ' + ', '.join(row) + ',')
            row = []
    if row:
        lines.append('    ' + ', '.join(row) + ',')
    return 'static const uint16_t %s[] =\n{\n%s\n};\n' % (name, '\n'.join(lines))

def main():
    text = io.open(SRC, encoding='utf-8').read()
    funcs = parse_ops(text)
    assert len(funcs) == 7, list(funcs)

    # 1. 编码 + 解码回放比对
    tables = {}
    for name, ops in funcs.items():
        t = encode(ops)
        replayed = decode(t)
        assert replayed == ops, name
        tables[name] = t
        print(f'[OK] {name}: {len(ops)} 个操作 -> {len(t)} 表项, 回放一致')

    # 2. 生成新文件
    body = []
    for name in sorted(tables):
        var = 's_' + name.replace('lcd_ex_', 'initSeq_')
        body.append('/**\n * @brief   %s 寄存器初始化序列\n */\nvoid %s(void)\n{\n    lcd_ex_run_init(%s);\n}\n' % (name.upper(), name, var))
    table_defs = '\n'.join(fmt_table(tables[n], 's_' + n.replace('lcd_ex_', 'initSeq_')) for n in sorted(tables))

    out = '''/**
 * @file    lcd_ex.c
 * @brief   各 LCD 驱动 IC 的寄存器初始化序列（表驱动）
 * @note    本文件由 lcd.c 以文本方式包含编译（不要单独加入工程）。
 *          初始化数据由 tool/gen_lcd_init_table.py 从原始函数调用流
 *          转换生成，重放校验逐操作一致。
 *          编码格式（uint16）：
 *            [cnt, regno, data...]  cnt: 0x8000=纯寄存器号；否则=data 个数
 *            [0xFFFF, ms, us]       延时（ms/us 只一个非零）
 *            [0xFFFF, 0, 0]         序列结束
 */
#include "delay.h"
#include "lcd.h"

/**
 * @brief   统一初始化序列执行器
 * @param   seq : 序列表（编码格式见文件头）
 */
static void lcd_ex_run_init(const uint16_t *seq)
{
    while (1)
    {
        uint16_t cnt = *seq++;

        if (cnt == 0xFFFF)              /* 延时 / 序列结束 */
        {
            uint16_t ms = *seq++;
            uint16_t us = *seq++;

            if (ms == 0 && us == 0)
            {
                break;                  /* 序列结束 */
            }

            if (ms) systickDelay_ms(ms);
            if (us) systickDelay_us(us);
        }
        else
        {
            lcd_wr_regno(*seq++);

            if (!(cnt & 0x8000))        /* 带数据的寄存器组 */
            {
                cnt &= 0x7FFF;
                while (cnt--)
                {
                    lcd_wr_data(*seq++);
                }
            }
            /* cnt==0x8000: 纯寄存器号，无数据 */
        }
    }
}

%s

%s
''' % (table_defs, '\n'.join(body))

    # 3. 从生成的文本中提取序列表并解码，与原始操作流比对（校验通过后才落盘）
    for name, ops in funcs.items():
        var = 's_' + name.replace('lcd_ex_', 'initSeq_')
        m = re.search(r'static const uint16_t %s\[\]\s*=\s*\{(.*?)\n\};' % var, out, re.DOTALL)
        assert m, var
        values = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', m.group(1))]
        assert decode(values) == ops, var

    with io.open(SRC, 'w', encoding='utf-8', newline='\n') as f:
        f.write(out)
    print('生成完成，落盘内容已校验')

if __name__ == '__main__':
    main()
