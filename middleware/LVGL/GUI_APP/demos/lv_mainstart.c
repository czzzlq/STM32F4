/**
 * @file    lv_mainstart.c
 * @brief   LVGL 验证界面（上电自检的可视化部分）
 * @note    界面自上而下逐行针对一条移植链路做可视化验证，任何一行异常都能
 *          直接定位到具体环节：
 *            1. 标题栏（分辨率 + 已注册显示/输入设备数）—— 移植层是否接通
 *            2. 8 色色带                                  —— RGB565 颜色与矩形填充
 *            3. 14/16/20/24 四档字号                      —— 字库渲染
 *            4. 滑块 + 数值                               —— 触摸输入 → 事件 → 文本刷新
 *            5. 开关 + 状态文字                           —— 控件状态与 UI→应用层回调
 *            6. 浮层开关按钮                              —— 点击事件、层叠与隐藏标志
 *            7. 进度条 + 百分比                           —— lv_timer_handler 周期驱动
 *            8. 状态区（运行时间 / 触摸坐标 / 呼吸灯）    —— tick 源、输入设备与动画引擎
 *          右下角的 FPS/CPU 浮层由 LV_USE_PERF_MONITOR 提供，用来观察刷新性能
 * @note    本文件只依赖 LVGL，不 include 任何驱动头文件；需要点灯等硬件动作用
 *          应用层实现 lv_ui_led_switch_hook()（见 lv_mainstart.h）
 */
#include "lv_mainstart.h"
#include "common_types.h"   /* __WEAK / UNUSED */
#include <stdio.h>

/* ==================== 配色 ==================== */
#define UI_BG               0x101418    /* 屏幕底色（深灰，便于判断背光与刷新是否生效） */
#define UI_PANEL            0x1E2836    /* 行容器底色 */
#define UI_TITLE_BG         0x1F3A5F    /* 标题栏底色 */
#define UI_TEXT             0xE6EDF3    /* 主文字色 */
#define UI_TEXT_DIM         0x8B949E    /* 次要文字色 */
#define UI_ACCENT           0x2F81F7    /* 强调色（滑块指示条 / 按钮） */
#define UI_OK               0x3FB950    /* 正常态 */
#define UI_OVERLAY_BG       0x2B1214    /* 浮层底色 */

/* 8 色色带：覆盖 RGB565 的三原色、三补色与黑白 */
static const uint32_t s_colorBars[8] =
{
    0xF800,     /* 红 */
    0x07E0,     /* 绿 */
    0x001F,     /* 蓝 */
    0x07FF,     /* 青 */
    0xF81F,     /* 品红 */
    0xFFE0,     /* 黄 */
    0xFFFF,     /* 白 */
    0x0000,     /* 黑 */
};

/* 四档字号：用于验证字库取模、列步进与行数是否正确 */
static const uint8_t s_fontSizes[4] = {14, 16, 20, 24};

static const lv_font_t *const s_fonts[4] =
{
    &lv_font_montserrat_14,
    &lv_font_montserrat_16,
    &lv_font_montserrat_20,
    &lv_font_montserrat_24,
};

/**
 * @brief   界面控件句柄集合
 * @note    定时器与事件回调需要跨函数访问这些控件，统一收在一个结构体里，
 *          避免散落的文件级变量
 */
typedef struct
{
    lv_obj_t *overlay;          /* 可切换显示的浮层 */
    lv_obj_t *overlayState;     /* 浮层当前状态文字 */
    lv_obj_t *switchState;      /* 开关状态文字 */
    lv_obj_t *sliderValue;      /* 滑块数值文字 */
    lv_obj_t *bar;              /* 自动推进的进度条 */
    lv_obj_t *barValue;         /* 进度条百分比文字 */
    lv_obj_t *uptime;           /* 运行时间 */
    lv_obj_t *touch;            /* 触摸状态 */
} ui_widgets_t;

static ui_widgets_t s_ui;

/* ==================== 小工具函数 ==================== */

/**
 * @brief   按面板高度取行高（带上下限夹取）
 * @param   h        : 面板高度
 *   @arg   num, den : 比例分子 / 分母
 *   @arg   min, max : 行高上下限（像素）
 * @retval  行高像素值
 * @note    同一套界面要能同时跑在 480x320 与 800x480 上，所以行高按比例取；
 *          夹取是为了避免小屏挤成一团、大屏行距过大
 */
