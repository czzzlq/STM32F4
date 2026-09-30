#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一次性工具（已执行完毕，保留备查）：剥离 lcd_ex.c / lcdfont.h 中的厂商注释，
代码与寄存器数据逐字保留，只做机械变换，保证数据零误差。

用法: python strip_vendor_comments.py
"""
import io
import re
import os

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def strip_comments(text):
    """移除所有块注释与行注释（这些文件中不含字符串字面量）"""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def normalize_blank_lines(text):
    lines = [ln.rstrip() for ln in text.split('\n')]
    out = []
    blank_run = 0
    for ln in lines:
        if ln.strip() == '':
            blank_run += 1
            if blank_run <= 1:
                out.append('')
        else:
            blank_run = 0
            out.append(ln)
    return '\n'.join(out)


def process_lcd_ex(src_path, dst_path):
    with io.open(src_path, 'r', encoding='gbk') as f:
        text = f.read()

    text = strip_comments(text)
    text = normalize_blank_lines(text)

    # 为每个 reginit 函数添加统一的中文说明头
    def add_header(m):
        name = m.group(1)
        return '\n/**\n * @brief   %s 寄存器初始化序列\n */\nvoid lcd_ex_%s(void)' % (name.upper(), name)

    text = re.sub(r'void lcd_ex_(\w+)\(void\)', add_header, text)

    header = (
        '/**\n'
        ' * @file    lcd_ex.c\n'
        ' * @brief   各 LCD 驱动 IC 的寄存器初始化序列\n'
        ' * @note    本文件由 lcd.c 以文本方式包含编译（不要单独加入工程）\n'
        ' *          初始化数据与原始序列逐字节一致，仅移除了原厂注释\n'
        ' */\n'
    )
    with io.open(dst_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(header + text.lstrip('\n') + '\n')


def process_lcdfont(src_path, dst_path):
    with io.open(src_path, 'r', encoding='gbk') as f:
        text = f.read()

    text = strip_comments(text)
    text = normalize_blank_lines(text)

    header = (
        '/**\n'
        ' * @file    lcdfont.h\n'
        ' * @brief   12/16/24/32 点阵 ASCII 字库（取模方式：阴码+逐行式+顺向）\n'
        ' * @note    字库从空格(0x20)开始，共 95 个字符\n'
        ' *          单字符字节数 = (size/8 + ((size%8)?1:0)) * (size/2)\n'
        ' */\n'
    )
    with io.open(dst_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(header + text.lstrip('\n') + '\n')


if __name__ == '__main__':
    process_lcd_ex(
        os.path.join(BASE, 'driver', 'LCD', 'lcd_ex.c'),
        os.path.join(BASE, 'driver', 'LCD', 'lcd_ex.c'),
    )
    process_lcdfont(
        os.path.join(BASE, 'driver', 'LCD', 'lcdfont.h'),
        os.path.join(BASE, 'driver', 'LCD', 'lcdfont.h'),
    )
    print('done')
