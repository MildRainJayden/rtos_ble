/*
 * led_effects.c — LED 灯带特效算法实现
 *
 * 所有特效输出 GRB 格式 (WS2812B 原生颜色顺序)
 * 全局亮度 g_led_brightness (0-255) 在输出前缩放所有通道值
 *
 * 特效帧率设计 (基于 60 LED / 72MHz STM32):
 *   RAINBOW: 20 FPS (每 50ms 更新) — HSV 色相旋转, 平滑流畅
 *   BREATHE: 33 FPS (每 30ms 更新) — 正弦呼吸需要较高帧率
 *   CHASE:   12.5 FPS (每 80ms 更新) — 位置移动, 帧率要求低
 *   STATIC:  仅变化时更新 — 无动画
 *   GRADIENT: 仅变化时更新 — 无动画
 *
 * 颜色数学:
 *   HSV → RGB: 标准算法, 六段折线法 (非浮点, 全部整数运算)
 *   亮度缩放: output = raw * brightness / 255 (整数乘法 + 右移)
 */

 #include "led_effects.h"
 #include "ws2812b.h"
 #include <string.h>

 /*---------------------------------------------------------------------------*
 *  全局特效状态变量定义
 *---------------------------------------------------------------------------*/

 //当前激活特效类型，默认全关闭
effect_type_t g_current_effect = EFFECT_OFF;

//当前特效参数 (含义因特效类型而异, 见 led_effect_step 中各特效的具体说明)
uint16_t g_effect_params[CMD_PARAM_MAX] = {0};
uint8_t  g_effect_param_count = 0;

//特效运行标志： 0=停止, 1=运行中
uint8_t g_effect_running = 0;

/*---------------------------------------------------------------------------*
 *  特效内部状态 (仅在本文件内使用)
 *---------------------------------------------------------------------------*/

 //彩虹特效： 全局色相偏移量 (0-359), 每帧递增产生旋转效果
 static uint16_t g_rainbow_hue_offset = 0;

 //呼吸特效: 相位 (0-255), 映射到 0-2π 正弦周期
 static uint8_t  g_breathe_phase = 0;

 //追色特效: 头灯珠位置 (0 到 led_count-1), 循环移动
 static uint16_t g_chase_position = 0;

 /*---------------------------------------------------------------------------*
 *  HSV → RGB 8-bit 转换
 *
 *  算法说明:
 *    将 HSV 颜色空间转换为 RGB 24-bit
 *    基于六段折线法 (hexcone model), 全部整数运算
 *
 *  输入:
 *    h: 色相 0-359 (角度值)
 *    s: 饱和度 0-255 (0=灰色, 255=最鲜艳)
 *    v: 明度 0-255 (0=黑色, 255=最亮)
 *
 *  输出:
 *    r, g, b: 各 0-255
 *
 *  数学原理:
 *    region = h / 60        → 六个色段 (0-5)
 *    f = fraction of h      → 段内位置
 *    p = v × (1 - s)        → 最小分量
 *    q = v × (1 - s × f)    → 下降分量
 *    t = v × (1 - s × (1-f))→ 上升分量
 *
 *  六段分布:
 *    0: R↓ → 红→黄  (R max, G 上升)
 *    1: G↑ → 黄→绿  (G max, R 下降)
 *    2: B↑ → 绿→青  (G max, B 上升)
 *    3: G↓ → 青→蓝  (B max, G 下降)
 *    4: R↑ → 蓝→品  (B max, R 上升)
 *    5: B↓ → 品→红  (R max, B 下降)
 *
 *  注意: 使用 uint32_t 中间值防止 8 位乘法溢出
 *---------------------------------------------------------------------------*/
void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t  region;
    uint16_t remainder;
    uint8_t  p, q, t;

    //饱和度 = 0 → 纯灰色 (R=G=B=V)
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }

    //将色相映射到 0-5 段 
    region    = h / 60;
    remainder = (uint16_t)(((uint32_t)(h % 60) * 255) / 60);

    /* 计算三个关键分量 (全部整数, 除以 255 ≈ 右移 8 位) */
    p = (uint8_t)(((uint32_t)v * (255 - s)) >> 8);
    q = (uint8_t)(((uint32_t)v * (255 - ((uint32_t)s * remainder / 255))) >> 8);
    t = (uint8_t)(((uint32_t)v * (255 - ((uint32_t)s * (255 - remainder) / 255))) >> 8);

    //根据色段分配 RGB 
    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;  /* 红 → 黄 */
        case 1:  *r = q; *g = v; *b = p; break;  /* 黄 → 绿 */
        case 2:  *r = p; *g = v; *b = t; break;  /* 绿 → 青 */
        case 3:  *r = p; *g = q; *b = v; break;  /* 青 → 蓝 */
        case 4:  *r = t; *g = p; *b = v; break;  /* 蓝 → 品 */
        default: *r = v; *g = p; *b = q; break;  /* 品 → 红 */
    }
}

