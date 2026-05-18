/*
 * MIT License
 *
 * Copyright (c) 2024 YuYuKunKun
 *
 * chan_wrapper.c — English-named C wrappers for Python ctypes bindings.
 * Compile with -DPYTHON_WRAPPER for Python C API integration.
 */

#include "chan_wrapper.h"
#include "chan.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef PYTHON_WRAPPER
#include <Python.h>
#endif

/* ================================================================
 * OOM / fatal error handler
 * ================================================================ */

static chan_fatal_callback g_fatal_cb = NULL;

void chan_set_fatal_callback(chan_fatal_callback cb) {
    g_fatal_cb = cb;
}

#ifdef PYTHON_WRAPPER
static void python_oom_handler(size_t sz) {
    if (g_fatal_cb) {
        char msg[256];
        snprintf(msg, sizeof(msg), "chan_c99: allocation of %zu bytes failed", sz);
        g_fatal_cb(msg);
    }
    PyErr_Format(PyExc_MemoryError,
                 "chan_c99: allocation of %zu bytes failed", sz);
}

__attribute__((constructor))
static void _init_python_handlers(void) {
    chan_oom_handler = python_oom_handler;
}
#endif

/* ================================================================
 * Memory debug
 * ================================================================ */

void chan_memory_summary(void)            { chan_memory_diagnostics(); }
int  chan_allocation_count(int type_tag)  { return chan_cnt_alloc(type_tag); }
int  chan_free_count(int type_tag)        { return chan_cnt_free(type_tag); }
void chan_set_debug_mem(int enabled)     { CHAN_DEBUG_MEM = (bool)enabled; }

/* ================================================================
 * Config — string-keyed field dispatch
 * ================================================================ */

void* chan_config_new(void)          { return 缠论配置_新建(); }
void* chan_config_new_no_push(void)  { return 缠论配置_不推送(); }
void  chan_config_release(void* c)   { if (c) 解引用(c); }

#define CONFIG_SET_IMPL(ctype, field) do { \
    缠论配置* _c = (缠论配置*)config; \
    if (strcmp(field, #field) == 0) { _c->field = value; return; } \
} while(0)

#define CONFIG_GET_INT_IMPL(field) do { \
    缠论配置* _c = (缠论配置*)config; \
    if (strcmp(field, #field) == 0) return _c->field; \
} while(0)

#define CONFIG_GET_STR_IMPL(field) do { \
    缠论配置* _c = (缠论配置*)config; \
    if (strcmp(field, #field) == 0) return _c->field; \
} while(0)

void chan_config_set_bool(void* config, const char* field, int value) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "缠K合并替换") == 0) c->缠K合并替换 = (bool)value;
    else if (strcmp(field, "笔内相同终点取舍") == 0) c->笔内相同终点取舍 = (bool)value;
    else if (strcmp(field, "笔内起始分型包含整笔") == 0) c->笔内起始分型包含整笔 = (bool)value;
    else if (strcmp(field, "笔内起始分型包含整笔_包括右") == 0) c->笔内起始分型包含整笔_包括右 = (bool)value;
    else if (strcmp(field, "笔内原始K线包含整笔") == 0) c->笔内原始K线包含整笔 = (bool)value;
    else if (strcmp(field, "笔次级成笔") == 0) c->笔次级成笔 = (bool)value;
    else if (strcmp(field, "笔弱化") == 0) c->笔弱化 = (bool)value;
    else if (strcmp(field, "线段_非缺口下穿刺") == 0) c->线段_非缺口下穿刺 = (bool)value;
    else if (strcmp(field, "线段_特征序列忽视老阴老阳") == 0) c->线段_特征序列忽视老阴老阳 = (bool)value;
    else if (strcmp(field, "线段_缺口后紧急修正") == 0) c->线段_缺口后紧急修正 = (bool)value;
    else if (strcmp(field, "线段_修正") == 0) c->线段_修正 = (bool)value;
    else if (strcmp(field, "线段内部中枢图显") == 0) c->线段内部中枢图显 = (bool)value;
    else if (strcmp(field, "扩展线段_当下分析") == 0) c->扩展线段_当下分析 = (bool)value;
    else if (strcmp(field, "分析笔") == 0) c->分析笔 = (bool)value;
    else if (strcmp(field, "分析线段") == 0) c->分析线段 = (bool)value;
    else if (strcmp(field, "分析扩展线段") == 0) c->分析扩展线段 = (bool)value;
    else if (strcmp(field, "分析笔中枢") == 0) c->分析笔中枢 = (bool)value;
    else if (strcmp(field, "分析线段中枢") == 0) c->分析线段中枢 = (bool)value;
    else if (strcmp(field, "计算指标") == 0) c->计算指标 = (bool)value;
    else if (strcmp(field, "图表展示") == 0) c->图表展示 = (bool)value;
    else if (strcmp(field, "推送K线") == 0) c->推送K线 = (bool)value;
    else if (strcmp(field, "推送笔") == 0) c->推送笔 = (bool)value;
    else if (strcmp(field, "推送线段") == 0) c->推送线段 = (bool)value;
    else if (strcmp(field, "推送中枢") == 0) c->推送中枢 = (bool)value;
    else if (strcmp(field, "图表展示_笔") == 0) c->图表展示_笔 = (bool)value;
    else if (strcmp(field, "图表展示_线段") == 0) c->图表展示_线段 = (bool)value;
    else if (strcmp(field, "图表展示_扩展线段") == 0) c->图表展示_扩展线段 = (bool)value;
    else if (strcmp(field, "图表展示_扩展线段_线段") == 0) c->图表展示_扩展线段_线段 = (bool)value;
    else if (strcmp(field, "图表展示_线段_线段") == 0) c->图表展示_线段_线段 = (bool)value;
    else if (strcmp(field, "图表展示_中枢_笔") == 0) c->图表展示_中枢_笔 = (bool)value;
    else if (strcmp(field, "图表展示_中枢_线段") == 0) c->图表展示_中枢_线段 = (bool)value;
    else if (strcmp(field, "图表展示_中枢_扩展线段") == 0) c->图表展示_中枢_扩展线段 = (bool)value;
    else if (strcmp(field, "图表展示_中枢_扩展线段_线段") == 0) c->图表展示_中枢_扩展线段_线段 = (bool)value;
    else if (strcmp(field, "图表展示_中枢_线段_线段") == 0) c->图表展示_中枢_线段_线段 = (bool)value;
    else if (strcmp(field, "图表展示_中枢_线段内部") == 0) c->图表展示_中枢_线段内部 = (bool)value;
    else if (strcmp(field, "买卖点激进识别") == 0) c->买卖点激进识别 = (bool)value;
    else if (strcmp(field, "买卖点与MACD柱强相关") == 0) c->买卖点与MACD柱强相关 = (bool)value;
    else if (strcmp(field, "买卖点_指标匹配_MACD") == 0) c->买卖点_指标匹配_MACD = (bool)value;
    else if (strcmp(field, "买卖点_指标匹配_KDJ") == 0) c->买卖点_指标匹配_KDJ = (bool)value;
    else if (strcmp(field, "买卖点_指标匹配_RSI") == 0) c->买卖点_指标匹配_RSI = (bool)value;
    else if (strcmp(field, "买卖点_峰值条件") == 0) c->买卖点_峰值条件 = (bool)value;
    else if (strcmp(field, "买卖点_计算线段BSP1") == 0) c->买卖点_计算线段BSP1 = (bool)value;
    else if (strcmp(field, "买卖点_处理BSP2") == 0) c->买卖点_处理BSP2 = (bool)value;
    else if (strcmp(field, "买卖点_计算线段BSP3") == 0) c->买卖点_计算线段BSP3 = (bool)value;
    else if (strcmp(field, "买卖点_依赖T1") == 0) c->买卖点_依赖T1 = (bool)value;
    else if (strcmp(field, "买卖点_调试输出") == 0) c->买卖点_调试输出 = (bool)value;
    else if (strcmp(field, "线段内部背驰_MACD") == 0) c->线段内部背驰_MACD = (bool)value;
    else if (strcmp(field, "线段内部背驰_斜率") == 0) c->线段内部背驰_斜率 = (bool)value;
    else if (strcmp(field, "线段内部背驰_测度") == 0) c->线段内部背驰_测度 = (bool)value;
    else {
        fprintf(stderr, "chan_config_set_bool: unknown field '%s'\n", field);
    }
}

