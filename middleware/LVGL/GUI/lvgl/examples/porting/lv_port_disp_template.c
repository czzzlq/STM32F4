/**
 * @file lv_port_disp_templ.c
 *
 */

 /*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp_template.h"
#include "../../lvgl.h"
#include "lcd.h"                                /* LCD 驱动（含 lcddev 分辨率信息） */
#include "malloc.h"                             /* mymalloc/myfree（外部 SRAM 内存池） */
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/
/* 绘图缓冲区位置：1 = 外部 SRAM（按实际分辨率整屏，96 万像素级面板可用）；
 * 0 = 内部 SRAM（固定 30 行，面板分辨率再大也不额外吃内存） */
#define USE_SRAM                1

/* 缓冲区最大行宽：仅用于给内部 SRAM 回退缓冲定一个编译期上界，
 * 实际注册给 LVGL 的尺寸按 lcddev 的真实分辨率计算 */
#define MY_DISP_HOR_RES         800
#define MY_DISP_VER_RES         480

/* 内部 SRAM 回退路径使用的行数 */
#define DISP_BUF_FALLBACK_LINES 30

/* 颜色字节数（RGB565） */
#define DISP_COLOR_SIZE         ((uint32_t)sizeof(lv_color_t))

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
/* 显示设备初始化函数 */
static void disp_init(void);

/* 显示设备刷新函数 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
/* GPU 填充函数(使用GPU时，需要实现) */
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_color_t *s_dispBuf = NULL;
static lv_disp_draw_buf_t s_drawBufDsc;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
/**
 * @brief       LCD 加速绘制函数
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 *   @arg       color:要填充的颜色数组
 * @retval      无
 * @note        本函数只服务于 LVGL 的 flush 回调，故为文件内私有
 */
static void lcd_draw_fast_rgb_color(int16_t sx, int16_t sy, int16_t ex, int16_t ey, uint16_t *color)
{
    uint16_t w = ex - sx + 1;
    uint16_t h = ey - sy + 1;

    lcd_set_window(sx, sy, w, h);
    uint32_t drawSize = (uint32_t)w * h;

    lcd_write_ram_prepare();

    for (uint32_t i = 0; i < drawSize; i++)
    {
        lcd_wr_data(color[i]);
    }
}

/**
 * @brief       初始化并注册显示设备
 * @param       无
 * @retval      无
 */