/*---------------------------------------------------------------------------*
 *  应用全局亮度缩放
 *
 *  output = raw * g_led_brightness / 255
 *  使用 16 位中间值防止溢出
 *---------------------------------------------------------------------------*/
static uint8_t apply_brightness(uint8_t raw)
{
    return (uint8_t)(((uint16_t)raw * g_led_brightness) >> 8);
}

/*---------------------------------------------------------------------------*
 *  将 RGB 值写入帧缓冲 (GRB 顺序)
 *
 *  WS2812B 颜色顺序为 GRB, 而非 RGB
 *  frame[offset+0] = G, frame[offset+1] = R, frame[offset+2] = B
 *
 *  亮度在写入前缩放
 *---------------------------------------------------------------------------*/
static void frame_set_led(uint8_t *frame, uint16_t index,
                          uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t offset = index * 3;
    frame[offset + 0] = apply_brightness(g);  /* WS2812B: G 先 */
    frame[offset + 1] = apply_brightness(r);  /* 然后是 R */
    frame[offset + 2] = apply_brightness(b);  /* 最后是 B */
}

/*---------------------------------------------------------------------------*
 *  特效: 静态颜色 (全部 LED 同色)
 *
 *  参数:
 *    g_effect_params[0] = R (0-255)
 *    g_effect_params[1] = G (0-255)
 *    g_effect_params[2] = B (0-255)
 *
 *  无动画: 仅调用一次即可 (或参数变更时重刷)
 *---------------------------------------------------------------------------*/
static void effect_static(uint8_t *frame, uint16_t led_count)
{
    uint8_t r = (uint8_t)g_effect_params[0];
    uint8_t g = (uint8_t)g_effect_params[1];
    uint8_t b = (uint8_t)g_effect_params[2];

    for (uint16_t i = 0; i < led_count; i++) {
        frame_set_led(frame, i, r, g, b);
    }
}

/*---------------------------------------------------------------------------*
 *  特效: 分段颜色 (设置指定范围内 LED 为指定颜色)
 *
 *  参数:
 *    g_effect_params[0] = start_index (含)
 *    g_effect_params[1] = end_index   (含)
 *    g_effect_params[2] = R
 *    g_effect_params[3] = G
 *    g_effect_params[4] = B
 *
 *  超出范围的 LED 保持原色不变
 *  注意: 此特效不改变 g_current_effect (仍为 EFFECT_STATIC)
 *---------------------------------------------------------------------------*/
static void effect_segment(uint8_t *frame, uint16_t led_count)
{
    uint16_t start = g_effect_params[0];
    uint16_t end   = g_effect_params[1];
    uint8_t  r     = (uint8_t)g_effect_params[2];
    uint8_t  g     = (uint8_t)g_effect_params[3];
    uint8_t  b     = (uint8_t)g_effect_params[4];

    /* 边界检查 */
    if (start >= led_count) start = 0;
    if (end >= led_count) end = led_count - 1;
    if (end < start) end = start;

    for (uint16_t i = start; i <= end; i++) {
        frame_set_led(frame, i, r, g, b);
    }
}

/*---------------------------------------------------------------------------*
 *  特效: 彩虹循环
 *
 *  参数:
 *    g_effect_params[0] = speed (1-100)
 *      speed=1:  每帧色相偏移 1° (慢)
 *      speed=50: 每帧色相偏移 20° (推荐默认)
 *      speed=100: 每帧色相偏移 36° (快)
 *
 *  原理:
 *    沿灯带均匀分布色相 (360° / 60 LED = 6°/LED)
 *    全局色相偏移量逐帧递增, 产生"色相旋转"动画
 *
 *  色相偏移公式:
 *    hue_offset += (speed * speed) / 25 + 1  → 非线性映射使速度可控
 *    实际公式简化: hue_offset += speed  (speed 直接作为偏移步长)
 *---------------------------------------------------------------------------*/