static lv_coord_t ui_row_h(lv_coord_t h, uint32_t num, uint32_t den, lv_coord_t min, lv_coord_t max)
{
    lv_coord_t v = (lv_coord_t)((uint32_t)h * num / den);

    if (v < min)
    {
        v = min;
    }

    if (v > max)
    {
        v = max;
    }

    return v;
}

/**
 * @brief   创建一个铺满宽度的行容器
 * @param   scr   : 屏幕对象
 *   @arg   y     : 行顶边 y 坐标
 *   @arg   w     : 宽度
 *   @arg   pad   : 左右留白
 *   @arg   rowH  : 行高
 *   @arg   bg    : 底色
 * @retval  行容器对象
 */
static lv_obj_t *ui_row_create(lv_obj_t *scr, lv_coord_t y, lv_coord_t w,
                               lv_coord_t pad, lv_coord_t rowH, uint32_t bg)
{
    lv_obj_t *row = lv_obj_create(scr);

    lv_obj_set_size(row, w - pad * 2, rowH);
    lv_obj_set_pos(row, pad, y);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

    /* lv_obj 默认自带 LV_OBJ_FLAG_CLICKABLE（见 lv_obj.c 的构造函数），
     * 容不下这层"点击"的话，按在行背景上的触摸会被行容器吃掉，
     * 屏幕上的坐标显示就只在行与行之间的几像素缝隙里才会刷新。
     * 清掉可点击标志后，命中测试会跳过本行、继续落到屏幕上；
     * 行内的滑块/开关/按钮自身仍可点击，不受影响 */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    return row;
}

/**
 * @brief   创建一行文本标签
 * @param   parent : 父对象
 *   @arg   font   : 字体（NULL 用默认字体）
 *   @arg   color  : 文字颜色
 *   @arg   text   : 初始文本
 * @retval  标签对象
 */
static lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font,
                                 uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);

    if (font != NULL)
    {
        lv_obj_set_style_text_font(label, font, 0);
    }

    return label;
}

/* ==================== 事件回调 ==================== */

/**
 * @brief   滑块数值变化回调
 * @param   e : 事件对象
 * @note    这条链路验证的是「触摸 → 命中测试 → 控件内部状态 → 事件 → 文本刷新」，
 *          滑块动而数字不动就说明事件回调没被触发
 */
static void ui_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);

    lv_label_set_text_fmt(s_ui.sliderValue, "%d", (int)lv_slider_get_value(slider));
}

/**
 * @brief   开关状态变化回调
 * @param   e : 事件对象
 * @note    除了刷新界面文字，还通过弱钩子回调应用层去点灯，
 *          用来验证「UI → 应用层 → 驱动」这条跨层通路
 */
static void ui_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    lv_label_set_text(s_ui.switchState, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_ui.switchState, lv_color_hex(on ? UI_OK : UI_TEXT_DIM), 0);

    lv_ui_led_switch_hook(on);
}

/**
 * @brief   按钮点击回调：切换浮层显示
 * @param   e : 事件对象
 * @note    浮层是屏幕的兄弟控件（不放进行容器），用来验证控件的层叠与隐藏标志
 */
static void ui_button_event_cb(lv_event_t *e)
{
    UNUSED(e);

    if (lv_obj_has_flag(s_ui.overlay, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(s_ui.overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.overlay);
        lv_label_set_text(s_ui.overlayState, "SHOWN");
    }
    else
    {
        lv_obj_add_flag(s_ui.overlay, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ui.overlayState, "HIDDEN");
    }
}

/**
 * @brief   屏幕按压中回调：实时显示触摸坐标
 * @param   e : 事件对象
 * @note    用 LV_EVENT_PRESSING 而不是轮询输入设备：事件能触发本身就说明
 *          输入设备已注册且被周期读取，坐标正确则说明坐标换算链路正确。
 *          只挂在屏幕上，所以只有按在空白处才会更新（按到控件上会被控件
 *          消费掉），这同时验证了命中测试
 */
static void ui_scr_event_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t point;

    if (indev == NULL)
    {
        return;
    }

    lv_indev_get_point(indev, &point);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED)
    {
        lv_label_set_text_fmt(s_ui.touch, "TOUCH  UP    x=%d  y=%d", (int)point.x, (int)point.y);
    }
    else
    {
        lv_label_set_text_fmt(s_ui.touch, "TOUCH  DOWN  x=%d  y=%d", (int)point.x, (int)point.y);
    }
}

/**
 * @brief   周期刷新回调（进度条 + 运行时间）
 * @param   timer : LVGL 定时器
 * @note    进度条用 LV_ANIM_OFF 手动步进，而不是交给动画引擎：
 *          这样进度值只由本回调推进，一旦 lv_timer_handler() 没被周期调用，
 *          界面会立刻"卡住"——正好用来验证 tick 源与 handler 的时序
 */