void lv_port_disp_init(void)
{
    /*-------------------------
     * 初始化显示设备
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * 创建一个绘图缓冲区
     *----------------------------*/

    /**
     * LVGL 需要一个缓冲区用来绘制小部件
     * 随后，这个缓冲区的内容会通过显示设备的 `flush_cb`(显示设备刷新函数) 复制到显示设备上
     * 这个缓冲区的大小需要大于显示设备一行的大小
     *
     * 这里有3中缓冲配置:
     * 1. 单缓冲区:
     *      LVGL 会将显示设备的内容绘制到这里，并将他写入显示设备。
     *
     * 2. 双缓冲区:
     *      LVGL 会将显示设备的内容绘制到其中一个缓冲区，并将他写入显示设备。
     *      需要使用 DMA 将要显示在显示设备的内容写入缓冲区。
     *      当数据从第一个缓冲区发送时，它将使 LVGL 能够将屏幕的下一部分绘制到另一个缓冲区。
     *      这样使得渲染和刷新可以并行执行。
     *
     * 3. 全尺寸双缓冲区
     *      设置两个屏幕大小的全尺寸缓冲区，并且设置 disp_drv.full_refresh = 1。
     *      这样，LVGL将始终以 'flush_cb' 的形式提供整个渲染屏幕，您只需更改帧缓冲区的地址。
     */

    /* 单缓冲区示例) */
#if USE_SRAM
    /* 按 LCD 实际分辨率分配整屏缓冲（自适应 240x320 ~ 800x480 各种面板）。
     * 分配失败不再 while(1) 死等：直接返回，由上层决定降级或报错，
     * 否则一个内存问题会表现成"上电无反应"，无从定位 */
    {
        uint32_t bufBytes = (uint32_t)lcddev.width * lcddev.height * DISP_COLOR_SIZE;

        s_dispBuf = (lv_color_t *)mymalloc(SRAMEX, bufBytes);

        if (s_dispBuf == NULL)
        {
            printf("LVGL: external SRAM draw buffer alloc failed (%u bytes)\r\n",
                   (unsigned int)bufBytes);
            return;
        }

        lv_disp_draw_buf_init(&s_drawBufDsc, s_dispBuf, NULL,
                              (uint32_t)lcddev.width * lcddev.height);
    }
#else
    /* 备用：内部RAM（30行），静态数组声明时初始化（常量表达式） */
    {
        static lv_color_t s_innerBuf[MY_DISP_HOR_RES * DISP_BUF_FALLBACK_LINES];
        uint32_t innerPixels = (uint32_t)lcddev.width * DISP_BUF_FALLBACK_LINES;

        if (innerPixels > ARRAY_SIZE(s_innerBuf))
        {
            innerPixels = ARRAY_SIZE(s_innerBuf);   /* 面板比静态数组还宽时截断，保证不越界 */
        }

        s_dispBuf = s_innerBuf;
        lv_disp_draw_buf_init(&s_drawBufDsc, s_dispBuf, NULL, innerPixels);
    }
#endif
    /* 双缓冲区示例) */
//    static lv_disp_draw_buf_t draw_buf_dsc_2;
//    static lv_color_t buf_2_1[MY_DISP_HOR_RES * 10];                                            /* 设置缓冲区的大小为 10 行屏幕的大小 */
//    static lv_color_t buf_2_2[MY_DISP_HOR_RES * 10];                                            /* 设置另一个缓冲区的大小为 10 行屏幕的大小 */
//    lv_disp_draw_buf_init(&draw_buf_dsc_2, buf_2_1, buf_2_2, MY_DISP_HOR_RES * 10);             /* 初始化显示缓冲区 */

    /* 全尺寸双缓冲区示例) 并且在下面设置 disp_drv.full_refresh = 1 */
//    static lv_disp_draw_buf_t draw_buf_dsc_3;
//    static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];                               /* 设置一个全尺寸的缓冲区 */
//    static lv_color_t buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES];                               /* 设置另一个全尺寸的缓冲区 */
//    lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2, MY_DISP_HOR_RES * MY_DISP_VER_RES);/* 初始化显示缓冲区 */

    /*-----------------------------------
     * 在 LVGL 中注册显示设备
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                  /* 显示设备的描述符 */
    lv_disp_drv_init(&disp_drv);                    /* 初始化为默认值 */

    /* 建立访问显示设备的函数  */

    /* 设置显示设备的分辨率：按 lcddev 动态获取，一套代码适配
     * 240x320 / 320x480 / 480x800 等各种面板（见 disp_init 的横屏设置） */
    disp_drv.hor_res = lcddev.width;
    disp_drv.ver_res = lcddev.height;

    /* 用来将缓冲区的内容复制到显示设备 */
    disp_drv.flush_cb = disp_flush;

    /* 设置显示缓冲区 */
    disp_drv.draw_buf = &s_drawBufDsc;

    /* 全尺寸双缓冲区示例)*/
    //disp_drv.full_refresh = 1

    /* 如果您有GPU，请使用颜色填充内存阵列
     * 注意，你可以在 lv_conf.h 中使能 LVGL 内置支持的 GPUs
     * 但如果你有不同的 GPU，那么可以使用这个回调函数。 */
    //disp_drv.gpu_fill_cb = gpu_fill;

    /* 注册显示设备 */
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * @brief       初始化显示设备和必要的外围设备
 * @retval      无
 * @note        lcd_init() 可能已在 bsp_init() 中执行过，重复调用会重新识别
 *              ID 并清屏；这里只在尚未初始化（lcddev.id == 0）时补做，
 *              之后统一设置横屏，保证 lcddev 的分辨率就是 LVGL 使用的分辨率
 */
static void disp_init(void)
{
    if (lcddev.id == 0)
    {
        lcd_init();         /* 初始化LCD */
    }

    lcd_display_dir(1);     /* 设置横屏 */
}

/**
 * @brief       将内部缓冲区的内容刷新到显示屏上的特定区域
 *   @note      可以使用 DMA 或者任何硬件在后台加速执行这个操作
 *              但是，需要在刷新完成后调用函数 'lv_disp_flush_ready()'
 *
 * @param       disp_drv    : 显示设备
 *   @arg       area        : 要刷新的区域，包含了填充矩形的对角坐标
 *   @arg       color_p     : 颜色数组
 *
 * @retval      无
 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    /* LVGL 官方给出的一个打点刷新屏幕的例子，但这个效率是最低效的 */

//    int32_t x;
//    int32_t y;
//    for(y = area->y1; y <= area->y2; y++) {
//        for(x = area->x1; x <= area->x2; x++) {
//            /*Put a pixel to the display. For example:*/
//            /*put_px(x, y, *color_p)*/
//            color_p++;
//        }
//    }

//    /* 在指定区域内填充指定颜色块 */
//    lcd_color_fill(area->x1, area->y1, area->x2, area->y2, (uint16_t *)color_p);
    lcd_draw_fast_rgb_color(area->x1,area->y1,area->x2,area->y2,(uint16_t*)color_p);

    /* 重要!!!
     * 通知图形库，已经刷新完毕了 */
    lv_disp_flush_ready(disp_drv);
}

/* 可选: GPU 接口 */

/* 如果你的 MCU 有硬件加速器 (GPU) 那么你可以使用它来为内存填充颜色 */
/**
 * @brief       使用 GPU 进行颜色填充
 *   @note      如有 MCU 有硬件加速器 (GPU),那么可以用它来为内存进行颜色填充
 *
 * @param       disp_drv    : 显示设备
 *   @arg       dest_buf    : 目标缓冲区
 *   @arg       dest_width  : 目标缓冲区的宽度
 *   @arg       fill_area   : 填充的区域
 *   @arg       color       : 颜色数组
 *
 * @retval      无
 */
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/

//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}


#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