static void effect_rainbow(uint8_t *frame, uint16_t led_count)
{
    uint16_t speed = g_effect_params[0];  /* 0-100 */
    if (speed == 0) speed = 1;
    if (speed > 100) speed = 100;

    /* 非线性速度映射: 让低速段更精细, 高速段更快 */
    uint16_t step = (uint16_t)(((uint32_t)speed * speed) / 50 + 1);
    if (step > 60) step = 60;  /* 上限: 别太快 */

    /* 为每个 LED 计算色相: LED i 的基础色相 + 全局偏移 */
    for (uint16_t i = 0; i < led_count; i++) {
        /* LED 在灯带上的位置决定了它的基础色相 (0-359) */
        uint16_t hue = (uint16_t)(((uint32_t)i * 360) / led_count);

        /* 加上全局偏移产生旋转动画 */
        hue = (hue + g_rainbow_hue_offset) % 360;

        uint8_t r, g, b;
        hsv_to_rgb(hue, 255, 255, &r, &g, &b);
        frame_set_led(frame, i, r, g, b);
    }

    /* 更新全局色相偏移 */
    g_rainbow_hue_offset = (g_rainbow_hue_offset + step) % 360;
}

/*---------------------------------------------------------------------------*
 *  特效: 呼吸灯
 *
 *  参数:
 *    g_effect_params[0] = R (0-255) — 目标颜色
 *    g_effect_params[1] = G (0-255)
 *    g_effect_params[2] = B (0-255)
 *    g_effect_params[3] = speed (1-10) — 呼吸速度
 *
 *  原理:
 *    使用正弦波控制亮度: brightness(t) = sin(t) 映射到 [0, 255]
 *    三角波近似: phase 0→127 线性上升, 128→255 线性下降
 *    此近似比浮点 sin 快得多, 视觉效果几乎无差别
 *
 *  亮度计算:
 *    if phase < 128: value = phase * 2          (0 → 254)
 *    else:           value = (255 - phase) * 2  (254 → 0)
 *
 *  相位递增: phase += speed (1-10)
 *    speed=3 → 约 2.5 秒一个完整呼吸周期
 *---------------------------------------------------------------------------*/
static void effect_breathe(uint8_t *frame, uint16_t led_count)
{
    uint8_t r_target = (uint8_t)g_effect_params[0];
    uint8_t g_target = (uint8_t)g_effect_params[1];
    uint8_t b_target = (uint8_t)g_effect_params[2];
    uint8_t speed    = (uint8_t)g_effect_params[3];

    if (speed == 0) speed = 1;
    if (speed > 10) speed = 10;

    /* 三角波计算亮度系数 (0-255) */
    uint8_t factor;
    if (g_breathe_phase < 128) {
        factor = g_breathe_phase * 2;          /* 上升段: 0 → 254 */
    } else {
        factor = (255 - g_breathe_phase) * 2;  /* 下降段: 254 → 0 */
    }

    /* 对目标颜色应用亮度系数 */
    uint8_t r = (uint8_t)(((uint16_t)r_target * factor) >> 8);
    uint8_t g = (uint8_t)(((uint16_t)g_target * factor) >> 8);
    uint8_t b = (uint8_t)(((uint16_t)b_target * factor) >> 8);

    for (uint16_t i = 0; i < led_count; i++) {
        frame_set_led(frame, i, r, g, b);
    }

    /* 更新相位 */
    g_breathe_phase += speed;
}

/*---------------------------------------------------------------------------*
 *  特效: 追色
 *
 *  参数:
 *    g_effect_params[0] = R — 光点颜色
 *    g_effect_params[1] = G
 *    g_effect_params[2] = B
 *    g_effect_params[3] = tail_length — 拖尾 LED 数 (1-30)
 *    g_effect_params[4] = speed — 移动速度 (1-10, 每帧移动 LED 数)
 *
 *  原理:
 *    一个亮色"头"沿灯带循环移动, 身后 LED 按距离衰减
 *    头经过的每个 LED 指数衰减: brightness = 255 * (decay ^ distance)
 *
 *  衰减算法:
 *    每个 LED 的亮度 = 255 * (decay_rate ^ distance_from_head)
 *    decay_rate = exp(log(1/256) / tail_length)
 *               ≈ 256 ^ (-distance / tail_length)
 *
 *    简化为整数查表法:
 *      tail=5  → decay=[255, 128, 64, 32, 16, 8, 0...]
 *      tail=10 → decay=[255, 198, 153, 119, 92, 71, 55, 43, 33, 26, 20, 0...]
 *
 *    使用预先计算的衰减表以节省 CPU
 *---------------------------------------------------------------------------*/