static void ui_timer_cb(lv_timer_t *timer)
{
    static int32_t direction = 1;
    int32_t value = lv_bar_get_value(s_ui.bar);

    value += direction * 2;
    if (value >= 100)
    {
        value = 100;
        direction = -1;
    }
    else if (value <= 0)
    {
        value = 0;
        direction = 1;
    }

    lv_bar_set_value(s_ui.bar, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_ui.barValue, "%d%%", (int)value);

    /* 运行时间取 lv_tick_get()：它由 LV_TICK_CUSTOM 走 os_get_tick_count()，
     * 所以这个秒数在走 = tick 源接通了；TIMER 显示本定时器的周期 */
    lv_label_set_text_fmt(s_ui.uptime, "UPTIME %u s   TIMER %u ms   LVGL v%d.%d",
                          (unsigned int)(lv_tick_get() / 1000U),
                          (unsigned int)timer->period,
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
}

/* ==================== 各行构建 ==================== */

/**
 * @brief   第 1 行：标题栏（分辨率 + 已注册设备数）
 * @retval  行高
 * @note    右侧显示的是 LVGL 侧的分辨率与已注册的显示/输入设备个数，
 *          二者都来自移植层：分辨率对得上说明 lv_port_disp_init 拿到了
 *          正确的 lcddev，设备计数为 1/1 说明显示与触摸都已注册成功
 */
static lv_coord_t ui_build_title(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 9, 28, 64);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_TITLE_BG);
    lv_obj_t *left = ui_label_create(row, &lv_font_montserrat_20, UI_TEXT, "LVGL Self-Test");
    lv_obj_t *right;
    uint8_t dispCount = 0;
    uint8_t indevCount = 0;
    lv_disp_t *disp = NULL;
    lv_indev_t *indev = NULL;
    char text[48];

    lv_obj_align(left, LV_ALIGN_LEFT_MID, 8, 0);

    while ((disp = lv_disp_get_next(disp)) != NULL)
    {
        dispCount++;
    }

    while ((indev = lv_indev_get_next(indev)) != NULL)
    {
        indevCount++;
    }

    snprintf(text, sizeof(text), "%dx%d  disp:%d  indev:%d",
             (int)w, (int)h, (int)dispCount, (int)indevCount);
    right = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT_DIM, text);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -8, 0);

    return h;
}

/**
 * @brief   第 2 行：8 色色带
 * @retval  行高
 * @note    色带用于验证 RGB565 的字节顺序与颜色映射：若出现"红蓝互换"，
 *          通常就是 LV_COLOR_16_SWAP 或 LCD 端字节序配置不匹配
 */
static lv_coord_t ui_build_colors(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 10, 20, 48);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_PANEL);
    lv_coord_t barW = (w - pad * 2) / 8;

    for (uint8_t i = 0; i < 8; i++)
    {
        lv_obj_t *bar = lv_obj_create(row);

        lv_obj_set_size(bar, barW, h);
        lv_obj_set_pos(bar, (lv_coord_t)i * barW, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(s_colorBars[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    return h;
}

/**
 * @brief   第 3 行：四档字号
 * @retval  行高
 */
static lv_coord_t ui_build_fonts(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 9, 24, 56);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_PANEL);
    lv_coord_t x = 8;

    for (uint8_t i = 0; i < 4; i++)
    {
        char text[8];
        lv_obj_t *label;

        snprintf(text, sizeof(text), "%d", (int)s_fontSizes[i]);
        label = ui_label_create(row, s_fonts[i], UI_TEXT, text);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, x, 0);
        x += 44;
    }

    {
        lv_obj_t *label = ui_label_create(row, &lv_font_montserrat_24, UI_ACCENT, "LVGL 8");

        lv_obj_align(label, LV_ALIGN_LEFT_MID, x + 8, 0);
    }

    return h;
}

/**
 * @brief   第 4 行：滑块 + 数值
 * @retval  行高
 * @note    数值标签先建好再挂事件：回调里会解引用 s_ui.sliderValue，
 *          若标签尚未创建就触发一次事件，会拿到空指针
 */