void chan_config_set_int(void* config, const char* field, int value) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "笔内元素数量") == 0) c->笔内元素数量 = value;
    else if (strcmp(field, "笔弱化_原始数量") == 0) c->笔弱化_原始数量 = value;
    else if (strcmp(field, "买卖点偏移") == 0) c->买卖点偏移 = value;
    else if (strcmp(field, "买卖点_T2S_最大层级") == 0) c->买卖点_T2S_最大层级 = value;
    else if (strcmp(field, "平滑异同移动平均线_快线周期") == 0) c->平滑异同移动平均线_快线周期 = value;
    else if (strcmp(field, "平滑异同移动平均线_慢线周期") == 0) c->平滑异同移动平均线_慢线周期 = value;
    else if (strcmp(field, "平滑异同移动平均线_信号周期") == 0) c->平滑异同移动平均线_信号周期 = value;
    else if (strcmp(field, "相对强弱指数_周期") == 0) c->相对强弱指数_周期 = value;
    else if (strcmp(field, "相对强弱指数_移动平均线周期") == 0) c->相对强弱指数_移动平均线周期 = value;
    else if (strcmp(field, "随机指标_RSV周期") == 0) c->随机指标_RSV周期 = value;
    else if (strcmp(field, "随机指标_K值平滑周期") == 0) c->随机指标_K值平滑周期 = value;
    else if (strcmp(field, "随机指标_D值平滑周期") == 0) c->随机指标_D值平滑周期 = value;
    else {
        fprintf(stderr, "chan_config_set_int: unknown field '%s'\n", field);
    }
}

void chan_config_set_double(void* config, const char* field, double value) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "相对强弱指数_超买阈值") == 0) c->相对强弱指数_超买阈值 = value;
    else if (strcmp(field, "相对强弱指数_超卖阈值") == 0) c->相对强弱指数_超卖阈值 = value;
    else if (strcmp(field, "随机指标_超买阈值") == 0) c->随机指标_超买阈值 = value;
    else if (strcmp(field, "随机指标_超卖阈值") == 0) c->随机指标_超卖阈值 = value;
    else if (strcmp(field, "买卖点错过误差值") == 0) c->买卖点错过误差值 = value;
    else if (strcmp(field, "买卖点_背离率") == 0) c->买卖点_背离率 = value;
    else if (strcmp(field, "买卖点_T2_回调阈值") == 0) c->买卖点_T2_回调阈值 = value;
    else {
        fprintf(stderr, "chan_config_set_double: unknown field '%s'\n", field);
    }
}

