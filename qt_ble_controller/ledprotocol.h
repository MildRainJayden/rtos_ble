/*
 * ledprotocol.h — LED 控制协议定义 (Qt 端, 与 STM32 cmd_protocol.h 对应)
 *
 * 协议格式:
 *   请求: CMD_NAME:param1,param2,...\n
 *   响应: {"cmd":"CMD_NAME","status":"ok","data":{...}}\n
 *
 * 纯头文件, 零 Qt 依赖, 可直接包含在任意 C++ 项目中。
 */

#ifndef LEDPROTOCOL_H
#define LEDPROTOCOL_H

#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>

/*---------------------------------------------------------------------------*
 *  命令构建
 *---------------------------------------------------------------------------*/

/* 设置全部 LED 为指定颜色
 * r, g, b: 0-255
 * 返回: "SET:color:255,0,0\n" */
inline QString buildSetColor(int r, int g, int b)
{
    return QString("SET:color:%1,%2,%3\n").arg(r).arg(g).arg(b);
}

/* 设置全局亮度
 * level: 0-100
 * 返回: "SET:brightness:80\n" */
inline QString buildSetBrightness(int level)
{
    return QString("SET:brightness:%1\n").arg(level);
}

/* 关闭全部 LED
 * 返回: "SET:off\n" */
inline QString buildSetOff()
{
    return QString("SET:off\n");
}

/* 彩虹循环特效
 * speed: 1-100
 * 返回: "SET:rainbow:50\n" */
inline QString buildSetRainbow(int speed)
{
    return QString("SET:rainbow:%1\n").arg(speed);
}

/* 呼吸灯特效
 * r, g, b: 目标颜色 0-255
 * speed: 速度 1-10
 * 返回: "SET:breathe:0,255,0,5\n" */
inline QString buildSetBreathe(int r, int g, int b, int speed)
{
    return QString("SET:breathe:%1,%2,%3,%4\n").arg(r).arg(g).arg(b).arg(speed);
}

/* 追色特效
 * r, g, b: 光点颜色 0-255
 * tail: 拖尾 LED 数 (1-30)
 * speed: 速度 1-10
 * 返回: "SET:chase:255,0,0,10,5\n" */
inline QString buildSetChase(int r, int g, int b, int tail, int speed)
{
    return QString("SET:chase:%1,%2,%3,%4,%5\n")
            .arg(r).arg(g).arg(b).arg(tail).arg(speed);
}

/* 双色渐变
 * r1,g1,b1: 起始颜色
 * r2,g2,b2: 结束颜色
 * 返回: "SET:gradient:255,0,0,0,0,255\n" */
inline QString buildSetGradient(int r1, int g1, int b1,
                                 int r2, int g2, int b2)
{
    return QString("SET:gradient:%1,%2,%3,%4,%5,%6\n")
            .arg(r1).arg(g1).arg(b1).arg(r2).arg(g2).arg(b2);
}

/* 分段设置
 * start, end: LED 索引范围 (含)
 * r, g, b: 颜色
 * 返回: "SET:segment:0,29,255,255,0\n" */
inline QString buildSetSegment(int start, int end, int r, int g, int b)
{
    return QString("SET:segment:%1,%2,%3,%4,%5\n")
            .arg(start).arg(end).arg(r).arg(g).arg(b);
}

/* 查询状态
 * 返回: "GET:status\n" */
inline QString buildGetStatus()
{
    return QString("GET:status\n");
}

/*---------------------------------------------------------------------------*
 *  响应解析
 *---------------------------------------------------------------------------*/

/* 解析后的状态信息 */
struct LedStatus {
    bool    valid;         /* 解析是否成功 */
    QString effect;        /* 当前特效名: static/rainbow/breathe/chase/gradient/off */
    int     brightness;    /* 亮度 0-100 */
    int     ledCount;      /* LED 数量 */
    bool    connected;     /* BLE 连接状态 */
};

/* 解析 GET:status 的 JSON 响应
 * json: 原始 JSON 字符串 (如 {"cmd":"GET:status","status":"ok","data":{...}})
 * 返回: LedStatus 结构, valid=true 表示解析成功 */
inline LedStatus parseStatusResponse(const QString &json)
{
    LedStatus st;
    st.valid = false;
    st.effect = "unknown";
    st.brightness = 0;
    st.ledCount = 0;
    st.connected = false;

    /* 解析 JSON */
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return st;
    }

    QJsonObject root = doc.object();
    if (root.value("status").toString() != "ok") {
        return st;
    }

    QJsonObject data = root.value("data").toObject();
    st.effect     = data.value("effect").toString("unknown");
    st.brightness = data.value("brightness").toInt(0);
    st.ledCount   = data.value("led_count").toInt(0);
    st.connected  = data.value("connected").toBool(false);
    st.valid      = true;

    return st;
}

/* 检查响应是否为成功
 * json: 原始 JSON 字符串
 * 返回: true 表示 status == "ok" */
inline bool isResponseOk(const QString &json)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    return doc.object().value("status").toString() == "ok";
}

/* 提取响应中的命令名
 * json: 原始 JSON 字符串
 * 返回: 命令名 (如 "SET:color") */
inline QString getResponseCmd(const QString &json)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    return doc.object().value("cmd").toString();
}

/* 提取错误消息
 * json: 原始 JSON 字符串
 * 返回: 错误描述, 如果无错误则返回空字符串 */
inline QString getResponseError(const QString &json)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString("JSON parse error: %1").arg(err.errorString());
    }
    QJsonObject root = doc.object();
    if (root.value("status").toString() == "error") {
        return root.value("msg").toString();
    }
    return QString();
}

#endif /* LEDPROTOCOL_H */