static lv_coord_t ui_build_slider(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 9, 26, 48);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_PANEL);
    lv_obj_t *tag = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT_DIM, "SLIDER");
    lv_obj_t *slider;

    lv_obj_align(tag, LV_ALIGN_LEFT_MID, 8, 0);

    s_ui.sliderValue = ui_label_create(row, &lv_font_montserrat_20, UI_TEXT, "40");
    lv_obj_align(s_ui.sliderValue, LV_ALIGN_RIGHT_MID, -8, 0);

    slider = lv_slider_create(row);
    lv_obj_set_size(slider, (w - pad * 2) * 55 / 100, 12);
    lv_obj_align(slider, LV_ALIGN_LEFT_MID, 80, 0);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
    lv_slider_set_value(slider, 40, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, ui_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return h;
}

/**
 * @brief   第 5 行：开关 + 状态文字
 * @retval  行高
 * @note    开关默认置位（ON），这样上电就能通过 lv_ui_led_switch_hook() 看到
 *          应用层点灯动作，无需先触摸。状态标签同样先建好再挂事件
 */
static lv_coord_t ui_build_switch(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 9, 26, 48);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_PANEL);
    lv_obj_t *tag = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT_DIM, "LED0 SWITCH");
    lv_obj_t *sw;

    lv_obj_align(tag, LV_ALIGN_LEFT_MID, 8, 0);

    s_ui.switchState = ui_label_create(row, &lv_font_montserrat_16, UI_OK, "ON");
    lv_obj_align(s_ui.switchState, LV_ALIGN_LEFT_MID, 175, 0);

    sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_LEFT_MID, 110, 0);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, ui_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return h;
}

/**
 * @brief   第 6 行：浮层开关按钮
 * @retval  行高
 * @note    浮层本体在所有行之后才创建，但按钮回调只在用户点击时才会跑，
 *          那时 lv_mainstart() 早已返回、s_ui.overlay 必定已就绪
 */
static lv_coord_t ui_build_button(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 9, 26, 48);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_PANEL);
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_t *btnLabel;

    s_ui.overlayState = ui_label_create(row, &lv_font_montserrat_16, UI_TEXT_DIM, "HIDDEN");
    lv_obj_align(s_ui.overlayState, LV_ALIGN_LEFT_MID, 175, 0);

    lv_obj_set_size(btn, 150, h - 12);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_ACCENT), 0);

    btnLabel = ui_label_create(btn, &lv_font_montserrat_14, UI_TEXT, "TOGGLE OVERLAY");
    lv_obj_center(btnLabel);

    lv_obj_add_event_cb(btn, ui_button_event_cb, LV_EVENT_CLICKED, NULL);

    return h;
}

/**
 * @brief   第 7 行：自动推进的进度条
 * @retval  行高
 */
static lv_coord_t ui_build_bar(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = ui_row_h(lv_disp_get_ver_res(NULL), 1, 9, 26, 48);
    lv_obj_t *row = ui_row_create(scr, y, w, pad, h, UI_PANEL);
    lv_obj_t *tag = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT_DIM, "TIMER BAR");

    lv_obj_align(tag, LV_ALIGN_LEFT_MID, 8, 0);

    s_ui.bar = lv_bar_create(row);
    lv_obj_set_size(s_ui.bar, (w - pad * 2) * 60 / 100, 14);
    lv_obj_align(s_ui.bar, LV_ALIGN_LEFT_MID, 90, 0);
    lv_bar_set_range(s_ui.bar, 0, 100);
    lv_bar_set_value(s_ui.bar, 40, LV_ANIM_OFF);

    s_ui.barValue = ui_label_create(row, &lv_font_montserrat_16, UI_TEXT, "40%");
    lv_obj_align(s_ui.barValue, LV_ALIGN_RIGHT_MID, -8, 0);

    return h;
}

/**
 * @brief   状态区：运行时间 + 触摸状态 + 呼吸灯
 * @param   y : 状态区顶边 y 坐标
 * @note    呼吸灯用 lv_anim 改亮度（LVGL 官方推荐的 exec_cb 用法），
 *          灯在"呼吸"就说明动画引擎与 lv_timer_handler 都在正常工作
 */