static void effect_chase(uint8_t *frame, uint16_t led_count)
{
    uint8_t  r_head  = (uint8_t)g_effect_params[0];
    uint8_t  g_head  = (uint8_t)g_effect_params[1];
    uint8_t  b_head  = (uint8_t)g_effect_params[2];
    uint16_t tail_len = g_effect_params[3];  /* 拖尾长度 */
    uint8_t  speed   = (uint8_t)g_effect_params[4];

    if (tail_len == 0) tail_len = 5;
    if (tail_len > 30) tail_len = 30;
    if (speed == 0) speed = 1;
    if (speed > 10) speed = 10;

    /* 衰减因子 (预计算): decay_factor = 256 ^ (-1/tail_len) 的整数近似
     * 简化: 使用线性衰减 (距离 d 的 LED 亮度 = 255 * (tail_len-d) / tail_len) */
    for (uint16_t i = 0; i < led_count; i++) {
        /* 计算 LED i 到头位置的距离 (循环距离) */
        int16_t dist = (int16_t)i - (int16_t)g_chase_position;

        /* 处理循环: 如果距离为负, 加上总长度即为另一方向的循环距离
         * 但我们只用正向距离 (头后面的 LED) */
        if (dist < 0) dist += (int16_t)led_count;

        /* 如果距离超过拖尾长度, 该 LED 暗 */
        if ((uint16_t)dist >= tail_len) {
            frame_set_led(frame, i, 0, 0, 0);
        } else {
            /* 线性衰减: factor = 255 * (tail_len - dist) / tail_len */
            uint8_t factor = (uint8_t)(((uint32_t)255 * (tail_len - (uint16_t)dist)) / tail_len);
            uint8_t r = (uint8_t)(((uint16_t)r_head * factor) >> 8);
            uint8_t g = (uint8_t)(((uint16_t)g_head * factor) >> 8);
            uint8_t b = (uint8_t)(((uint16_t)b_head * factor) >> 8);
            frame_set_led(frame, i, r, g, b);
        }
    }

    /* 移动头位置 (减 direction 产生正向移动) */
    g_chase_position = (g_chase_position + speed) % led_count;
}

/*---------------------------------------------------------------------------*
 *  特效: 双色渐变
 *
 *  参数:
 *    g_effect_params[0] = R1 — 起始颜色 (LED 0 的颜色)
 *    g_effect_params[1] = G1
 *    g_effect_params[2] = B1
 *    g_effect_params[3] = R2 — 结束颜色 (LED 59 的颜色)
 *    g_effect_params[4] = G2
 *    g_effect_params[5] = B2
 *
 *  原理:
 *    沿灯带线性插值: LED i 的颜色 = color1 + (color2 - color1) * i / (N-1)
 *    公式: c[i] = c1 + (c2 - c1) * i / (led_count - 1)
 *
 *  无动画: 渐变是静态的
 *---------------------------------------------------------------------------*/
static void effect_gradient(uint8_t *frame, uint16_t led_count)
{
    uint8_t r1 = (uint8_t)g_effect_params[0];
    uint8_t g1 = (uint8_t)g_effect_params[1];
    uint8_t b1 = (uint8_t)g_effect_params[2];
    uint8_t r2 = (uint8_t)g_effect_params[3];
    uint8_t g2 = (uint8_t)g_effect_params[4];
    uint8_t b2 = (uint8_t)g_effect_params[5];

    uint16_t last = led_count - 1;

    for (uint16_t i = 0; i < led_count; i++) {
        /* 线性插值: t = i / last (0.0 ~ 1.0, 用 0-255 表示) */
        uint8_t t = (uint8_t)(((uint32_t)i * 255) / last);

        /* c = c1 + (c2 - c1) * t */
        uint8_t r = (uint8_t)(r1 + ((int16_t)(r2 - r1) * t) / 255);
        uint8_t g = (uint8_t)(g1 + ((int16_t)(g2 - g1) * t) / 255);
        uint8_t b = (uint8_t)(b1 + ((int16_t)(b2 - b1) * t) / 255);

        frame_set_led(frame, i, r, g, b);
    }
}