void chan_config_set_string(void* config, const char* field, const char* value) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "手动终止") == 0) {
        c->手动终止[0] = '\0'; strncpy(c->手动终止, value, 31); c->手动终止[31] = '\0';
    }
    else if (strcmp(field, "指标计算方式") == 0) {
        c->指标计算方式[0] = '\0'; strncpy(c->指标计算方式, value, 31); c->指标计算方式[31] = '\0';
    }
    else if (strcmp(field, "买卖点_指标模式") == 0) {
        c->买卖点_指标模式[0] = '\0'; strncpy(c->买卖点_指标模式, value, 31); c->买卖点_指标模式[31] = '\0';
    }
    else if (strcmp(field, "买卖点_计算方式") == 0) {
        c->买卖点_计算方式[0] = '\0'; strncpy(c->买卖点_计算方式, value, 31); c->买卖点_计算方式[31] = '\0';
    }
    else if (strcmp(field, "买卖点_中枢来源") == 0) {
        c->买卖点_中枢来源[0] = '\0'; strncpy(c->买卖点_中枢来源, value, 31); c->买卖点_中枢来源[31] = '\0';
    }
    else if (strcmp(field, "线段内部背驰_模式") == 0) {
        c->线段内部背驰_模式[0] = '\0'; strncpy(c->线段内部背驰_模式, value, 31); c->线段内部背驰_模式[31] = '\0';
    }
    else if (strcmp(field, "标识") == 0) {
        c->标识[0] = '\0'; strncpy(c->标识, value, 63); c->标识[63] = '\0';
    }
    else if (strcmp(field, "加载文件路径") == 0) {
        c->加载文件路径[0] = '\0'; strncpy(c->加载文件路径, value, 255); c->加载文件路径[255] = '\0';
    }
    else {
        fprintf(stderr, "chan_config_set_string: unknown field '%s'\n", field);
    }
}

int chan_config_get_bool(void* config, const char* field) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "缠K合并替换") == 0) return c->缠K合并替换;
    else if (strcmp(field, "笔内相同终点取舍") == 0) return c->笔内相同终点取舍;
    else if (strcmp(field, "笔内起始分型包含整笔") == 0) return c->笔内起始分型包含整笔;
    else if (strcmp(field, "笔内起始分型包含整笔_包括右") == 0) return c->笔内起始分型包含整笔_包括右;
    else if (strcmp(field, "笔内原始K线包含整笔") == 0) return c->笔内原始K线包含整笔;
    else if (strcmp(field, "笔次级成笔") == 0) return c->笔次级成笔;
    else if (strcmp(field, "笔弱化") == 0) return c->笔弱化;
    else if (strcmp(field, "线段_非缺口下穿刺") == 0) return c->线段_非缺口下穿刺;
    else if (strcmp(field, "线段_特征序列忽视老阴老阳") == 0) return c->线段_特征序列忽视老阴老阳;
    else if (strcmp(field, "线段_缺口后紧急修正") == 0) return c->线段_缺口后紧急修正;
    else if (strcmp(field, "线段_修正") == 0) return c->线段_修正;
    else if (strcmp(field, "线段内部中枢图显") == 0) return c->线段内部中枢图显;
    else if (strcmp(field, "扩展线段_当下分析") == 0) return c->扩展线段_当下分析;
    else if (strcmp(field, "分析笔") == 0) return c->分析笔;
    else if (strcmp(field, "分析线段") == 0) return c->分析线段;
    else if (strcmp(field, "分析扩展线段") == 0) return c->分析扩展线段;
    else if (strcmp(field, "分析笔中枢") == 0) return c->分析笔中枢;
    else if (strcmp(field, "分析线段中枢") == 0) return c->分析线段中枢;
    else if (strcmp(field, "计算指标") == 0) return c->计算指标;
    else if (strcmp(field, "图表展示") == 0) return c->图表展示;
    else if (strcmp(field, "推送K线") == 0) return c->推送K线;
    else if (strcmp(field, "推送笔") == 0) return c->推送笔;
    else if (strcmp(field, "推送线段") == 0) return c->推送线段;
    else if (strcmp(field, "推送中枢") == 0) return c->推送中枢;
    else if (strcmp(field, "图表展示_笔") == 0) return c->图表展示_笔;
    else if (strcmp(field, "图表展示_线段") == 0) return c->图表展示_线段;
    else if (strcmp(field, "图表展示_扩展线段") == 0) return c->图表展示_扩展线段;
    else if (strcmp(field, "图表展示_扩展线段_线段") == 0) return c->图表展示_扩展线段_线段;
    else if (strcmp(field, "图表展示_线段_线段") == 0) return c->图表展示_线段_线段;
    else if (strcmp(field, "图表展示_中枢_笔") == 0) return c->图表展示_中枢_笔;
    else if (strcmp(field, "图表展示_中枢_线段") == 0) return c->图表展示_中枢_线段;
    else if (strcmp(field, "图表展示_中枢_扩展线段") == 0) return c->图表展示_中枢_扩展线段;
    else if (strcmp(field, "图表展示_中枢_扩展线段_线段") == 0) return c->图表展示_中枢_扩展线段_线段;
    else if (strcmp(field, "图表展示_中枢_线段_线段") == 0) return c->图表展示_中枢_线段_线段;
    else if (strcmp(field, "图表展示_中枢_线段内部") == 0) return c->图表展示_中枢_线段内部;
    else if (strcmp(field, "买卖点激进识别") == 0) return c->买卖点激进识别;
    else if (strcmp(field, "买卖点与MACD柱强相关") == 0) return c->买卖点与MACD柱强相关;
    else if (strcmp(field, "买卖点_指标匹配_MACD") == 0) return c->买卖点_指标匹配_MACD;
    else if (strcmp(field, "买卖点_指标匹配_KDJ") == 0) return c->买卖点_指标匹配_KDJ;
    else if (strcmp(field, "买卖点_指标匹配_RSI") == 0) return c->买卖点_指标匹配_RSI;
    else if (strcmp(field, "买卖点_峰值条件") == 0) return c->买卖点_峰值条件;
    else if (strcmp(field, "买卖点_计算线段BSP1") == 0) return c->买卖点_计算线段BSP1;
    else if (strcmp(field, "买卖点_处理BSP2") == 0) return c->买卖点_处理BSP2;
    else if (strcmp(field, "买卖点_计算线段BSP3") == 0) return c->买卖点_计算线段BSP3;
    else if (strcmp(field, "买卖点_依赖T1") == 0) return c->买卖点_依赖T1;
    else if (strcmp(field, "买卖点_调试输出") == 0) return c->买卖点_调试输出;
    else if (strcmp(field, "线段内部背驰_MACD") == 0) return c->线段内部背驰_MACD;
    else if (strcmp(field, "线段内部背驰_斜率") == 0) return c->线段内部背驰_斜率;
    else if (strcmp(field, "线段内部背驰_测度") == 0) return c->线段内部背驰_测度;
    else { return 0; }
    return 0;
}

