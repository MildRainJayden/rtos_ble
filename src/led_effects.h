/*
 * led_effects.h — LED 灯带特效算法
 *
 * 支持的特效:
 *   EFFECT_STATIC   — 静态颜色: 全部 LED 显示统一颜色
 *   EFFECT_RAINBOW  — 彩虹循环: 色相沿灯带均匀分布 + 旋转动画
 *   EFFECT_BREATHE  — 呼吸灯: 亮度按正弦波变化
 *   EFFECT_CHASE    — 追色: 彩色光点沿灯带移动, 带衰减拖尾
 *   EFFECT_GRADIENT — 双色渐变: 从颜色1线性过渡到颜色2
 *   EFFECT_OFF      — 全部关闭
 *
 * 颜色空间:
 *   - 内部使用 HSV 空间便于特效计算
 *   - 输出使用 GRB 顺序 (WS2812B 原生格式): [G, R, B]
 *
 * 全局亮度:
 *   - g_led_brightness (0-255) 对最终输出进行缩放
 *   - 缩放公式: output = raw × g_led_brightness / 255
 */

 #ifndef LED_EFFECTS_H
 #define LED_EFFECTS_H

#include <stdint.h>





 #endif