/*---------------------------------------------------------------------------*
 *  特效: 全灭
 *
 *  将所有 LED 设为 (0, 0, 0)
 *---------------------------------------------------------------------------*/
static void effect_off(uint8_t *frame, uint16_t led_count)
{
    for (uint16_t i = 0; i < led_count; i++) {
        frame_set_led(frame, i, 0, 0, 0);
    }
}

/*---------------------------------------------------------------------------*
 *  led_effect_step — 计算一帧 LED 数据
 *
 *  被 led_effect_task 定期调用 (频率取决于特效类型)
 *  根据 g_current_effect 分发到对应的特效函数
 *
 *  参数:
 *    frame     — 输出帧缓冲 (GRB 格式, LED_COUNT × 3 字节)
 *    led_count — LED 数量
 *---------------------------------------------------------------------------*/
void led_effect_step(uint8_t *frame, uint16_t led_count)
{
    uint8_t is_segment = (g_effect_params[0] != 0 || g_effect_params[1] != 0)
                         && g_current_effect == EFFECT_STATIC
                         && g_effect_param_count == 5;

    switch (g_current_effect) {
        case EFFECT_STATIC:
            if (is_segment) {
                effect_segment(frame, led_count);
            } else {
                effect_static(frame, led_count);
            }
            break;

        case EFFECT_RAINBOW:
            effect_rainbow(frame, led_count);
            break;

        case EFFECT_BREATHE:
            effect_breathe(frame, led_count);
            break;

        case EFFECT_CHASE:
            effect_chase(frame, led_count);
            break;

        case EFFECT_GRADIENT:
            effect_gradient(frame, led_count);
            break;

        case EFFECT_OFF:
        default:
            effect_off(frame, led_count);
            break;
    }
}

/*---------------------------------------------------------------------------*
 *  led_effect_set_command — 接收特效切换命令
 *
 *  从 ble_cmd_task 接收 effect_cmd_t, 更新全局特效状态
 *  重置特效内部状态 (相位/偏移/位置等)
 *
 *  SET:segment 特殊处理: 不改 g_current_effect, 直接更新参数
 *---------------------------------------------------------------------------*/
void led_effect_set_command(const effect_cmd_t *cmd)
{
    g_effect_running = 0;  /* 暂停当前特效计算 */

    /* 保存新特效参数 */
    g_current_effect    = cmd->type;
    g_effect_param_count = cmd->param_count;
    memcpy(g_effect_params, cmd->params, cmd->param_count * sizeof(uint16_t));

    /* 重置特效内部状态 */
    g_rainbow_hue_offset = 0;
    g_breathe_phase      = 0;
    g_chase_position     = 0;

    g_effect_running = 1;  /* 启动新特效 */
}

/*---------------------------------------------------------------------------*
 *  led_effect_init — 初始化特效状态
 *
 *  设置默认状态: 全部关闭, 无激活特效
 *---------------------------------------------------------------------------*/
void led_effect_init(void)
{
    g_current_effect    = EFFECT_OFF;
    g_effect_running    = 0;
    g_effect_param_count = 0;
    g_led_brightness    = 128;  /* 50% 默认亮度 */

    memset(g_effect_params, 0, sizeof(g_effect_params));

    g_rainbow_hue_offset = 0;
    g_breathe_phase      = 0;
    g_chase_position     = 0;

    /* 帧缓冲清零 */
    memset(g_led_frame, 0, sizeof(g_led_frame));
}

/*---------------------------------------------------------------------------*
 *  led_effect_get_status — 填充系统状态 (响应 GET:status)
 *---------------------------------------------------------------------------*/
void led_effect_get_status(system_status_t *status)
{
    memset(status, 0, sizeof(system_status_t));

    status->current_effect = g_current_effect;
    status->brightness     = (uint8_t)((g_led_brightness * 100) / 255);  /* 转百分比 */
    status->led_count      = LED_COUNT;
    status->is_connected   = 1;  /* 默认认为已连接 */

    memcpy(status->effect_params, g_effect_params,
           g_effect_param_count * sizeof(uint16_t));
}