int chan_config_get_int(void* config, const char* field) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "笔内元素数量") == 0) return c->笔内元素数量;
    else if (strcmp(field, "笔弱化_原始数量") == 0) return c->笔弱化_原始数量;
    else if (strcmp(field, "买卖点偏移") == 0) return c->买卖点偏移;
    else if (strcmp(field, "买卖点_T2S_最大层级") == 0) return c->买卖点_T2S_最大层级;
    else if (strcmp(field, "平滑异同移动平均线_快线周期") == 0) return c->平滑异同移动平均线_快线周期;
    else if (strcmp(field, "平滑异同移动平均线_慢线周期") == 0) return c->平滑异同移动平均线_慢线周期;
    else if (strcmp(field, "平滑异同移动平均线_信号周期") == 0) return c->平滑异同移动平均线_信号周期;
    else if (strcmp(field, "相对强弱指数_周期") == 0) return c->相对强弱指数_周期;
    else if (strcmp(field, "相对强弱指数_移动平均线周期") == 0) return c->相对强弱指数_移动平均线周期;
    else if (strcmp(field, "随机指标_RSV周期") == 0) return c->随机指标_RSV周期;
    else if (strcmp(field, "随机指标_K值平滑周期") == 0) return c->随机指标_K值平滑周期;
    else if (strcmp(field, "随机指标_D值平滑周期") == 0) return c->随机指标_D值平滑周期;
    else { return 0; }
    return 0;
}

double chan_config_get_double(void* config, const char* field) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "相对强弱指数_超买阈值") == 0) return c->相对强弱指数_超买阈值;
    else if (strcmp(field, "相对强弱指数_超卖阈值") == 0) return c->相对强弱指数_超卖阈值;
    else if (strcmp(field, "随机指标_超买阈值") == 0) return c->随机指标_超买阈值;
    else if (strcmp(field, "随机指标_超卖阈值") == 0) return c->随机指标_超卖阈值;
    else if (strcmp(field, "买卖点错过误差值") == 0) return c->买卖点错过误差值;
    else if (strcmp(field, "买卖点_背离率") == 0) return c->买卖点_背离率;
    else if (strcmp(field, "买卖点_T2_回调阈值") == 0) return c->买卖点_T2_回调阈值;
    else { return 0.0; }
    return 0.0;
}

const char* chan_config_get_string(void* config, const char* field) {
    缠论配置* c = (缠论配置*)config;
    if (0) {}
    else if (strcmp(field, "手动终止") == 0) return c->手动终止;
    else if (strcmp(field, "指标计算方式") == 0) return c->指标计算方式;
    else if (strcmp(field, "买卖点_指标模式") == 0) return c->买卖点_指标模式;
    else if (strcmp(field, "买卖点_计算方式") == 0) return c->买卖点_计算方式;
    else if (strcmp(field, "买卖点_中枢来源") == 0) return c->买卖点_中枢来源;
    else if (strcmp(field, "线段内部背驰_模式") == 0) return c->线段内部背驰_模式;
    else if (strcmp(field, "标识") == 0) return c->标识;
    else if (strcmp(field, "加载文件路径") == 0) return c->加载文件路径;
    else { return ""; }
    return "";
}

/* ================================================================
 * K-line creation
 * ================================================================ */

void* chan_kline_new(const char* id, int index, int period, time_t ts,
                     double open, double high, double low, double close, double vol) {
    return K线_新建(id, index, period, ts, open, high, low, close, vol);
}

void* chan_kline_create_plain(const char* id, time_t ts,
                              double open, double high, double low, double close,
                              double vol, int index, int period) {
    return K线_创建普K(id, ts, open, high, low, close, vol, index, period);
}

/* ================================================================
 * Observer lifecycle
 * ================================================================ */

void* chan_observer_new(const char* symbol, int period, void* config) {
    return 观察者_新建(symbol, period, (缠论配置*)config);
}

void chan_observer_feed_kline(void* obs, void* kline) {
    观察者_增加原始K线((观察者*)obs, (K线*)kline);
}

void* chan_observer_read_file(const char* path, void* config) {
    return 观察者_读取数据文件(path, (缠论配置*)config);
}

