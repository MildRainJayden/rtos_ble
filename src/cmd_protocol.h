/*
 * cmd_protocol.h — 文本命令协议解析器
 *
 * 通信协议:
 *   请求格式: CMD_NAME:param1,param2,...\n
 *   响应格式: {"cmd":"CMD_NAME","status":"ok","data":{...}}\n
 *
 * 协议特点:
 *   - 纯文本 ASCII, 方便调试 (可用串口助手直接测试)
 *   - 无动态内存分配 (所有解析在栈上完成)
 *   - JSON 格式响应 (便于 Qt 端解析)
 *   - 容错: 格式错误返回明确错误信息
 *
 * 支持的命令:
 *   SET:color:R,G,B          — 设置全部 LED 为指定颜色
 *   SET:brightness:level      — 设置全局亮度 (0-100)
 *   SET:off                   — 关闭全部 LED
 *   SET:rainbow:speed         — 彩虹循环特效 (speed: 1-100)
 *   SET:breathe:R,G,B,speed   — 呼吸灯特效
 *   SET:chase:R,G,B,len,speed — 追色特效
 *   SET:gradient:R1,G1,B1,R2,G2,B2 — 双色渐变
 *   SET:segment:start,end,R,G,B — 分段颜色
 *   GET:status                — 查询当前状态
 */

 #ifndef CMD_PROTOCOL_H
 #define CMD_PROTOCOL_H

#include <stdint.h>

/*---------------------------------------------------------------------------*
 *  常量定义
 *---------------------------------------------------------------------------*/
//命令名字符串最大长度 (含 '\0')
#define CMD_NAME_MAX_LEN  24

//单条命令最大参数个数
#define CMD_PARAM_MAX           12

//响应缓冲区最大长度
#define CMD_RESPONSE_MAX_LEN    256

/*---------------------------------------------------------------------------*
 *  特效类型枚举
 *---------------------------------------------------------------------------*/
typedef enum {
    EFFECT_STATIC   = 0,  /* 静态颜色 (全部 LED 同色) */
    EFFECT_RAINBOW  = 1,  /* 彩虹循环 (HSV 色相旋转) */
    EFFECT_BREATHE  = 2,  /* 呼吸灯 (正弦亮度变化) */
    EFFECT_CHASE    = 3,  /* 追色 (移动亮条 + 衰减拖尾) */
    EFFECT_GRADIENT = 4,  /* 双色渐变 (线性插值) */
    EFFECT_OFF      = 5,  /* 关闭全部 */
} effect_type_t;

/*---------------------------------------------------------------------------*
 *  特效命令结构 (从 ble_cmd_task 发送到 led_effect_task)
 *---------------------------------------------------------------------------*/
typedef struct {
    effect_type_t type;                   /* 特效类型 */
    uint16_t      params[CMD_PARAM_MAX];  /* 参数值 (含义因 type 而异) */
    uint8_t       param_count;            /* 有效参数个数 */
} effect_cmd_t;

/*---------------------------------------------------------------------------*
 *  系统状态结构 (响应 GET:status 时填充)
 *---------------------------------------------------------------------------*/
typedef struct {
    effect_type_t current_effect;  /* 当前特效 */
    uint8_t       brightness;      /* 全局亮度 (0-100) */
    uint8_t       led_count;       /* LED 总数 */
    uint16_t      effect_params[6]; /* 当前特效参数 */
    uint8_t       is_connected;    /* BLE 连接状态 (0=断开, 1=已连接) */
} system_status_t;

/*---------------------------------------------------------------------------*
 *  解析结果结构
 *---------------------------------------------------------------------------*/
typedef struct {
    char     cmd_name[CMD_NAME_MAX_LEN];  /* 命令名 (如 "SET:rainbow") */
    uint16_t params[CMD_PARAM_MAX];       /* 解析出的参数值 */
    uint8_t  param_count;                 /* 有效参数个数 */
    uint8_t  is_get;                      /* 1=查询命令, 0=设置命令 */
} parsed_cmd_t;

/*---------------------------------------------------------------------------*
 *  API 函数
 *---------------------------------------------------------------------------*/

/* 解析原始命令行
 *
 * 输入: raw_line — 以 '\0' 结尾的文本命令 (不含 \n)
 * 输出: cmd      — 解析结果结构
 *
 * 返回: 0 = 成功, 1 = 格式错误
 *
 * 解析规则:
 *   1. 提取命令名 (第一个 ':' 之前的部分, 或整个字符串)
 *   2. 跳过 ':', 按逗号分隔提取参数 (仅支持十进制整数)
 *   3. 以 'GET:' 开头判定为查询命令
 *
 * 示例:
 *   "SET:rainbow:50" → cmd_name="SET:rainbow", params=[50], count=1
 *   "SET:color:255,0,0" → cmd_name="SET:color", params=[255,0,0], count=3
 *   "GET:status" → cmd_name="GET:status", is_get=1
 */
uint8_t cmd_parse(const char *raw_line, parsed_cmd_t *cmd);

/* 将解析后的命令转换为特效命令
 *
 * 输入: parsed — 解析后的命令
 * 输出: effect — 特效命令结构 (可直接发送到 led_effect_task)
 * 输出: response — 格式化的 JSON 响应字符串
 *
 * 返回: 0 = 成功, 1 = 参数错误
 *
 * 此函数负责:
 *   - 验证参数范围和个数
 *   - 映射命令名到特效类型
 *   - 构建 JSON 响应
 */
uint8_t cmd_execute(const parsed_cmd_t *parsed, effect_cmd_t *effect,
                    char *response, uint16_t resp_max_len);

/* 构建通用 JSON 错误响应
 *
 * 参数: cmd_name — 原始命令名 (如 "SET:invalid")
 *        err_msg  — 错误描述 (如 "unknown command")
 *        out      — 输出缓冲区
 *        max_len  — 输出缓冲区大小
 */
void cmd_build_error(const char *cmd_name, const char *err_msg,
                     char *out, uint16_t max_len);

/* 构建 GET:status 的 JSON 响应
 *
 * 参数: status  — 系统状态结构
 *        out     — 输出缓冲区
 *        max_len — 输出缓冲区大小
 */
void cmd_build_status_response(const system_status_t *status,
                               char *out, uint16_t max_len);
 #endif