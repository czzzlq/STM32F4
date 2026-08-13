/**
  ******************************************************************************
  * @file       drv_key.c
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月25日
  * @brief      按键驱动
  * @attention  仅供学习使用
  * @version    
  *             -V0.1
  *             -v0.2:实现应用层与硬件层分离，硬件层负责实现接口
  ******************************************************************************
**/
#include "drv_key.h"
#include "bsp_gpio.h"
#include "os_wrapper.h"
#include "timer_callback.h"

#define DEBOUNCE_TIME           10          //消抖时间(单位：ms)
#define KEY_LONG_PRESS_TIME     3000        //按键长按阈值(单位：ms)

/**
 * @brief   按键全局控制块
 * @note    使用volatile关键字，保证在中断和任务之间的访问是可见的
 */
volatile drv_key_ctrl_block_t drv_key_ctrl_block[DRV_KEY_ID_MAX] = {
    {BSP_KEY0_PORT,BSP_KEY0_PIN,0,0,DRV_KEY_EVENT_NONE,BSP_KEY0_ACTIVE_LEVEL,0},
    {BSP_KEY1_PORT,BSP_KEY1_PIN,0,0,DRV_KEY_EVENT_NONE,BSP_KEY1_ACTIVE_LEVEL,0},
    {BSP_WKUP_PORT,BSP_WKUP_PIN,0,0,DRV_KEY_EVENT_NONE,BSP_WKUP_ACTIVE_LEVEL,0}
};

/**
 * @brief   按键检测函数
 * @param   p_ctrl_block：按键控制块指针 
 * @retval  NULL
 */
static void key_detect(drv_key_ctrl_block_t* p_ctrl_block){
    GPIO_PinState pin_level = HAL_GPIO_ReadPin(p_ctrl_block->gpio_port,p_ctrl_block->gpio_pin);

    if(pin_level == (p_ctrl_block->active_level)){  //按键按下
        if(p_ctrl_block->key_state != 1){  //按键从释放到按下
            p_ctrl_block->press_time = HAL_GetTick(); //记录按下时的Tick值
            p_ctrl_block->key_state = 1;
            p_ctrl_block->long_press_trigger = 0;   //重置长按标志
        }
        if(p_ctrl_block->key_state == 1){
            uint32_t press_duration = HAL_GetTick() - (p_ctrl_block->press_time);  //获取按键按下的持续时间

            if(press_duration >= DEBOUNCE_TIME){   //消抖完成
                if((press_duration >= KEY_LONG_PRESS_TIME) && (!p_ctrl_block->long_press_trigger)){
                    p_ctrl_block->key_event = DRV_KEY_EVENT_LONG_PRESS_TRIGGER;    //触发长按事件
                    p_ctrl_block->long_press_trigger = 1;
                }
                //短按等释放后判断
            }
        }
    }else{
        if(p_ctrl_block->key_state == 1){ //从按下到释放
            uint32_t press_duration = HAL_GetTick() - (p_ctrl_block->press_time);  //获取按键按下的持续时间

            if(press_duration >= DEBOUNCE_TIME){
                if((press_duration < KEY_LONG_PRESS_TIME) && (!p_ctrl_block->long_press_trigger)){  //短按的按键在长按状态下不会做出反应
                    p_ctrl_block->key_event = DRV_KEY_EVENT_PRESS;
                }
            }
        }
        //标志重置
        p_ctrl_block->key_state = 0;
        p_ctrl_block->long_press_trigger = 0;
    }
}

/**
 * @brief  驱动层提供的扫描接口，在定时器中调用
 * 
 */
void drv_key_scan_isr(){
    for(uint8_t i = 0; i < DRV_KEY_ID_MAX; i++){
        key_detect((drv_key_ctrl_block_t*)&drv_key_ctrl_block[i]);
    }
}

/**
 * @brief   按键底层初始化
 * @param   NULL
 * @retval  NULL
 */
static void key_msp_init(void){
    BSP_KEY0_CLK_ENABLE();
    BSP_KEY1_CLK_ENABLE();
    BSP_WKUP_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStructure = {0};

    //KEY0
    GPIO_InitStructure.Pin = BSP_KEY0_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_KEY0_PORT,&GPIO_InitStructure);

    //KEY1
    GPIO_InitStructure.Pin = BSP_KEY1_PIN;
    HAL_GPIO_Init(BSP_KEY1_PORT,&GPIO_InitStructure);

    //WKUP
    GPIO_InitStructure.Pin = BSP_WKUP_PIN;
    GPIO_InitStructure.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BSP_WKUP_PORT,&GPIO_InitStructure);
}

/**
 * @brief   按键初始化
 * @param   NULL
 * @retval  NULL
 */
status_t drv_key_init(void){
    key_msp_init();
    /* 注册按键扫描回调到TIM5 */
    timer_callback_register(TIMER_ID_TIM5, drv_key_scan_isr);
    return STATUS_OK;
}

/**
 * @brief 按键事件获取(中断保护，避免竞态问题)
 * @param key_id :按键ID
 * @return ekey_event 
 */
drv_key_event_t drv_key_get_event(uint8_t key_id){
    if(key_id >= DRV_KEY_ID_MAX){
        return STATUS_ERR_PARAM;
    }
    os_enter_critical();    //进入临界区
    drv_key_event_t event = drv_key_ctrl_block[key_id].key_event;
    drv_key_ctrl_block[key_id].key_event = DRV_KEY_EVENT_NONE;
    os_exit_critical();     //退出临界区
    return event;
}