void chan_observer_release(void* obs) {
    if (obs) 解引用(obs);
}

/* ================================================================
 * Observer sequence lengths
 * ================================================================ */

#define SEQ_LEN_IMPL(name, seq) \
    size_t chan_observer_##name##_length(void* obs) { \
        return ((观察者*)obs)->seq.长度; \
    }

SEQ_LEN_IMPL(raw_klines,           普通K线序列)
SEQ_LEN_IMPL(chan_klines,          缠论K线序列)
SEQ_LEN_IMPL(base_chan_klines,     基础缠K序列)
SEQ_LEN_IMPL(fractals,             分型序列)
SEQ_LEN_IMPL(strokes,              笔序列)
SEQ_LEN_IMPL(stroke_hubs,          笔_中枢序列)
SEQ_LEN_IMPL(segments,             线段序列)
SEQ_LEN_IMPL(hubs,                 中枢序列)
SEQ_LEN_IMPL(ext_segments,         扩展线段序列)
SEQ_LEN_IMPL(ext_hubs,             扩展中枢序列)
SEQ_LEN_IMPL(ext_segments_seg,     扩展线段序列_线段)
SEQ_LEN_IMPL(ext_hubs_seg,         扩展中枢序列_线段)
SEQ_LEN_IMPL(seg_seg,              线段_线段序列)
SEQ_LEN_IMPL(seg_hubs,             线段_中枢序列)
SEQ_LEN_IMPL(ext_seg_extseg,       扩展线段序列_扩展线段)
SEQ_LEN_IMPL(ext_hubs_extseg,      扩展中枢序列_扩展线段)

#undef SEQ_LEN_IMPL

/* ================================================================
 * Observer sequence element access
 * ================================================================ */

#define SEQ_AT_IMPL(name, seq) \
    void* chan_observer_##name##_at(void* obs, size_t idx) { \
        return 动态数组_获取(&((观察者*)obs)->seq, idx); \
    }

SEQ_AT_IMPL(raw_klines,           普通K线序列)
SEQ_AT_IMPL(chan_klines,          缠论K线序列)
SEQ_AT_IMPL(base_chan_klines,     基础缠K序列)
SEQ_AT_IMPL(fractals,             分型序列)
SEQ_AT_IMPL(strokes,              笔序列)
SEQ_AT_IMPL(stroke_hubs,          笔_中枢序列)
SEQ_AT_IMPL(segments,             线段序列)
SEQ_AT_IMPL(hubs,                 中枢序列)
SEQ_AT_IMPL(ext_segments,         扩展线段序列)
SEQ_AT_IMPL(ext_hubs,             扩展中枢序列)
SEQ_AT_IMPL(ext_segments_seg,     扩展线段序列_线段)
SEQ_AT_IMPL(ext_hubs_seg,         扩展中枢序列_线段)
SEQ_AT_IMPL(seg_seg,              线段_线段序列)
SEQ_AT_IMPL(seg_hubs,             线段_中枢序列)
SEQ_AT_IMPL(ext_seg_extseg,       扩展线段序列_扩展线段)
SEQ_AT_IMPL(ext_hubs_extseg,      扩展中枢序列_扩展线段)

#undef SEQ_AT_IMPL

/* ================================================================
 * K-line field accessors
 * ================================================================ */

const char* chan_kline_id(void* k)        { return ((K线*)k)->标识; }
int         chan_kline_index(void* k)      { return ((K线*)k)->序号; }
int         chan_kline_period(void* k)     { return ((K线*)k)->周期; }
time_t      chan_kline_timestamp(void* k)  { return ((K线*)k)->时间戳; }
double      chan_kline_high(void* k)       { return ((K线*)k)->高; }
double      chan_kline_low(void* k)        { return ((K线*)k)->低; }
double      chan_kline_open(void* k)       { return ((K线*)k)->开盘价; }
double      chan_kline_close(void* k)      { return ((K线*)k)->收盘价; }
double      chan_kline_volume(void* k)     { return ((K线*)k)->成交量; }
int         chan_kline_direction(void* k)  { return (int)K线_方向((K线*)k); }

/* ================================================================
 * Chan K-line field accessors
 * ================================================================ */

int         chan_ckline_index(void* ck)          { return ((缠论K线*)ck)->序号; }
time_t      chan_ckline_timestamp(void* ck)       { return ((缠论K线*)ck)->时间戳; }
double      chan_ckline_high(void* ck)            { return ((缠论K线*)ck)->高; }
double      chan_ckline_low(void* ck)             { return ((缠论K线*)ck)->低; }
int         chan_ckline_direction(void* ck)       { return (int)((缠论K线*)ck)->方向; }
int         chan_ckline_fractal_type(void* ck)    { return (int)((缠论K线*)ck)->分型; }
double      chan_ckline_fractal_value(void* ck)   { return ((缠论K线*)ck)->分型特征值; }
int         chan_ckline_period(void* ck)          { return ((缠论K线*)ck)->周期; }
const char* chan_ckline_id(void* ck)              { return ((缠论K线*)ck)->标识; }
int         chan_ckline_orig_start(void* ck)      { return ((缠论K线*)ck)->原始起始序号; }
int         chan_ckline_orig_end(void* ck)        { return ((缠论K线*)ck)->原始结束序号; }
void*       chan_ckline_ref_kline(void* ck)       { return ((缠论K线*)ck)->标的K线; }

/* ================================================================
 * Fractal field accessors
 * ================================================================ */

