/*
 * cmd_protocol.c — 文本命令协议解析器实现
 *
 * 协议: 纯文本 ASCII, 换行符终止
 *   请求: CMD_NAME:param1,param2,...\n
 *   响应: JSON 对象, 换行符终止
 *
 * 解析器设计原则:
 *   - 零动态内存分配 (全部栈上处理, 适合嵌入式)
 *   - 容错处理: 未知命令返回错误 JSON (不崩溃)
 *   - 参数自动解析为 uint16_t (0-65535, 覆盖 LED 控制的所有范围)
 *   - snprintf 构建响应 (安全, 自动截断)
 *
 * 响应 JSON 格式:
 *   成功: {"cmd":"SET:color","status":"ok","data":{"r":255,"g":0,"b":0}}
 *   错误: {"cmd":"SET:color","status":"error","msg":"missing params"}
 */

 #include "cmd_protocol.h"
 #include "led_effects.h"
 #include "ws2812b.h"
 #include <stdio.h>
 #include <string.h>

 /*---------------------------------------------------------------------------*
 *  命令名 → 特效类型 映射表
 *---------------------------------------------------------------------------*/
typedef struct {
    const char   *name;
    effect_type_t type;
    uint8_t       min_params;
    uint8_t       max_params;
} cmd_map_t;

static const cmd_map_t g_cmd_map[] = {
    {"SET:color",     EFFECT_STATIC,   3, 3},  /* R,G,B */
    {"SET:brightness",EFFECT_STATIC,   1, 1},  /* level */
    {"SET:off",       EFFECT_OFF,      0, 0},  /* 无参数 */
    {"SET:rainbow",   EFFECT_RAINBOW,  1, 1},  /* speed */
    {"SET:breathe",   EFFECT_BREATHE,  4, 4},  /* R,G,B,speed */
    {"SET:chase",     EFFECT_CHASE,    5, 5},  /* R,G,B,length,speed */
    {"SET:gradient",  EFFECT_GRADIENT, 6, 6},  /* R1,G1,B1,R2,G2,B2 */
    {"SET:segment",   EFFECT_STATIC,   5, 5},  /* start,end,R,G,B (特殊处理) */
    {NULL,            0,               0, 0},  /* 哨兵 */
};

/*---------------------------------------------------------------------------*
 *  解析原始命令行
 *
 *  示例运行:
 *    输入 raw_line = "SET:rainbow:50"
 *    解析:
 *      1. 扫描到第一个 ':' → cmd_name = "SET:rainbow"
 *      2. 跳过 ':', 扫描参数: "50" → params[0] = 50
 *      3. 结果: cmd_name="SET:rainbow", params=[50], count=1
 *
 *  容错: 非数字字符直接跳过, 避免解析死循环
 *---------------------------------------------------------------------------*/
uint8_t cmd_parse(const char *raw_line, parsed_cmd_t *cmd)
{
    // 空指针校验
    if (raw_line == NULL || cmd == NULL) {
        return 1;
    }

    // 初始化输出结构
    memset(cmd, 0, sizeof(parsed_cmd_t));

    const char *last_colon = strrchr(raw_line, ':');
    const char *param_start = NULL;

    // -------------------------- 1. 智能提取命令名 --------------------------
    if (last_colon != NULL) {
        char next_char = *(last_colon + 1);
        // 判断冒号是否为参数分隔符（后面是数字或逗号）
        if (next_char >= '0' && next_char <= '9' || next_char == ',') {
            // 有参数：命令名 = 开头到最后一个冒号前
            size_t cmd_name_len = last_colon - raw_line;
            if (cmd_name_len > CMD_NAME_MAX_LEN - 1) {
                cmd_name_len = CMD_NAME_MAX_LEN - 1;
            }
            strncpy(cmd->cmd_name, raw_line, cmd_name_len);
            cmd->cmd_name[cmd_name_len] = '\0';
            param_start = last_colon + 1;
        } else {
            // 无参数：整个字符串都是命令名（冒号是命令名的一部分）
            strncpy(cmd->cmd_name, raw_line, CMD_NAME_MAX_LEN - 1);
            cmd->cmd_name[CMD_NAME_MAX_LEN - 1] = '\0';
            param_start = NULL;
        }
    } else {
        // 没有冒号：整个字符串都是命令名
        strncpy(cmd->cmd_name, raw_line, CMD_NAME_MAX_LEN - 1);
        cmd->cmd_name[CMD_NAME_MAX_LEN - 1] = '\0';
        param_start = NULL;
    }

    // -------------------------- 2. 处理GET查询命令 --------------------------
    if (strlen(cmd->cmd_name) >= 4 && strncmp(cmd->cmd_name, "GET:", 4) == 0) {
        cmd->is_get = 1;
        return 0;
    }
    cmd->is_get = 0;

    // -------------------------- 3. 解析参数（如果有） --------------------------
    if (param_start == NULL) {
        cmd->param_count = 0;
        return 0;
    }

    uint8_t i = 0;
    cmd->param_count = 0;
    while (param_start[i] != '\0' && cmd->param_count < CMD_PARAM_MAX) {
        uint16_t val = 0;
        uint8_t has_digit = 0;

        // 解析十进制整数
        while (param_start[i] >= '0' && param_start[i] <= '9') {

            uint16_t digit = (uint16_t)(param_start[i] - '0');
             // 溢出检测：uint16_t最大值是65535
            if (val > 6553 || (val == 6553 && digit > 5)) {
                has_digit = 0;
                // 跳过剩余的数字
                while (param_start[i] >= '0' && param_start[i] <= '9') {
                    i++;
                }
                break;
            }
            val = val * 10 + (uint16_t)(param_start[i] - '0');
            has_digit = 1;
            i++;
        }

        if (has_digit) {
            cmd->params[cmd->param_count++] = val;
        }

        // 跳过逗号或非数字字符
        if (param_start[i] == ',') {
            i++;
        } else if (param_start[i] != '\0' && !(param_start[i] >= '0' && param_start[i] <= '9')) {
            i++;
        }
    }

    return 0;
}

