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
#include "cmd_protocol.h"

/*---------------------------------------------------------------------------*
 *  特效状态 (全局, 由 led_effect_task 维护)
 *---------------------------------------------------------------------------*/

 //当前激活的特效类型
 extern effect_type_t g_current_effect;

 //当前特效参数 (最多 12 个, 含义因特效类型而异)
extern uint16_t g_effect_params[CMD_PARAM_MAX];
extern uint8_t  g_effect_param_count;

//特效状态标志: 0=停止, 1=运行中
extern uint8_t g_effect_running;

/*---------------------------------------------------------------------------*
 *  API 函数
 *---------------------------------------------------------------------------*/

/* 处理单个特效步进 (计算一帧 GRB 数据)
 *
 * 输入: 全局变量 g_current_effect, g_effect_params, g_led_brightness
 * 输出: frame[LED_COUNT * 3] — GRB 格式的帧数据
 *
 * 调用时机: led_effect_task 中定期调用
 * 调用频率: 取决于特效类型
 *   - STATIC: 仅在参数变更时调用一次
 *   - RAINBOW: 每 50ms 调用一次 (speed 参数调节每次的色相偏移量)
 *   - BREATHE: 每 30ms 调用一次
 *   - CHASE: 每 80ms 调用一次
 */
void led_effect_step(uint8_t *frame, uint16_t led_count);

/* 将特效命令参数复制到全局特效状态
 *
 * 输入: cmd — 来自 ble_cmd_task 的特效命令
 *
 * 此函数更新 g_current_effect, g_effect_params[], g_effect_param_count
 * 并设置 g_effect_running = 1
 */
void led_effect_set_command(const effect_cmd_t *cmd);

//初始化特效状态，默认全部关闭
void led_effect_init(void);

//填充系统状态结构，响应 GET:status
void led_effect_get_status(system_status_t *status);

/*---------------------------------------------------------------------------*
 *  颜色空间转换工具 (亦可被其他模块使用)
 *---------------------------------------------------------------------------*/

/* HSV 转 RGB 8-bit
 * 输入: h — 色相 (0-359)
 *        s — 饱和度 (0-255)
 *        v — 明度 (0-255)
 * 输出: r, g, b — 各通道 8-bit 值
 */
void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b);

 #endif