void*   chan_fractal_left(void* f)           { return ((分型*)f)->左; }
void*   chan_fractal_mid(void* f)            { return ((分型*)f)->中; }
void*   chan_fractal_right(void* f)          { return ((分型*)f)->右; }
int     chan_fractal_structure(void* f)      { return (int)((分型*)f)->结构; }
time_t  chan_fractal_timestamp(void* f)      { return ((分型*)f)->时间戳; }
double  chan_fractal_feature_value(void* f)  { return ((分型*)f)->分型特征值; }

/* ================================================================
 * Dash line accessors
 * ================================================================ */

int         chan_dash_index(void* d)      { return ((虚线*)d)->序号; }
const char* chan_dash_id(void* d)         { return ((虚线*)d)->标识; }
int         chan_dash_level(void* d)      { return ((虚线*)d)->级别; }
void*       chan_dash_wen(void* d)        { return ((虚线*)d)->文; }
void*       chan_dash_wu(void* d)         { return ((虚线*)d)->武; }
int         chan_dash_direction(void* d)  { return (int)虚线_方向((虚线*)d); }
double      chan_dash_high(void* d)       { return 虚线_高((虚线*)d); }
double      chan_dash_low(void* d)        { return 虚线_低((虚线*)d); }
int         chan_dash_is_valid(void* d)   { return (int)((虚线*)d)->有效性; }
const char* chan_dash_mode(void* d)       { return ((虚线*)d)->模式; }

size_t chan_dash_base_seq_length(void* d)    { return ((虚线*)d)->基础序列.长度; }
void*  chan_dash_base_seq_at(void* d, size_t idx) { return 动态数组_获取(&((虚线*)d)->基础序列, idx); }

size_t chan_dash_feature_seq_length(void* d) { return ((虚线*)d)->特征序列.长度; }
void*  chan_dash_feature_seq_at(void* d, size_t idx) { return 动态数组_获取(&((虚线*)d)->特征序列, idx); }

size_t chan_dash_real_hub_seq_length(void* d)     { return ((虚线*)d)->实_中枢序列.长度; }
void*  chan_dash_real_hub_seq_at(void* d, size_t idx)  { return 动态数组_获取(&((虚线*)d)->实_中枢序列, idx); }
size_t chan_dash_virtual_hub_seq_length(void* d)  { return ((虚线*)d)->虚_中枢序列.长度; }
void*  chan_dash_virtual_hub_seq_at(void* d, size_t idx) { return 动态数组_获取(&((虚线*)d)->虚_中枢序列, idx); }
size_t chan_dash_combined_hub_seq_length(void* d) { return ((虚线*)d)->合_中枢序列.长度; }
void*  chan_dash_combined_hub_seq_at(void* d, size_t idx) { return 动态数组_获取(&((虚线*)d)->合_中枢序列, idx); }

/* ================================================================
 * Hub accessors
 * ================================================================ */

int         chan_hub_index(void* h)              { return ((中枢*)h)->序号; }
const char* chan_hub_id(void* h)                 { return ((中枢*)h)->标识; }
int         chan_hub_level(void* h)              { return ((中枢*)h)->级别; }
double      chan_hub_high(void* h)               { return 中枢_高((中枢*)h); }
double      chan_hub_low(void* h)                { return 中枢_低((中枢*)h); }
double      chan_hub_high_high(void* h)          { return 中枢_高高((中枢*)h); }
double      chan_hub_low_low(void* h)            { return 中枢_低低((中枢*)h); }
int         chan_hub_direction(void* h)          { return (int)中枢_方向((中枢*)h); }
int         chan_hub_is_complete(void* h)        { return (int)中枢_完整性((中枢*)h); }
const char* chan_hub_status(void* h)             { return 中枢_当前状态((中枢*)h); }
void*       chan_hub_third_buy_sell(void* h)     { return ((中枢*)h)->第三买卖线; }
void*       chan_hub_local_third_buy_sell(void* h) { return ((中枢*)h)->本级_第三买卖线; }
size_t      chan_hub_element_count(void* h)      { return ((中枢*)h)->元素.长度; }
void*       chan_hub_element_at(void* h, size_t idx) { return 动态数组_获取(&((中枢*)h)->元素, idx); }

/* ================================================================
 * Segment feature accessors
 * ================================================================ */

int    chan_segfeat_index(void* sf)           { return ((线段特征*)sf)->序号; }
const char* chan_segfeat_id(void* sf)         { return ((线段特征*)sf)->标识; }
int    chan_segfeat_direction(void* sf)       { return (int)线段特征_方向((线段特征*)sf); }
void*  chan_segfeat_wen(void* sf)             { return 线段特征_文((线段特征*)sf); }
void*  chan_segfeat_wu(void* sf)              { return 线段特征_武((线段特征*)sf); }
double chan_segfeat_high(void* sf)            { return 线段特征_高((线段特征*)sf); }
double chan_segfeat_low(void* sf)             { return 线段特征_低((线段特征*)sf); }
size_t chan_segfeat_element_count(void* sf)   { return ((线段特征*)sf)->元素.长度; }
void*  chan_segfeat_element_at(void* sf, size_t idx) { return 动态数组_获取(&((线段特征*)sf)->元素, idx); }

/* ================================================================
 * Gap accessors
 * ================================================================ */

double chan_gap_high(void* g) { return ((缺口*)g)->高; }
double chan_gap_low(void* g)  { return ((缺口*)g)->低; }

/* ================================================================
 * Reference counting
 * ================================================================ */

void chan_retain(void* obj)  { 引用(obj); }
void chan_release(void* obj) { 解引用(obj); }

/* ================================================================
 * Utility
 * ================================================================ */