/*---------------------------------------------------------------------------*
 *  查找命令映射表
 *---------------------------------------------------------------------------*/
static const cmd_map_t* cmd_find(const char *name)
{
    for (uint8_t k = 0; g_cmd_map[k].name != NULL; k++) {
        if (strcmp(name, g_cmd_map[k].name) == 0) {
            return &g_cmd_map[k];
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------*
 *  执行命令 (验证 + 转换 + 构建响应)
 *
 *  流程:
 *    1. 查找命令映射表
 *    2. 验证参数个数
 *    3. 特殊命令处理 (SET:brightness 更新亮度, SET:segment 分段设置)
 *    4. 填充 effect_cmd_t 结构
 *    5. 构建 JSON 响应
 *---------------------------------------------------------------------------*/
uint8_t cmd_execute(const parsed_cmd_t *parsed, effect_cmd_t *effect, char *response, uint16_t resp_max_len)
{
    //处理 GET:status 查询命令
    if (parsed->is_get) {
        if (strcmp(parsed->cmd_name, "GET:status") == 0) {
            system_status_t status;
            led_effect_get_status(&status);
            cmd_build_status_response(&status, response, resp_max_len);
        } else {
            cmd_build_error(parsed->cmd_name, "unknown get command",
                            response, resp_max_len);
            return 1;
        }
        return 0;
    }

    // 查找命令映射
    const cmd_map_t *map = cmd_find(parsed->cmd_name);
    if (map == NULL) {
        cmd_build_error(parsed->cmd_name, "unknown command",
                        response, resp_max_len);
        return 1;
    }

    // 验证参数个数 
    if (parsed->param_count < map->min_params ||
        parsed->param_count > map->max_params) {
        cmd_build_error(parsed->cmd_name, "param count mismatch",
                        response, resp_max_len);
        return 1;
    }

    //特殊命令处理
    //SET:brightness 更新全局亮度
    if (strcmp(parsed->cmd_name, "SET:brightness") == 0) {
        uint16_t level = parsed->params[0];
        if (level > 100) {
            cmd_build_error(parsed->cmd_name, "brightness 0-100",
                            response, resp_max_len);
            return 1;
        }
        /* 将 0-100 映射到 0-255 */
        g_led_brightness = (uint8_t)((level * 255) / 100);

        snprintf(response, resp_max_len,
                 "{\"cmd\":\"SET:brightness\",\"status\":\"ok\","
                 "\"data\":{\"level\":%d}}\n", level);
        return 0;
    }

    //SET:segment — 分段设置 (特殊处理: 前两个参数是起止索引)
    if (strcmp(parsed->cmd_name, "SET:segment") == 0) {
        effect->type        = EFFECT_STATIC;
        effect->param_count = 5;  /* start, end, R, G, B */
        memcpy(effect->params, parsed->params, 5 * sizeof(uint16_t));

        snprintf(response, resp_max_len,
                 "{\"cmd\":\"SET:segment\",\"status\":\"ok\","
                 "\"data\":{\"start\":%d,\"end\":%d,"
                 "\"r\":%d,\"g\":%d,\"b\":%d}}\n",
                 parsed->params[0], parsed->params[1],
                 parsed->params[2], parsed->params[3], parsed->params[4]);
        return 0;
    }

    //通用命令处理
    //填充 effect_cmd_t
    effect->type = map->type;
    effect->param_count = parsed->param_count;
    memcpy(effect->params, parsed->params, parsed->param_count * sizeof(uint16_t));

    //构建成功响应
    char data_str[128] = {0};
    uint16_t offset = 0;
    for (uint8_t k = 0; k < effect->param_count; k++) {
        offset += (uint16_t)snprintf(data_str + offset,
                                      sizeof(data_str) - offset,
                                      "%s%d", (k > 0 ? "," : ""),
                                      effect->params[k]);
    }

    snprintf(response, resp_max_len,
             "{\"cmd\":\"%s\",\"status\":\"ok\",\"data\":{\"params\":[%s]}}\n",
             parsed->cmd_name, data_str);

    return 0;
}

/*---------------------------------------------------------------------------*
 *  构建 JSON 错误响应
 *---------------------------------------------------------------------------*/
void cmd_build_error(const char *cmd_name, const char *err_msg, char *out, uint16_t max_len)
{
    snprintf(out, max_len,
             "{\"cmd\":\"%s\",\"status\":\"error\",\"msg\":\"%s\"}\n",
             cmd_name, err_msg);
}

/*---------------------------------------------------------------------------*
 *  构建 GET:status JSON 响应
 *---------------------------------------------------------------------------*/
void cmd_build_status_response(const system_status_t *status,
                               char *out, uint16_t max_len)
{
    const char *effect_names[] = {
        "static", "rainbow", "breathe", "chase", "gradient", "off"
    };

    const char *eff_name = "unknown";
    if (status->current_effect < 6) {
        eff_name = effect_names[status->current_effect];
    }

    snprintf(out, max_len,
             "{\"cmd\":\"GET:status\",\"status\":\"ok\","
             "\"data\":{"
             "\"effect\":\"%s\","
             "\"brightness\":%d,"
             "\"led_count\":%d,"
             "\"connected\":%d"
             "}}\n",
             eff_name,
             status->brightness,
             status->led_count,
             status->is_connected);
}