static void ui_build_status(lv_obj_t *scr, lv_coord_t y, lv_coord_t w, lv_coord_t pad)
{
    lv_coord_t h = lv_disp_get_ver_res(NULL);
    lv_coord_t rowH = h - y - pad;
    lv_obj_t *row;
    lv_obj_t *led;
    lv_anim_t anim;

    /* 状态区吃掉剩余空间；小面板上行高可能算出负数，夹一个下限保证
     * lv_obj_set_size 不会拿到非法尺寸（超出屏幕的部分由 LVGL 裁剪） */
    if (rowH < 30)
    {
        rowH = 30;
    }

    row = ui_row_create(scr, y, w, pad, rowH, UI_BG);

    s_ui.uptime = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT, "UPTIME --");
    lv_obj_align(s_ui.uptime, LV_ALIGN_TOP_LEFT, 4, 2);

    led = lv_led_create(row);
    lv_obj_align(led, LV_ALIGN_TOP_RIGHT, -8, 2);
    lv_led_set_color(led, lv_color_hex(UI_ACCENT));
    lv_led_set_brightness(led, LV_LED_BRIGHT_MAX);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, led);
    lv_anim_set_values(&anim, LV_LED_BRIGHT_MIN, LV_LED_BRIGHT_MAX);
    lv_anim_set_time(&anim, 600);
    lv_anim_set_playback_time(&anim, 600);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_led_set_brightness);
    lv_anim_start(&anim);

    s_ui.touch = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT, "TOUCH  --");
    lv_obj_align(s_ui.touch, LV_ALIGN_TOP_LEFT, 4, 20);

    /* 底部提示只在状态区够高时创建：窄面板（480x320）状态区只有 ~50px，
     * 第三行会和触摸坐标压在一起，此时优先保留运行时间与触摸坐标这两条实测信息 */
    if (rowH >= 60)
    {
        lv_obj_t *hint = ui_label_create(row, &lv_font_montserrat_14, UI_TEXT_DIM,
                                         "Tap blank area -> live coords");

        lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    }
}

/**
 * @brief   浮层（默认隐藏，由按钮切换）
 * @note    尺寸取屏幕的 60%x40% 并居中，带描边以便观察矩形/边框绘制
 */
static void ui_build_overlay(lv_obj_t *scr, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *title;

    s_ui.overlay = lv_obj_create(scr);
    lv_obj_set_size(s_ui.overlay, w * 60 / 100, h * 40 / 100);
    lv_obj_center(s_ui.overlay);
    lv_obj_set_style_bg_color(s_ui.overlay, lv_color_hex(UI_OVERLAY_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.overlay, 3, 0);
    lv_obj_set_style_border_color(s_ui.overlay, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_radius(s_ui.overlay, 6, 0);
    lv_obj_clear_flag(s_ui.overlay, LV_OBJ_FLAG_SCROLLABLE);

    title = ui_label_create(s_ui.overlay, &lv_font_montserrat_20, UI_TEXT, "OVERLAY OK");
    lv_obj_center(title);

    lv_obj_add_flag(s_ui.overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ==================== 对外入口 ==================== */

/**
 * @brief   构建 LVGL 验证界面
 * @note    无参数、无返回值；重复调用会叠加控件，需要刷新界面时先用
 *          lv_obj_clean(lv_scr_act()) 清屏
 */
void lv_mainstart(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t w = lv_disp_get_hor_res(NULL);
    lv_coord_t h = lv_disp_get_ver_res(NULL);
    lv_coord_t pad = (w / 64 > 4) ? (w / 64) : 4;   /* 面板越宽留白越大，最小 4px */
    lv_coord_t gap = (h / 120 > 2) ? (h / 120) : 2; /* 行间距 */
    lv_coord_t y = pad;

    /* 深色底 + 不可滚动：整屏就是一个固定面板 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 屏幕级按压事件：验证输入设备注册与坐标换算（按空白处才会触发） */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, ui_scr_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, ui_scr_event_cb, LV_EVENT_RELEASED, NULL);

    y += ui_build_title(scr, y, w, pad) + gap;
    y += ui_build_colors(scr, y, w, pad) + gap;
    y += ui_build_fonts(scr, y, w, pad) + gap;
    y += ui_build_slider(scr, y, w, pad) + gap;
    y += ui_build_switch(scr, y, w, pad) + gap;
    y += ui_build_button(scr, y, w, pad) + gap;
    y += ui_build_bar(scr, y, w, pad) + gap;

    ui_build_status(scr, y, w, pad);
    ui_build_overlay(scr, w, h);

    /* 周期刷新：200ms 推进进度条并刷新运行时间 */
    lv_timer_create(ui_timer_cb, 200, NULL);
}

/* ==================== 应用层钩子 ==================== */

/**
 * @brief   界面 → 应用层通知钩子（弱实现）
 * @param   on : 界面上的开关当前状态
 * @note    界面侧默认空实现，应用层（app/task）给出同名强定义即可联动真实
 *          硬件。弱属性只加在定义上、声明处不加：否则 armcc 会把应用层的
 *          覆盖定义一并传播成弱符号，链接时报重定义
 */
__WEAK void lv_ui_led_switch_hook(bool on)
{
    (void)on;
}
