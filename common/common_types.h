/**
  ******************************************************************************
  * @file       common_types.h
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月25日
  * @brief      定义通用类型定义
  * @attention  仅供学习使用
  * @version    
  *             -V0.1
  * @note       NULL
  ******************************************************************************
**/
#ifndef __COMMON_TYPES_H__
#define __COMMON_TYPES_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

//状态码
typedef enum{
    STATUS_OK           = 0,    //操作成功
    STATUS_ERR          = -1,   //通用错误
    STATUS_ERR_PARAM    = -2,   //参数错误
    STATUS_ERR_TIMEOUT  = -3,   //超时错误
    STATUS_ERR_BUSY     = -4,   //资源忙/正在使用
    STATUS_ERR_NO_MEM   = -5    //内存不足
} status_t;                    

// 忽略未使用的变量，避免编译器警告 
#define UNUSED(x)   ((void)(x))

//计算数组元素的个数
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(a[0]))

#endif