time_t chan_str2timestamp(const char* ts_str) {
    return 转化为时间戳(ts_str);
}

/* ================================================================
 * Bulk K-line data export
 * ================================================================ */

double* chan_observer_raw_klines_export(void* obs, size_t* out_count) {
    观察者* o = (观察者*)obs;
    size_t n = o->普通K线序列.长度;
    *out_count = n;
    if (n == 0) return NULL;
    double* buf = malloc(n * 6 * sizeof(double));
    if (!buf) return NULL;
    for (size_t i = 0; i < n; i++) {
        K线* k = 动态数组_获取(&o->普通K线序列, i);
        if (k) {
            buf[i * 6 + 0] = (double)k->时间戳;
            buf[i * 6 + 1] = k->开盘价;
            buf[i * 6 + 2] = k->高;
            buf[i * 6 + 3] = k->低;
            buf[i * 6 + 4] = k->收盘价;
            buf[i * 6 + 5] = k->成交量;
        } else {
            for (int j = 0; j < 6; j++) buf[i * 6 + j] = 0.0;
        }
    }
    return buf;
}

/* ================================================================
 * Direction / structure helpers
 * ================================================================ */

int chan_direction_is_up(int dir)            { return (相对方向)dir == 相对方向_向上; }
int chan_direction_is_down(int dir)          { return (相对方向)dir == 相对方向_向下; }
int chan_direction_is_gap(int dir)           { return ((相对方向)dir == 相对方向_向上缺口 || (相对方向)dir == 相对方向_向下缺口); }
int chan_direction_is_containment(int dir)   { return ((相对方向)dir == 相对方向_顺 || (相对方向)dir == 相对方向_逆); }
int chan_direction_is_junction(int dir)      { return ((相对方向)dir == 相对方向_衔接向上 || (相对方向)dir == 相对方向_衔接向下); }
int chan_direction_flip(int dir)             { return (int)相对方向_翻转((相对方向)dir); }

/* ================================================================
 * 虚线 (Dash) analysis methods
 * ================================================================ */

int chan_dash_before(void* d, void* other) {
    return (int)虚线_之前是((虚线*)d, (虚线*)other);
}

int chan_dash_after(void* d, void* other) {
    return (int)虚线_之后是((虚线*)d, (虚线*)other);
}

/* ================================================================
 * 线段 (Segment) analysis methods
 * ================================================================ */

const char* chan_segment_sixiang(void* seg) {
    return 线段_四象((虚线*)seg);
}

int chan_segment_feature_fractal_end(void* seg) {
    return (int)线段_特征分型终结((虚线*)seg);
}

void chan_segment_feature_seq_state(void* seg, int* left, int* mid, int* right) {
    bool l, m, r;
    线段_特征序列状态((虚线*)seg, &l, &m, &r);
    if (left)  *left  = (int)l;
    if (mid)   *mid   = (int)m;
    if (right) *right = (int)r;
}

void* chan_segment_get_gap(void* seg) {
    return (void*)线段_获取缺口((虚线*)seg);
}

void* chan_segment_find_penetration(void* seg) {
    return (void*)线段_查找贯穿伤((虚线*)seg);
}

/* ================================================================
 * 笔 (Stroke) analysis — observer-level lookup
 * ================================================================ */

void* chan_observer_find_stroke_by_wen(void* obs, void* fractal) {
    观察者* o = (观察者*)obs;
    return (void*)笔_以文会友(&o->笔序列, (分型*)fractal);
}

void* chan_observer_find_stroke_by_wu(void* obs, void* fractal) {
    观察者* o = (观察者*)obs;
    return (void*)笔_以武会友(&o->笔序列, (分型*)fractal);
}

void* chan_observer_find_stroke_by_ckline(void* obs, void* ck, int offset) {
    观察者* o = (观察者*)obs;
    return (void*)笔_根据缠K找笔(&o->笔序列, (缠论K线*)ck, offset);
}

int chan_stroke_relative_relation(void* stroke, void* config) {
    return (int)笔_相对关系((虚线*)stroke, (缠论配置*)config);
}

/* ================================================================
 * 背驰分析 (Divergence analysis)
 * ================================================================ */

int chan_divergence_slope(void* enter, void* leave) {
    return (int)背驰分析_斜率背驰((虚线*)enter, (虚线*)leave);
}

int chan_divergence_measure(void* enter, void* leave) {
    return (int)背驰分析_测度背驰((虚线*)enter, (虚线*)leave);
}

int chan_divergence_macd(void* enter, void* leave, void** klines, size_t n, const char* method) {
    return (int)背驰分析_MACD背驰((虚线*)enter, (虚线*)leave, (K线**)klines, n, method);
}

int chan_divergence_full(void* enter, void* leave, void** klines, size_t n) {
    return (int)背驰分析_全量背驰((虚线*)enter, (虚线*)leave, (K线**)klines, n);
}

int chan_divergence_any(void* enter, void* leave, void** klines, size_t n) {
    return (int)背驰分析_任意背驰((虚线*)enter, (虚线*)leave, (K线**)klines, n);
}

int chan_divergence_config(void* enter, void* leave, void** klines, size_t n, void* config) {
    return (int)背驰分析_配置背驰((虚线*)enter, (虚线*)leave, (K线**)klines, n, (缠论配置*)config);
}

int chan_divergence_optional(void* enter, void* leave, void** klines, size_t n) {
    return (int)背驰分析_任选背驰((虚线*)enter, (虚线*)leave, (K线**)klines, n);
}

int chan_divergence_mode(void* enter, void* leave, void** klines, size_t n, void* config, const char* mode) {
    return (int)背驰分析_背驰模式((虚线*)enter, (虚线*)leave, (K线**)klines, n, (缠论配置*)config, mode);
}

/* ================================================================
 * Helper: extract K-line pointer array from observer for a dash range
 * ================================================================ */

int chan_observer_get_kline_array(void* obs, void* dash,
                                   void*** out, size_t* out_len) {
    观察者* o = (观察者*)obs;
    K线** karr = NULL;
    size_t n = 0;
    虚线_获取普K序列((虚线*)dash, o, &karr, &n);
    if (!karr || n == 0) {
        *out = NULL;
        *out_len = 0;
        return 0;
    }
    /* Copy to a malloc'd array; the original is stack-allocated by the C function */
    void** copy = malloc(n * sizeof(void*));
    if (!copy) { *out = NULL; *out_len = 0; return 0; }
    memcpy(copy, karr, n * sizeof(void*));
    *out = copy;
    *out_len = n;
    return 1;
}

void chan_free_kline_array(void** arr) {
    free(arr);
}

/* ================================================================
 * 虚线 (Dash) field accessors (struct fields not yet exposed)
 * ================================================================ */

void* chan_dash_confirm_ckline(void* d) {
    return (void*)((虚线*)d)->确认K线;
}

void* chan_dash_prev_gap(void* d) {
    return (void*)((虚线*)d)->前一缺口;
}

void* chan_dash_prev_end(void* d) {
    return (void*)((虚线*)d)->前一结束位置;
}

int chan_dash_short_circuit(void* d) {
    return (int)((虚线*)d)->短路修正;
}

/* ================================================================
 * 虚线 creation
 * ================================================================ */

void* chan_dash_create_stroke(void* wen, void* wu, int valid) {
    return (void*)虚线_创建笔((分型*)wen, (分型*)wu, (bool)valid);
}

/* ================================================================
 * 虚线 K-line extraction (returns malloc'd array, caller frees)
 * ================================================================ */

int chan_dash_get_raw_klines(void* dash, void* obs, void*** out, size_t* out_len) {
    K线** karr = NULL;
    size_t n = 0;
    虚线_获取普K序列((虚线*)dash, (观察者*)obs, &karr, &n);
    if (!karr || n == 0) { *out = NULL; *out_len = 0; return 0; }
    void** copy = malloc(n * sizeof(void*));
    if (!copy) { *out = NULL; *out_len = 0; return 0; }
    memcpy(copy, karr, n * sizeof(void*));
    *out = copy;
    *out_len = n;
    return 1;
}

int chan_dash_get_chan_klines(void* dash, void* obs, void*** out, size_t* out_len) {
    缠论K线** karr = NULL;
    size_t n = 0;
    虚线_获取缠K序列((虚线*)dash, (观察者*)obs, &karr, &n);
    if (!karr || n == 0) { *out = NULL; *out_len = 0; return 0; }
    void** copy = malloc(n * sizeof(void*));
    if (!copy) { *out = NULL; *out_len = 0; return 0; }
    memcpy(copy, karr, n * sizeof(void*));
    *out = copy;
    *out_len = n;
    return 1;
}

/* ================================================================
 * 笔 utility functions — operate on observer's internal sequences
 * ================================================================ */

int chan_observer_stroke_chan_count(void* obs, void* config) {
    观察者* o = (观察者*)obs;
    return 笔_获取缠K数量(&o->缠论K线序列, &o->笔序列, (缠论配置*)config);
}

void* chan_observer_stroke_second_high(void* obs, int same_end) {
    观察者* o = (观察者*)obs;
    return (void*)笔_次高(&o->缠论K线序列, (bool)same_end);
}

void* chan_observer_stroke_second_low(void* obs, int same_end) {
    观察者* o = (观察者*)obs;
    return (void*)笔_次低(&o->缠论K线序列, (bool)same_end);
}

void* chan_observer_stroke_actual_high(void* obs, int same_end) {
    观察者* o = (观察者*)obs;
    return (void*)笔_实际高点(&o->缠论K线序列, (bool)same_end);
}

void* chan_observer_stroke_actual_low(void* obs, int same_end) {
    观察者* o = (观察者*)obs;
    return (void*)笔_实际低点(&o->缠论K线序列, (bool)same_end);
}

/* ================================================================
 * Pipeline analysis — re-run analysis on observer's sequences
 * ================================================================ */

void chan_observer_analyze_strokes(void* obs, void* config) {
    观察者* o = (观察者*)obs;
    笔_分析(NULL, &o->分型序列, &o->笔序列,
             &o->缠论K线序列, &o->普通K线序列, (缠论配置*)config);
}

void chan_observer_analyze_segments(void* obs, void* config) {
    观察者* o = (观察者*)obs;
    线段_分析(&o->笔序列, &o->线段序列, (缠论配置*)config);
}

void chan_observer_analyze_ext_segments(void* obs, void* config) {
    观察者* o = (观察者*)obs;
    线段_扩展分析(&o->笔序列, &o->扩展线段序列, (缠论配置*)config);
}

void chan_observer_analyze_stroke_hubs(void* obs, int skip_first, const char* id) {
    观察者* o = (观察者*)obs;
    中枢_分析(&o->笔序列, &o->笔_中枢序列, (bool)skip_first, id);
}

void chan_observer_analyze_segment_hubs(void* obs, int skip_first, const char* id) {
    观察者* o = (观察者*)obs;
    中枢_分析(&o->线段序列, &o->中枢序列, (bool)skip_first, id);
}
