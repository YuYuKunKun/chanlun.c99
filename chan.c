/*
 * MIT License
 *
 * Copyright (c) 2026 YuYuKunKun
 *
 * chan.c — 缠论 C99 移植实现
 */

#define _XOPEN_SOURCE 700
#include "chan.h"

/* 全局回收站 — 在观察者_增加原始K线中设置，供所有分析函数使用 */
static 动态数组* chan_回收站 = NULL;

/* 将弹出/替换后孤立的对象放入回收站 */
static void 入回收站(void* 对象) {
    if (chan_回收站 && 对象) 动态数组_追加(chan_回收站, 对象);
}

/* ================================================================
 * 内存管理
 * ================================================================ */

bool CHAN_DEBUG_MEM = false;

static size_t 分配计数[CHAN_TYPE_COUNT] = {0};
static size_t 释放计数[CHAN_TYPE_COUNT] = {0};

static const char* 类型名称[] = {
    "K线", "缠论K线", "分型", "缺口", "虚线", "中枢",
    "线段特征", "特征分型", "平滑异同移动平均线", "相对强弱指数",
    "随机指标", "缠论配置", "观察者"
};

void 打印内存摘要(void) {
    fprintf(stderr, "\n=== 内存摘要 ===\n");
    fprintf(stderr, "%-20s %8s %8s %8s\n", "类型", "分配", "释放", "泄漏");
    size_t total_leak = 0;
    for (int i = 0; i < CHAN_TYPE_COUNT; i++) {
        if (分配计数[i] > 0 || 释放计数[i] > 0) {
            size_t leak = 分配计数[i] - 释放计数[i];
            total_leak += leak;
            fprintf(stderr, "%-20s %8zu %8zu %8zu%s\n",
                    类型名称[i], 分配计数[i], 释放计数[i], leak,
                    leak > 0 ? " <---" : "");
        }
    }
    fprintf(stderr, "%-20s %8s %8s %8zu\n", "合计", "", "", total_leak);
    fflush(stderr);
}

static size_t 笔创建计数 = 0;
static size_t 笔加入序列计数 = 0;
static size_t 笔释放于文官路径 = 0;
static size_t 笔释放于底部 = 0;
static size_t 笔释放于失败路径 = 0;
static size_t 笔释放于弹出旧笔 = 0;
static size_t 线段创建于step4 = 0;
static size_t 线段创建于loop = 0;
static size_t 线段创建于扩展 = 0;
static size_t 线段释放计数 = 0;

void 打印笔线段摘要(void) {
    fprintf(stderr, "\n=== 笔/线段 详细摘要 ===\n");
    fprintf(stderr, "笔创建: %zu\n", 笔创建计数);
    fprintf(stderr, "笔加入序列: %zu\n", 笔加入序列计数);
    fprintf(stderr, "笔释放-文官路径: %zu\n", 笔释放于文官路径);
    fprintf(stderr, "笔释放-底部: %zu\n", 笔释放于底部);
    fprintf(stderr, "笔释放-失败路径: %zu\n", 笔释放于失败路径);
    fprintf(stderr, "笔释放-弹出旧笔: %zu\n", 笔释放于弹出旧笔);
    fprintf(stderr, "笔净泄漏: %zu\n",
            笔创建计数 - (笔释放于文官路径 + 笔释放于底部 + 笔释放于失败路径 + 笔释放于弹出旧笔 + 笔加入序列计数));
    fprintf(stderr, "线段创建-step4: %zu\n", 线段创建于step4);
    fprintf(stderr, "线段创建-loop: %zu\n", 线段创建于loop);
    fprintf(stderr, "线段创建-扩展: %zu\n", 线段创建于扩展);
    fprintf(stderr, "线段释放: %zu\n", 线段释放计数);
    fprintf(stderr, "线段净泄漏: %zu\n",
            线段创建于step4 + 线段创建于loop + 线段创建于扩展 - 线段释放计数);
    fflush(stderr);
}

void* 分配(size_t 大小, 对象类型 类型) {
    void* ptr = calloc(1, 大小);
    if (!ptr) {
        fprintf(stderr, "分配: 内存不足 (请求 %zu 字节)\n", 大小);
        abort();
    }
    引用计数(ptr) = 1;
    对象类型_取(ptr) = 类型;
    分配计数[类型]++;
    return ptr;
}

void 引用(void* 对象) {
    if (!对象) return;
    引用计数(对象)++;
}

void 解引用(void* 对象) {
    if (!对象) return;
    if (--引用计数(对象) <= 0) {
        if (CHAN_DEBUG_MEM) {
            fprintf(stderr, "解引用: 释放对象 %p\n", 对象);
        }
        对象类型 t = 对象类型_取(对象);
        对象销毁(对象);
        free(对象);
        释放计数[t]++;
    }
}

void 对象销毁(void* 对象) {
    对象类型 t = 对象类型_取(对象);
    switch (t) {
        case CHAN_TYPE_K线:               释放K线((K线*)对象); break;
        case CHAN_TYPE_缠论K线:          释放缠论K线((缠论K线*)对象); break;
        case CHAN_TYPE_分型:             释放分型((分型*)对象); break;
        case CHAN_TYPE_缺口:             释放缺口((缺口*)对象); break;
        case CHAN_TYPE_虚线:             释放虚线((虚线*)对象); break;
        case CHAN_TYPE_中枢:             释放中枢((中枢*)对象); break;
        case CHAN_TYPE_线段特征:         释放线段特征((线段特征*)对象); break;
        case CHAN_TYPE_特征分型:         释放特征分型((特征分型*)对象); break;
        case CHAN_TYPE_平滑异同移动平均线: 释放平滑异同移动平均线((平滑异同移动平均线*)对象); break;
        case CHAN_TYPE_相对强弱指数:      释放相对强弱指数((相对强弱指数*)对象); break;
        case CHAN_TYPE_随机指标:         释放随机指标((随机指标*)对象); break;
        case CHAN_TYPE_缠论配置:         释放缠论配置((缠论配置*)对象); break;
        case CHAN_TYPE_观察者:           释放观察者((观察者*)对象); break;
        default: break;
    }
}

/* ================================================================
 * 动态数组
 * ================================================================ */

void 动态数组_初始化(动态数组* arr, size_t 初始容量) {
    if (初始容量 == 0) 初始容量 = 4;
    arr->数据 = malloc(初始容量 * sizeof(void*));
    if (!arr->数据) { perror("动态数组_初始化"); abort(); }
    arr->长度 = 0;
    arr->容量 = 初始容量;
}

void 动态数组_追加(动态数组* arr, void* 元素) {
    if (arr->长度 >= arr->容量) {
        size_t 新容量 = arr->容量 * 2;
        void** 新数据 = realloc(arr->数据, 新容量 * sizeof(void*));
        if (!新数据) { perror("动态数组_追加"); abort(); }
        arr->数据 = 新数据;
        arr->容量 = 新容量;
    }
    arr->数据[arr->长度++] = 元素;
}

void* 动态数组_弹出(动态数组* arr) {
    if (arr->长度 == 0) return NULL;
    return arr->数据[--arr->长度];
}

void* 动态数组_获取(动态数组* arr, size_t 索引) {
    if (索引 >= arr->长度) return NULL;
    return arr->数据[索引];
}

void 动态数组_设置(动态数组* arr, size_t 索引, void* 元素) {
    if (索引 < arr->长度) arr->数据[索引] = 元素;
}

size_t 动态数组_查找(动态数组* arr, void* 元素) {
    for (size_t i = 0; i < arr->长度; i++) {
        if (arr->数据[i] == 元素) return i;
    }
    return (size_t)-1;
}

void 动态数组_释放(动态数组* arr, bool 释放元素) {
    if (释放元素) {
        for (size_t i = 0; i < arr->长度; i++) {
            解引用(arr->数据[i]);
        }
    }
    free(arr->数据);
    arr->数据 = NULL;
    arr->长度 = 0;
    arr->容量 = 0;
}

void 动态数组_全部解引用(动态数组* arr) {
    动态数组_释放(arr, true);
}

/* 类型化列表包装 */
void 虚线指针列表_追加(动态数组* 列表, 虚线* 元素) {
    动态数组_追加(列表, 元素);
    引用(元素);
}

void 虚线指针列表_移除末位(动态数组* 列表) {
    虚线* 尾 = 动态数组_弹出(列表);
    解引用(尾);
}

void 虚线指针列表_全部解引用(动态数组* 列表) {
    动态数组_释放(列表, true);
}

void 线段特征指针列表_全部解引用(动态数组* 列表) {
    动态数组_释放(列表, true);
}

void 中枢指针列表_全部解引用(动态数组* 列表) {
    动态数组_释放(列表, true);
}

/* ================================================================
 * 工具函数
 * ================================================================ */

time_t 转化为时间戳_数字(time_t ts) {
    return ts;
}

time_t 转化为时间戳(const char* ts_str) {
    struct tm tm_buf = {0};
    if (strptime(ts_str, "%Y-%m-%d %H:%M:%S", &tm_buf)) {
        return mktime(&tm_buf);
    }
    return 0;
}

相对方向 相对方向_翻转(相对方向 dir) {
    switch (dir) {
        case 相对方向_向上:     return 相对方向_向下;
        case 相对方向_向下:     return 相对方向_向上;
        case 相对方向_向下缺口: return 相对方向_向上缺口;
        case 相对方向_向上缺口: return 相对方向_向下缺口;
        case 相对方向_衔接向上: return 相对方向_衔接向下;
        case 相对方向_衔接向下: return 相对方向_衔接向上;
        case 相对方向_顺:       return 相对方向_逆;
        case 相对方向_逆:       return 相对方向_顺;
        default:                return dir;
    }
}

bool 相对方向_是否向上(相对方向 dir) {
    return dir == 相对方向_向上 || dir == 相对方向_向上缺口 || dir == 相对方向_衔接向上;
}

bool 相对方向_是否向下(相对方向 dir) {
    return dir == 相对方向_向下 || dir == 相对方向_向下缺口 || dir == 相对方向_衔接向下;
}

bool 相对方向_是否包含(相对方向 dir) {
    return dir == 相对方向_顺 || dir == 相对方向_逆 || dir == 相对方向_同;
}

bool 相对方向_是否缺口(相对方向 dir) {
    return dir == 相对方向_向下缺口 || dir == 相对方向_向上缺口;
}

bool 相对方向_是否衔接(相对方向 dir) {
    return dir == 相对方向_衔接向下 || dir == 相对方向_衔接向上;
}

相对方向 相对方向_分析(double 前高, double 前低, double 后高, double 后低) {
    if (前高 == 后高 && 前低 == 后低) return 相对方向_同;
    if (前高 > 后高 && 前低 > 后低) {
        if (前低 == 后高) return 相对方向_衔接向下;
        if (前低 > 后高)  return 相对方向_向下缺口;
        return 相对方向_向下;
    }
    if (前高 < 后高 && 前低 < 后低) {
        if (前高 == 后低) return 相对方向_衔接向上;
        if (前高 < 后低)  return 相对方向_向上缺口;
        return 相对方向_向上;
    }
    if (前高 >= 后高 && 前低 <= 后低) return 相对方向_顺;
    if (前高 <= 后高 && 前低 >= 后低) return 相对方向_逆;
    fprintf(stderr, "相对方向_分析: 无法识别 (%f,%f) vs (%f,%f)\n", 前高, 前低, 后高, 后低);
    return 相对方向_同;
}

分型结构 分型结构_分析(缺口* 左, 缺口* 中, 缺口* 右,
                      bool 可以逆序包含, bool 忽视顺序包含) {
    if (!左 || !中 || !右) return 分型结构_散;

    相对方向 左中关系 = 相对方向_分析(左->高, 左->低, 中->高, 中->低);
    相对方向 中右关系 = 相对方向_分析(中->高, 中->低, 右->高, 右->低);

    switch (左中关系) {
        case 相对方向_顺:
            if (忽视顺序包含) break;
            fprintf(stderr, "分型结构_分析: 顺序包含 左中\n");
            return 分型结构_散;
        default: break;
    }
    switch (中右关系) {
        case 相对方向_顺:
            if (忽视顺序包含) break;
            fprintf(stderr, "分型结构_分析: 顺序包含 中右\n");
            return 分型结构_散;
        default: break;
    }

    bool 左中向上 = 相对方向_是否向上(左中关系) && !相对方向_是否包含(左中关系);
    bool 左中向下 = 相对方向_是否向下(左中关系) && !相对方向_是否包含(左中关系);
    bool 中右向上 = 相对方向_是否向上(中右关系) && !相对方向_是否包含(中右关系);
    bool 中右向下 = 相对方向_是否向下(中右关系) && !相对方向_是否包含(中右关系);

    if (左中向上 && 中右向上) return 分型结构_上;
    if (左中向上 && 中右向下) return 分型结构_顶;
    if (左中向上 && 中右关系 == 相对方向_逆 && 可以逆序包含) return 分型结构_上;

    if (左中向下 && 中右向上) return 分型结构_底;
    if (左中向下 && 中右向下) return 分型结构_下;
    if (左中向下 && 中右关系 == 相对方向_逆 && 可以逆序包含) return 分型结构_下;

    if (左中关系 == 相对方向_逆 && 中右向上 && 可以逆序包含) return 分型结构_底;
    if (左中关系 == 相对方向_逆 && 中右向下 && 可以逆序包含) return 分型结构_顶;
    if (左中关系 == 相对方向_逆 && 中右关系 == 相对方向_逆 && 可以逆序包含) return 分型结构_散;

    fprintf(stderr, "分型结构_分析: 未匹配 可以逆序=%d 左中=%d 中右=%d\n",
            可以逆序包含, 左中关系, 中右关系);
    return 分型结构_散;
}

/* ================================================================
 * 缠论配置
 * ================================================================ */

缠论配置* 缠论配置_新建(void) {
    缠论配置* c = 分配(sizeof(缠论配置), CHAN_TYPE_缠论配置);
    strcpy(c->标识, "bar");
    c->缠K合并替换 = false;
    c->笔内元素数量 = 5;
    c->笔内相同终点取舍 = false;
    c->笔内起始分型包含整笔 = false;
    c->笔内起始分型包含整笔_包括右 = false;
    c->笔内原始K线包含整笔 = false;
    c->笔次级成笔 = false;
    c->笔弱化 = false;
    c->笔弱化_原始数量 = 3;
    c->线段_非缺口下穿刺 = false;
    c->线段_特征序列忽视老阴老阳 = false;
    c->线段_缺口后紧急修正 = true;
    c->线段_修正 = false;
    c->线段内部中枢图显 = true;
    c->扩展线段_当下分析 = false;
    c->分析笔 = true;
    c->分析线段 = true;
    c->分析扩展线段 = true;
    c->分析笔中枢 = true;
    c->分析线段中枢 = true;
    c->手动终止[0] = '\0';
    c->计算指标 = true;
    strcpy(c->指标计算方式, "收");
    c->平滑异同移动平均线_快线周期 = 13;
    c->平滑异同移动平均线_慢线周期 = 31;
    c->平滑异同移动平均线_信号周期 = 11;
    c->相对强弱指数_周期 = 13;
    c->相对强弱指数_移动平均线周期 = 13;
    c->相对强弱指数_超买阈值 = 75.0;
    c->相对强弱指数_超卖阈值 = 25.0;
    c->随机指标_RSV周期 = 13;
    c->随机指标_K值平滑周期 = 5;
    c->随机指标_D值平滑周期 = 5;
    c->随机指标_超买阈值 = 80.0;
    c->随机指标_超卖阈值 = 20.0;
    c->图表展示 = true;
    c->推送K线 = true;
    c->推送笔 = true;
    c->推送线段 = true;
    c->推送中枢 = true;
    c->图表展示_笔 = true;
    c->图表展示_线段 = true;
    c->图表展示_扩展线段 = true;
    c->图表展示_扩展线段_线段 = true;
    c->图表展示_线段_线段 = true;
    c->图表展示_中枢_笔 = true;
    c->图表展示_中枢_线段 = true;
    c->图表展示_中枢_扩展线段 = true;
    c->图表展示_中枢_扩展线段_线段 = true;
    c->图表展示_中枢_线段_线段 = true;
    c->图表展示_中枢_线段内部 = true;
    c->买卖点偏移 = 1;
    c->买卖点激进识别 = false;
    c->买卖点与MACD柱强相关 = false;
    c->买卖点错过误差值 = 0.01;
    strcpy(c->买卖点_指标模式, "配置");
    c->买卖点_指标匹配_MACD = true;
    c->买卖点_指标匹配_KDJ = true;
    c->买卖点_指标匹配_RSI = true;
    c->买卖点_背离率 = INFINITY;
    c->买卖点_T2_回调阈值 = 1.0;
    c->买卖点_T2S_最大层级 = 3;
    c->买卖点_峰值条件 = false;
    strcpy(c->买卖点_计算方式, "峰");
    c->买卖点_计算线段BSP1 = true;
    c->买卖点_处理BSP2 = true;
    c->买卖点_计算线段BSP3 = true;
    c->买卖点_依赖T1 = true;
    strcpy(c->买卖点_中枢来源, "合");
    c->买卖点_调试输出 = false;
    c->线段内部背驰_MACD = true;
    c->线段内部背驰_斜率 = true;
    c->线段内部背驰_测度 = true;
    strcpy(c->线段内部背驰_模式, "相对");
    strcpy(c->加载文件路径, "./templates/last.nb");
    return c;
}

缠论配置* 缠论配置_不推送(void) {
    缠论配置* c = 缠论配置_新建();
    c->线段内部中枢图显 = false;
    c->图表展示 = false;
    c->推送K线 = false;
    c->推送笔 = false;
    c->推送线段 = false;
    c->推送中枢 = false;
    c->图表展示_笔 = false;
    c->图表展示_线段 = false;
    c->图表展示_扩展线段 = false;
    c->图表展示_扩展线段_线段 = false;
    c->图表展示_线段_线段 = false;
    c->图表展示_中枢_笔 = false;
    c->图表展示_中枢_线段 = false;
    c->图表展示_中枢_扩展线段 = false;
    c->图表展示_中枢_扩展线段_线段 = false;
    c->图表展示_中枢_线段_线段 = false;
    c->图表展示_中枢_线段内部 = false;
    return c;
}

void 释放缠论配置(缠论配置* obj) {
    (void)obj;
}

/* ================================================================
 * 缺口
 * ================================================================ */

缺口* 缺口_新建(double 高, double 低) {
    assert(高 > 低);
    缺口* q = 分配(sizeof(缺口), CHAN_TYPE_缺口);
    q->高 = 高;
    q->低 = 低;
    return q;
}

void 释放缺口(缺口* obj) {
    (void)obj;
}

/* ================================================================
 * 指标取值
 * ================================================================ */

double 指标_K线取值(K线* k线, const char* 计算方式) {
    if (strcmp(计算方式, "开") == 0) return k线->开盘价;
    if (strcmp(计算方式, "高") == 0) return k线->高;
    if (strcmp(计算方式, "低") == 0) return k线->低;
    if (strcmp(计算方式, "收") == 0) return k线->收盘价;
    if (strcmp(计算方式, "高低均值") == 0) return (k线->高 + k线->低) / 2.0;
    if (strcmp(计算方式, "高低收均值") == 0) return (k线->高 + k线->低 + k线->收盘价) / 3.0;
    if (strcmp(计算方式, "开高低收均值") == 0) return (k线->高 + k线->低 + k线->开盘价 + k线->收盘价) / 4.0;
    return k线->收盘价;
}

/* ================================================================
 * 平滑异同移动平均线 (MACD)
 * ================================================================ */

static double macd_平滑系数(int 周期) {
    return 2.0 / (周期 + 1.0);
}

平滑异同移动平均线* 平滑异同移动平均线_首次计算(double 收盘价, time_t 时间,
                        int 快线周期, int 慢线周期, int 信号周期) {
    平滑异同移动平均线* m = 分配(sizeof(平滑异同移动平均线), CHAN_TYPE_平滑异同移动平均线);
    m->时间戳 = 时间;
    m->收盘价 = 收盘价;
    m->快线周期 = 快线周期;
    m->慢线周期 = 慢线周期;
    m->信号周期 = 信号周期;
    m->快线EMA = 收盘价;
    m->慢线EMA = 收盘价;
    m->DIF = 0;
    m->DEA_EMA = 0;
    m->DEA = 0;
    m->MACD柱 = 0;
    m->有快线EMA = m->有慢线EMA = true;
    m->有DIF = m->有DEA = m->有MACD柱 = m->有DEA_EMA = true;
    return m;
}

平滑异同移动平均线* 平滑异同移动平均线_首次计算_K线(K线* k线, const char* 计算方式,
                              int 快线, int 慢线, int 信号) {
    return 平滑异同移动平均线_首次计算(指标_K线取值(k线, 计算方式), k线->时间戳, 快线, 慢线, 信号);
}

平滑异同移动平均线* 平滑异同移动平均线_增量计算(平滑异同移动平均线* 前, double 收盘价, time_t 时间) {
    平滑异同移动平均线* m = 分配(sizeof(平滑异同移动平均线), CHAN_TYPE_平滑异同移动平均线);
    m->时间戳 = 时间;
    m->收盘价 = 收盘价;
    m->快线周期 = 前->快线周期;
    m->慢线周期 = 前->慢线周期;
    m->信号周期 = 前->信号周期;

    double a快 = macd_平滑系数(前->快线周期);
    double a慢 = macd_平滑系数(前->慢线周期);
    double a信 = macd_平滑系数(前->信号周期);

    m->快线EMA = 收盘价 * a快 + 前->快线EMA * ((前->快线周期 - 1.0) / (前->快线周期 + 1.0));
    m->慢线EMA = 收盘价 * a慢 + 前->慢线EMA * ((前->慢线周期 - 1.0) / (前->慢线周期 + 1.0));
    m->DIF = m->快线EMA - m->慢线EMA;
    m->DEA_EMA = m->DIF * a信 + 前->DEA_EMA * ((前->信号周期 - 1.0) / (前->信号周期 + 1.0));
    m->DEA = m->DEA_EMA;
    m->MACD柱 = m->DIF - m->DEA_EMA;

    m->有快线EMA = m->有慢线EMA = true;
    m->有DIF = m->有DEA = m->有MACD柱 = m->有DEA_EMA = true;
    return m;
}

平滑异同移动平均线* 平滑异同移动平均线_增量计算_K线(平滑异同移动平均线* 前, K线* k线, const char* 计算方式) {
    return 平滑异同移动平均线_增量计算(前, 指标_K线取值(k线, 计算方式), k线->时间戳);
}

void 释放平滑异同移动平均线(平滑异同移动平均线* obj) {
    (void)obj;
}

/* ================================================================
 * 相对强弱指数 (RSI)
 * ================================================================ */

相对强弱指数* 相对强弱指数_首次计算(double 收盘价, time_t 时间, int 周期,
                      double 超买, double 超卖, int sma周期, bool 有sma) {
    相对强弱指数* r = 分配(sizeof(相对强弱指数), CHAN_TYPE_相对强弱指数);
    r->时间戳 = 时间;
    r->收盘价 = 收盘价;
    r->周期 = 周期;
    r->超买阈值 = 超买;
    r->超卖阈值 = 超卖;
    r->有RSI_SMA周期 = 有sma;
    r->RSI_SMA周期 = sma周期;
    r->RSI = 0;
    r->平均上涨 = 0;
    r->平均下跌 = 0;
    r->上涨幅度 = 0;
    r->下跌幅度 = 0;
    r->平滑系数 = 1.0 / 周期;
    r->有RSI = false;
    r->有平均上涨 = false;
    r->有平均下跌 = false;
    r->RSI_SMA = 0;
    r->有RSI_SMA = false;
    r->RSI历史队列 = NULL;
    r->RSI历史队列_长度 = 0;
    return r;
}

相对强弱指数* 相对强弱指数_首次计算_K线(K线* k线, const char* 计算方式,
                          int 周期, double 超买, double 超卖, int sma) {
    return 相对强弱指数_首次计算(指标_K线取值(k线, 计算方式), k线->时间戳, 周期, 超买, 超卖, sma, sma > 0);
}

相对强弱指数* 相对强弱指数_增量计算(相对强弱指数* 前, double 收盘价, time_t 时间) {
    相对强弱指数* r = 分配(sizeof(相对强弱指数), CHAN_TYPE_相对强弱指数);
    r->时间戳 = 时间;
    r->收盘价 = 收盘价;
    r->周期 = 前->周期;
    r->超买阈值 = 前->超买阈值;
    r->超卖阈值 = 前->超卖阈值;
    r->有RSI_SMA周期 = 前->有RSI_SMA周期;
    r->RSI_SMA周期 = 前->RSI_SMA周期;
    r->平滑系数 = 1.0 / 前->周期;

    double 变化 = 收盘价 - 前->收盘价;
    r->上涨幅度 = fmax(变化, 0);
    r->下跌幅度 = fmax(-变化, 0);

    if (!前->有平均上涨 || !前->有平均下跌) {
        r->平均上涨 = r->上涨幅度;
        r->平均下跌 = r->下跌幅度;
    } else {
        r->平均上涨 = 前->平均上涨 * (1.0 - r->平滑系数) + r->上涨幅度 * r->平滑系数;
        r->平均下跌 = 前->平均下跌 * (1.0 - r->平滑系数) + r->下跌幅度 * r->平滑系数;
    }
    r->有平均上涨 = r->有平均下跌 = true;

    if (r->平均下跌 == 0) {
        r->RSI = (r->平均上涨 > 0) ? 100.0 : 50.0;
    } else {
        double RS = r->平均上涨 / r->平均下跌;
        r->RSI = 100.0 - (100.0 / (1.0 + RS));
    }
    r->有RSI = true;

    /* RSI_SMA */
    if (前->有RSI_SMA周期 && 前->RSI_SMA周期 > 0) {
        size_t 新长 = 前->RSI历史队列_长度 + 1;
        double* 新队列 = malloc(新长 * sizeof(double));
        if (!新队列) { 解引用(r); return NULL; }
        if (前->RSI历史队列) {
            memcpy(新队列, 前->RSI历史队列, 前->RSI历史队列_长度 * sizeof(double));
        }
        新队列[新长 - 1] = r->RSI;
        if (新长 > (size_t)前->RSI_SMA周期) {
            /* 移除最早的元素 (pop(0)) */
            for (size_t i = 1; i < 新长; i++) 新队列[i-1] = 新队列[i];
            新长--;
        }
        r->RSI历史队列 = 新队列;
        r->RSI历史队列_长度 = 新长;
        if (新长 > 0) {
            double sum = 0;
            for (size_t i = 0; i < 新长; i++) sum += 新队列[i];
            r->RSI_SMA = sum / 新长;
            r->有RSI_SMA = true;
        }
    }
    return r;
}

相对强弱指数* 相对强弱指数_增量计算_K线(相对强弱指数* 前, K线* k线, const char* 计算方式) {
    return 相对强弱指数_增量计算(前, 指标_K线取值(k线, 计算方式), k线->时间戳);
}

void 释放相对强弱指数(相对强弱指数* obj) {
    free(obj->RSI历史队列);
}

/* ================================================================
 * 随机指标 (KDJ)
 * ================================================================ */

随机指标* 随机指标_首次计算(double 高, double 低, double 收, time_t 时间,
                      int N, int M1, int M2, double 超买, double 超卖) {
    随机指标* k = 分配(sizeof(随机指标), CHAN_TYPE_随机指标);
    k->时间戳 = 时间;
    k->最高价 = 高;
    k->最低价 = 低;
    k->收盘价 = 收;
    k->N = N; k->M1 = M1; k->M2 = M2;
    k->超买阈值 = 超买;
    k->超卖阈值 = 超卖;
    k->有RSV = k->有K = k->有D = k->有J = false;
    k->RSV = k->K = k->D = k->J = 0;
    k->历史最高价队列 = malloc(sizeof(double));
    if (!k->历史最高价队列) { 解引用(k); return NULL; }
    k->历史最高价队列[0] = 高;
    k->历史最高价队列_长度 = 1;
    k->历史最低价队列 = malloc(sizeof(double));
    if (!k->历史最低价队列) { 解引用(k); return NULL; }
    k->历史最低价队列[0] = 低;
    k->历史最低价队列_长度 = 1;
    k->有前RSV = k->有前K = k->有前D = false;
    return k;
}

随机指标* 随机指标_首次计算_K线(K线* k线, const char* 计算方式,
                      int N, int M1, int M2, double 超买, double 超卖) {
    (void)计算方式;
    return 随机指标_首次计算(k线->高, k线->低, k线->收盘价, k线->时间戳, N, M1, M2, 超买, 超卖);
}

随机指标* 随机指标_增量计算(随机指标* 前, double 高, double 低, double 收, time_t 时间) {
    随机指标* k = 分配(sizeof(随机指标), CHAN_TYPE_随机指标);
    k->时间戳 = 时间;
    k->最高价 = 高; k->最低价 = 低; k->收盘价 = 收;
    k->N = 前->N; k->M1 = 前->M1; k->M2 = 前->M2;
    k->超买阈值 = 前->超买阈值; k->超卖阈值 = 前->超卖阈值;

    /* 复制并更新历史队列 */
    size_t 新长高 = 前->历史最高价队列_长度 + 1;
    k->历史最高价队列 = malloc(新长高 * sizeof(double));
    if (!k->历史最高价队列) { 解引用(k); return NULL; }
    memcpy(k->历史最高价队列, 前->历史最高价队列, 前->历史最高价队列_长度 * sizeof(double));
    k->历史最高价队列[新长高-1] = 高;
    if (新长高 > (size_t)前->N) {
        for (size_t i = 1; i < 新长高; i++) k->历史最高价队列[i-1] = k->历史最高价队列[i];
        新长高--;
    }
    k->历史最高价队列_长度 = 新长高;

    size_t 新长低 = 前->历史最低价队列_长度 + 1;
    k->历史最低价队列 = malloc(新长低 * sizeof(double));
    if (!k->历史最低价队列) { 解引用(k); return NULL; }
    memcpy(k->历史最低价队列, 前->历史最低价队列, 前->历史最低价队列_长度 * sizeof(double));
    k->历史最低价队列[新长低-1] = 低;
    if (新长低 > (size_t)前->N) {
        for (size_t i = 1; i < 新长低; i++) k->历史最低价队列[i-1] = k->历史最低价队列[i];
        新长低--;
    }
    k->历史最低价队列_长度 = 新长低;

    /* 计算RSV */
    if (新长高 == (size_t)前->N && 新长低 == (size_t)前->N) {
        double hmax = k->历史最高价队列[0];
        double lmin = k->历史最低价队列[0];
        for (size_t i = 1; i < 新长高; i++) if (k->历史最高价队列[i] > hmax) hmax = k->历史最高价队列[i];
        for (size_t i = 1; i < 新长低; i++) if (k->历史最低价队列[i] < lmin) lmin = k->历史最低价队列[i];
        if (hmax != lmin)
            k->RSV = (收 - lmin) / (hmax - lmin) * 100.0;
        else
            k->RSV = 50.0;
        k->有RSV = true;
    } else {
        k->有RSV = false;
    }

    /* K值 */
    if (k->有RSV) {
        if (!前->有K) k->K = k->RSV;
        else k->K = (前->K * (前->M1 - 1) + k->RSV) / 前->M1;
        k->有K = true;
    } else {
        k->K = 前->K; k->有K = 前->有K;
    }

    /* D值 */
    if (k->有K) {
        if (!前->有D) k->D = k->K;
        else k->D = (前->D * (前->M2 - 1) + k->K) / 前->M2;
        k->有D = true;
    } else {
        k->D = 前->D; k->有D = 前->有D;
    }

    /* J值 */
    if (k->有K && k->有D) {
        k->J = 3.0 * k->K - 2.0 * k->D;
        k->有J = true;
    }

    k->有前RSV = k->有RSV; k->前一个RSV = k->RSV;
    k->有前K = k->有K; k->前一个K = k->K;
    k->有前D = k->有D; k->前一个D = k->D;

    return k;
}

随机指标* 随机指标_增量计算_K线(随机指标* 前, K线* k线, const char* 计算方式) {
    (void)计算方式;
    return 随机指标_增量计算(前, k线->高, k线->低, k线->收盘价, k线->时间戳);
}

void 释放随机指标(随机指标* obj) {
    free(obj->历史最高价队列);
    free(obj->历史最低价队列);
}

/* ================================================================
 * K线
 * ================================================================ */

K线* K线_新建(const char* 标识, int 序号, int 周期, time_t 时间戳,
              double 开盘价, double 高, double 低, double 收盘价, double 成交量) {
    K线* k = 分配(sizeof(K线), CHAN_TYPE_K线);
    strncpy(k->标识, 标识, 63); k->标识[63] = '\0';
    k->序号 = 序号;
    k->周期 = 周期;
    k->时间戳 = 时间戳;
    k->开盘价 = 开盘价;
    k->高 = 高;
    k->低 = 低;
    k->收盘价 = 收盘价;
    k->成交量 = 成交量;
    k->macd = NULL;
    k->rsi = NULL;
    k->kdj = NULL;
    return k;
}

K线* K线_创建普K(const char* 标识, time_t 时间戳, double 开盘价, double 高,
                  double 低, double 收盘价, double 成交量, int 序号, int 周期) {
    return K线_新建(标识, 序号, 周期, 时间戳, 开盘价, 高, 低, 收盘价, 成交量);
}

相对方向 K线_方向(K线* k线) {
    return k线->开盘价 < k线->收盘价 ? 相对方向_向上 : 相对方向_向下;
}

K线* K线_读取大端字节数组(const uint8_t* 字节组, int 周期, const char* 标识) {
    /* 解包 >6d (6个 big-endian double) */
    double vals[6];
    for (int i = 0; i < 6; i++) {
        uint64_t raw = 0;
        for (int j = 0; j < 8; j++) raw = (raw << 8) | 字节组[i * 8 + j];
        uint64_t sign_bit = raw >> 63;
        int64_t exponent = ((raw >> 52) & 0x7FF) - 1023;
        uint64_t mantissa = raw & 0xFFFFFFFFFFFFFULL;
        if (exponent == -1023) {
            vals[i] = 0.0;
        } else {
            double value = 1.0 + (double)mantissa / (double)(1ULL << 52);
            vals[i] = ldexp(value, (int)exponent);
            if (sign_bit) vals[i] = -vals[i];
        }
    }
    return K线_新建(标识, 0, 周期, (time_t)vals[0],
                    vals[1], vals[2], vals[3], vals[4], vals[5]);
}

void K线_保存到DAT文件(const char* 路径, K线** 序列, size_t 长度) {
    FILE* f = fopen(路径, "wb");
    if (!f) { perror(路径); return; }
    for (size_t i = 0; i < 长度; i++) {
        double vals[6] = {
            (double)序列[i]->时间戳,
            序列[i]->开盘价, 序列[i]->高, 序列[i]->低,
            序列[i]->收盘价, 序列[i]->成交量
        };
        for (int j = 0; j < 6; j++) {
            uint64_t raw;
            memcpy(&raw, &vals[j], 8);
            /* 转为大端 */
            uint8_t be[8];
            for (int k = 7; k >= 0; k--) { be[k] = raw & 0xFF; raw >>= 8; }
            fwrite(be, 1, 8, f);
        }
    }
    fclose(f);
}

void K线_获取MACD(K线** 序列, size_t 长度, K线* 始, K线* 终,
                   double* 阳, double* 阴, double* 合, double* 总) {
    *阳 = 0; *阴 = 0;
    bool 开始 = false;
    for (size_t i = 0; i < 长度; i++) {
        if (序列[i] == 始) 开始 = true;
        if (开始 && 序列[i]->macd) {
            double h = 序列[i]->macd->MACD柱;
            if (h >= 0) *阳 += h;
            else *阴 += h;
        }
        if (序列[i] == 终) break;
    }
    *合 = *阳 + *阴;
    *总 = *阳 + fabs(*阴);
}

void 释放K线(K线* obj) {
    解引用(obj->macd);
    解引用(obj->rsi);
    解引用(obj->kdj);
}

/* ================================================================
 * 缠论K线
 * ================================================================ */

缠论K线* 缠论K线_创建缠K(time_t 时间戳, double 高, double 低, 相对方向 方向,
                         分型结构 结构, int 原始序号, K线* 普k, 缠论K线* 之前) {
    assert(高 >= 低);
    缠论K线* c = 分配(sizeof(缠论K线), CHAN_TYPE_缠论K线);
    c->时间戳 = 时间戳;
    c->高 = 高;
    c->低 = 低;
    c->方向 = 方向;
    c->分型 = 结构;
    c->原始起始序号 = 原始序号;
    c->原始结束序号 = 原始序号;
    c->标的K线 = 普k;
    strncpy(c->标识, 普k->标识, 63); c->标识[63] = '\0';
    c->周期 = 普k->周期;
    c->分型特征值 = 高;

    if (之前) {
        c->序号 = 之前->序号 + 1;
        if (相对方向_是否包含(相对方向_分析(之前->高, 之前->低, c->高, c->低))) {
            fprintf(stderr, "缠论K线_创建缠K: 包含关系不应出现\n");
            解引用(c);
            return NULL;
        }
    } else {
        c->序号 = 0;
    }
    return c;
}

缠论K线* 缠论K线_镜像(缠论K线* self) {
    缠论K线* c = 分配(sizeof(缠论K线), CHAN_TYPE_缠论K线);
    memcpy(c, self, sizeof(缠论K线));
    引用计数(c) = 1;
    return c;
}

time_t 缠论K线_时间戳对齐(缠论K线** 基线, size_t 基线长, 缠论K线* k线) {
    if (基线长 == 0) return k线->时间戳;
    for (size_t i_ = 基线长; i_ > 0; i_--) {
        size_t i = i_ - 1;
        if (基线[0]->周期 < k线->周期) {
            if (k线->时间戳 <= 基线[i]->时间戳 && 基线[i]->时间戳 <= k线->时间戳 + k线->周期) {
                if (k线->分型特征值 == 基线[i]->分型特征值) return 基线[i]->时间戳;
            }
        } else {
            if (基线[i]->时间戳 <= k线->时间戳 && k线->时间戳 <= 基线[i]->时间戳 + 基线[i]->周期) {
                if (k线->分型特征值 == 基线[i]->分型特征值) return 基线[i]->时间戳;
            }
        }
    }
    return k线->时间戳;
}

缠论K线* 缠论K线_兼并(缠论K线* 之前缠K, 缠论K线* 当前缠K, K线* 当前普K,
                     缠论配置* 配置, const char** 模式) {
    相对方向 关系 = 相对方向_分析(当前缠K->高, 当前缠K->低, 当前普K->高, 当前普K->低);
    if (!相对方向_是否包含(关系)) {
        缠论K线* 新 = 缠论K线_创建缠K(当前普K->时间戳, 当前普K->高, 当前普K->低,
                               K线_方向(当前普K),
                               相对方向_是否向下(关系) ? 分型结构_下 : 分型结构_上,
                               当前普K->序号, 当前普K, 当前缠K);
        if (新) {
            新->序号 = 当前缠K->序号 + 1;
            *模式 = "添加";
            return 新;
        }
        return NULL;
    }

    if (当前普K->序号 == 当前缠K->原始结束序号) {
        *模式 = NULL; return NULL;
    }
    if (当前普K->序号 - 1 != 当前缠K->原始结束序号 && 当前普K->序号 != 当前缠K->原始结束序号) {
        fprintf(stderr, "缠论K线_兼并: 不可追加不连续元素\n");
        *模式 = NULL; return NULL;
    }

    double (*取值函数)(double, double) = fmax;
    if (之前缠K) {
        if (相对方向_是否向下(相对方向_分析(之前缠K->高, 之前缠K->低, 当前缠K->高, 当前缠K->低)))
            取值函数 = fmin;
    }

    if (关系 != 相对方向_顺) {
        当前缠K->时间戳 = 当前普K->时间戳;
        当前缠K->标的K线 = 当前普K;
    }
    当前缠K->高 = 取值函数(当前缠K->高, 当前普K->高);
    当前缠K->低 = 取值函数(当前缠K->低, 当前普K->低);
    当前缠K->原始结束序号 = 当前普K->序号;
    当前缠K->方向 = K线_方向(当前普K);

    if (之前缠K) 当前缠K->序号 = 之前缠K->序号 + 1;

    if (配置->缠K合并替换) {
        *模式 = "替换";
        return 缠论K线_镜像(当前缠K);
    }
    *模式 = NULL;
    return NULL;
}

void 缠论K线_分析(K线* 当前K线, 动态数组* 缠K序列, 动态数组* 普K序列,
                  缠论配置* 配置, const char** 状态, 分型** 形态) {
    *状态 = NULL; *形态 = NULL;
    当前K线->标识[0] = '\0'; strncpy(当前K线->标识, 配置->标识, 63);

    if (普K序列->长度 == 0) {
        if (配置->计算指标) {
            当前K线->macd = 平滑异同移动平均线_首次计算_K线(当前K线, 配置->指标计算方式,
                配置->平滑异同移动平均线_快线周期, 配置->平滑异同移动平均线_慢线周期,
                配置->平滑异同移动平均线_信号周期);
            当前K线->rsi = 相对强弱指数_首次计算_K线(当前K线, 配置->指标计算方式,
                配置->相对强弱指数_周期, 配置->相对强弱指数_超买阈值,
                配置->相对强弱指数_超卖阈值, 配置->相对强弱指数_移动平均线周期);
            当前K线->kdj = 随机指标_首次计算_K线(当前K线, 配置->指标计算方式,
                配置->随机指标_RSV周期, 配置->随机指标_K值平滑周期,
                配置->随机指标_D值平滑周期, 配置->随机指标_超买阈值,
                配置->随机指标_超卖阈值);
        }
        动态数组_追加(普K序列, 当前K线);
        引用(当前K线);
    } else {
        K线* 之前普K = 动态数组_获取(普K序列, 普K序列->长度 - 1);
        if (之前普K->时间戳 == 当前K线->时间戳) {
            当前K线->序号 = 之前普K->序号;
            解引用(之前普K);
            入回收站(之前普K);
            动态数组_设置(普K序列, 普K序列->长度 - 1, 当前K线);
            引用(当前K线);
            if (配置->计算指标 && 普K序列->长度 >= 2) {
                K线* 前前 = 动态数组_获取(普K序列, 普K序列->长度 - 2);
                if (前前) {
                    解引用(当前K线->macd);
                    当前K线->macd = 平滑异同移动平均线_增量计算_K线(前前->macd, 当前K线, 配置->指标计算方式);
                    解引用(当前K线->rsi);
                    当前K线->rsi = 相对强弱指数_增量计算_K线(前前->rsi, 当前K线, 配置->指标计算方式);
                    解引用(当前K线->kdj);
                    当前K线->kdj = 随机指标_增量计算_K线(前前->kdj, 当前K线, 配置->指标计算方式);
                }
            }
        } else {
            if (之前普K->时间戳 > 当前K线->时间戳) {
                fprintf(stderr, "缠论K线_分析: 时序错误\n");
                return;
            }
            当前K线->序号 = 之前普K->序号 + 1;
            if (配置->计算指标) {
                当前K线->macd = 平滑异同移动平均线_增量计算_K线(之前普K->macd, 当前K线, 配置->指标计算方式);
                当前K线->rsi = 相对强弱指数_增量计算_K线(之前普K->rsi, 当前K线, 配置->指标计算方式);
                当前K线->kdj = 随机指标_增量计算_K线(之前普K->kdj, 当前K线, 配置->指标计算方式);
            }
            动态数组_追加(普K序列, 当前K线);
            引用(当前K线);
        }
    }

    缠论K线* 之前缠K = NULL;
    if (缠K序列->长度 > 1) 之前缠K = 动态数组_获取(缠K序列, 缠K序列->长度 - 2);

    if (缠K序列->长度 > 0) {
        缠论K线* 当前缠K = 动态数组_获取(缠K序列, 缠K序列->长度 - 1);
        const char* 模式 = NULL;
        缠论K线* 新缠K = 缠论K线_兼并(之前缠K, 当前缠K, 当前K线, 配置, &模式);
        if (新缠K) {
            if (strcmp(模式, "添加") == 0) {
                动态数组_追加(缠K序列, 新缠K);
                引用(新缠K);
                *状态 = "创建";
            } else if (strcmp(模式, "替换") == 0) {
                缠论K线* 旧缠K = 动态数组_获取(缠K序列, 缠K序列->长度 - 1);
                解引用(旧缠K);
                入回收站(旧缠K);
                动态数组_设置(缠K序列, 缠K序列->长度 - 1, 新缠K);
                引用(新缠K);
                *状态 = "替换";
            }
        } else {
            *状态 = "兼并";
        }
    } else {
        缠论K线* 新缠K = 缠论K线_创建缠K(当前K线->时间戳, 当前K线->高, 当前K线->低,
                                   K线_方向(当前K线), 分型结构_散, 当前K线->序号, 当前K线, NULL);
        动态数组_追加(缠K序列, 新缠K);
        引用(新缠K);
        *状态 = "新建";
    }

    if (缠K序列->长度 < 3) return;

    缠论K线* 左 = 动态数组_获取(缠K序列, 缠K序列->长度 - 3);
    缠论K线* 中 = 动态数组_获取(缠K序列, 缠K序列->长度 - 2);
    缠论K线* 右 = 动态数组_获取(缠K序列, 缠K序列->长度 - 1);

    缺口 _左边 =  {.高 = 左->高, .低 = 左->低};
    缺口 _中间 =  {.高 = 中->高, .低 = 中->低};
    缺口 _右边 =  {.高 = 右->高, .低 = 右->低};

    分型结构 结构 = 分型结构_分析(&_左边, &_中间, &_右边, false, false);
    中->分型 = 结构;

    switch (结构) {
        case 分型结构_底:
            中->分型特征值 = 中->低;
            右->分型特征值 = 右->高;
            右->分型 = 分型结构_顶;
            break;
        case 分型结构_顶:
            中->分型特征值 = 中->高;
            右->分型特征值 = 右->低;
            右->分型 = 分型结构_底;
            break;
        case 分型结构_上:
            中->分型特征值 = 中->高;
            右->分型特征值 = 右->高;
            右->分型 = 分型结构_顶;
            break;
        case 分型结构_下:
            中->分型特征值 = 中->低;
            右->分型特征值 = 右->低;
            右->分型 = 分型结构_底;
            break;
        default: break;
    }

    *形态 = 分型_新建(左, 中, 右);
    if (结构 == 分型结构_上 || 结构 == 分型结构_下) {
        解引用(*形态);
        *形态 = 分型_新建(中, 右, NULL);
    }
}

void 释放缠论K线(缠论K线* obj) {
    (void)obj;
}

/* ================================================================
 * 分型
 * ================================================================ */

分型* 分型_新建(缠论K线* 左, 缠论K线* 中, 缠论K线* 右) {
    if (左 && 右) assert(左->时间戳 < 中->时间戳 && 中->时间戳 < 右->时间戳);
    分型* f = 分配(sizeof(分型), CHAN_TYPE_分型);
    f->左 = 左;
    f->中 = 中;
    f->右 = 右;
    f->结构 = 中->分型;
    f->时间戳 = 中->时间戳;
    f->分型特征值 = 中->分型特征值;
    return f;
}

bool 分型_判断分型(分型* 左, 分型* 右, const char* 模式) {
    (void)模式;
    if (左 == 右) return true;
    if (左 && 右 && 左->中 == 右->中) return true;
    return false;
}

分型* 分型_从缠K序列中获取分型(缠论K线** 序列, size_t 长度, 缠论K线* 中) {
    for (size_t i = 0; i < 长度; i++) {
        if (序列[i] == 中) {
            if (i > 0 && i + 1 < 长度)
                return 分型_新建(序列[i-1], 中, 序列[i+1]);
            else if (i > 0)
                return 分型_新建(序列[i-1], 中, NULL);
            break;
        }
    }
    return NULL;
}

void 分型_向序列中添加(动态数组* 分型序列, 分型* 当前分型) {
    if (分型序列->长度 == 0 && (当前分型->结构 != 分型结构_顶 && 当前分型->结构 != 分型结构_底)) {
        fprintf(stderr, "分型_向序列中添加: 首次添加不为顶底\n");
        return;
    }
    if (分型序列->长度 > 0) {
        分型* 末 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (末->结构 == 当前分型->结构) {
            fprintf(stderr, "分型_向序列中添加: 分型相同无法添加\n");
            return;
        }
        if (末->右 == NULL)
            fprintf(stderr, "分型_向序列中添加: 分型异常\n");
    }
    动态数组_追加(分型序列, 当前分型);
    引用(当前分型);
}

void 释放分型(分型* obj) {
    (void)obj;
}

/* ================================================================
 * 虚线
 * ================================================================ */

虚线* 虚线_新建(int 序号, const char* 标识, 分型* 文, 分型* 武, int 级别, bool 有效性) {
    虚线* x = 分配(sizeof(虚线), CHAN_TYPE_虚线);
    x->序号 = 序号;
    strncpy(x->标识, 标识, 63); x->标识[63] = '\0';
    x->级别 = 级别;
    x->文 = 文;
    x->武 = 武;
    x->有效性 = 有效性;
    /* 缓存 高/低 */
    if ((文->结构 == 分型结构_顶 && (武->结构 == 分型结构_底 || 武->结构 == 分型结构_下)) ||
        (文->结构 == 分型结构_底 && (武->结构 == 分型结构_顶 || 武->结构 == 分型结构_上))) {
        /* 方向可识别 */
        bool 向上 = (文->结构 == 分型结构_底);
        x->高 = 向上 ? 武->中->高 : 文->中->高;
        x->低 = 向上 ? 文->中->低 : 武->中->低;
    } else {
        x->高 = 文->中->高;
        x->低 = 武->中->低;
    }
    动态数组_初始化(&x->基础序列, 0);
    动态数组_初始化(&x->特征序列, 3);
    动态数组_初始化(&x->实_中枢序列, 4);
    动态数组_初始化(&x->虚_中枢序列, 4);
    动态数组_初始化(&x->合_中枢序列, 4);
    x->确认K线 = NULL;
    strcpy(x->模式, "文武");
    x->_特征序列_显示 = false;
    x->前一缺口 = NULL;
    x->前一结束位置 = NULL;
    x->短路修正 = false;
    return x;
}

虚线* 虚线_创建笔(分型* 文, 分型* 武, bool 有效性) {
    return 虚线_新建(0, "笔", 文, 武, 1, 有效性);
}

虚线* 虚线_创建线段(动态数组* 虚线序列) {
    虚线* 首 = 动态数组_获取(虚线序列, 0);
    虚线* 末 = 动态数组_获取(虚线序列, 虚线序列->长度 - 1);
    char buf[128];
    if (strcmp(首->标识, "笔") != 0) snprintf(buf, 127, "线段<%s>", 首->标识);
    else strcpy(buf, "线段");

    虚线* 段 = 虚线_新建(0, buf, 首->文, 末->武, 首->级别 + 1, true);
    /* 初始化特征序列为 [NULL, NULL, NULL] */
    动态数组_追加(&段->特征序列, NULL);
    动态数组_追加(&段->特征序列, NULL);
    动态数组_追加(&段->特征序列, NULL);

    for (size_t i = 0; i < 虚线序列->长度; i++)
        虚线指针列表_追加(&段->基础序列, 动态数组_获取(虚线序列, i));
    return 段;
}

相对方向 虚线_方向(虚线* self) {
    if (self->文->结构 == 分型结构_顶 && (self->武->结构 == 分型结构_底 || self->武->结构 == 分型结构_下))
        return 相对方向_向下;
    if (self->文->结构 == 分型结构_底 && (self->武->结构 == 分型结构_顶 || self->武->结构 == 分型结构_上))
        return 相对方向_向上;
    fprintf(stderr, "虚线_方向: 无法识别\n");
    return 相对方向_向上;
}

double 虚线_高(虚线* self) {
    if (虚线_方向(self) == 相对方向_向上) return self->武->中->高;
    return self->文->中->高;
}

double 虚线_低(虚线* self) {
    if (虚线_方向(self) == 相对方向_向下) return self->武->中->低;
    return self->文->中->低;
}

bool 虚线_之前是(虚线* self, 虚线* 之前) {
    if (strcmp(self->标识, 之前->标识) != 0) return false;
    return 分型_判断分型(之前->武, self->文, "中");
}

bool 虚线_之后是(虚线* self, 虚线* 之后) {
    if (strcmp(self->标识, 之后->标识) != 0) return false;
    return 分型_判断分型(self->武, 之后->文, "中");
}

void 虚线_获取普K序列(虚线* self, 观察者* obs, K线*** out, size_t* out_len) {
    size_t si = 动态数组_查找(&obs->普通K线序列, self->文->中->标的K线);
    size_t ei = 动态数组_查找(&obs->普通K线序列, self->武->中->标的K线);
    *out_len = (si != (size_t)-1 && ei != (size_t)-1 && ei >= si) ? (ei - si + 1) : 0;
    *out = (*out_len > 0) ? (K线**)(obs->普通K线序列.数据 + si) : NULL;
}

void 虚线_获取缠K序列(虚线* self, 观察者* obs, 缠论K线*** out, size_t* out_len) {
    size_t si = 动态数组_查找(&obs->缠论K线序列, self->文->中);
    size_t ei = 动态数组_查找(&obs->缠论K线序列, self->武->中);
    *out_len = (si != (size_t)-1 && ei != (size_t)-1 && ei >= si) ? (ei - si + 1) : 0;
    *out = (*out_len > 0) ? (缠论K线**)(obs->缠论K线序列.数据 + si) : NULL;
}

void 释放虚线(虚线* obj) {
    虚线指针列表_全部解引用(&obj->基础序列);
    线段特征指针列表_全部解引用(&obj->特征序列);

    /* 内部中枢预释放（平衡创建引用）+ 释放（平衡序列引用） */
    #define 中枢序列_预释放(seq) do { \
        for (size_t _i = 0; _i < (seq).长度; _i++) \
            解引用(动态数组_获取(&(seq), _i)); \
        中枢指针列表_全部解引用(&(seq)); \
    } while(0)
    中枢序列_预释放(obj->实_中枢序列);
    中枢序列_预释放(obj->虚_中枢序列);
    中枢序列_预释放(obj->合_中枢序列);
    #undef 中枢序列_预释放

    解引用(obj->前一缺口);
}

/* ================================================================
 * 笔
 * ================================================================ */

int 笔_获取缠K数量(动态数组* 缠K序列, 动态数组* 笔序列, 缠论配置* 配置) {
    int 实际数量 = (int)缠K序列->长度;
    if (实际数量 >= 配置->笔内元素数量) return 实际数量;

    if (配置->笔弱化 && 实际数量 >= 3) {
        缠论K线* 高 = 笔_实际高点(缠K序列, 配置->笔内相同终点取舍);
        缠论K线* 低 = 笔_实际低点(缠K序列, 配置->笔内相同终点取舍);
        int 原始数量 = 1 + abs(低->标的K线->序号 - 高->标的K线->序号);
        if (原始数量 >= 配置->笔内元素数量) return 配置->笔内元素数量;

        if (笔序列->长度 > 0) {
            虚线* b = 笔_根据缠K找笔(笔序列, 高, 1);
            if (!b) b = 笔_根据缠K找笔(笔序列, 低, 1);
            if (b) {
                if (虚线_方向(b) == 相对方向_向上 && 低->低 < b->低)
                    if (原始数量 >= 配置->笔弱化_原始数量) return 配置->笔内元素数量;
                if (虚线_方向(b) == 相对方向_向下 && 低->低 > b->高)
                    if (原始数量 >= 配置->笔弱化_原始数量) return 配置->笔内元素数量;
            }
        }
    }
    return 实际数量;
}

static int 缠K_按高比较(const void* a, const void* b) {
    double ha = (*(缠论K线**)a)->高, hb = (*(缠论K线**)b)->高;
    return (ha > hb) - (ha < hb);
}
static int 缠K_按低比较(const void* a, const void* b) {
    double la = (*(缠论K线**)a)->低, lb = (*(缠论K线**)b)->低;
    return (la > lb) - (la < lb);
}
static int 缠K_按时间比较(const void* a, const void* b) {
    time_t ta = (*(缠论K线**)a)->时间戳, tb = (*(缠论K线**)b)->时间戳;
    return (ta > tb) - (ta < tb);
}

缠论K线* 笔_实际高点(动态数组* 缠K序列, bool 笔内相同终点取舍) {
    if (缠K序列->长度 == 0) return NULL;
    /* 找最大->高 */
    缠论K线* max_k = 动态数组_获取(缠K序列, 0);
    for (size_t i = 1; i < 缠K序列->长度; i++) {
        缠论K线* c = 动态数组_获取(缠K序列, i);
        if (c->高 > max_k->高) max_k = c;
    }
    /* 收集所有等于max高的 */
    缠论K线** eq = malloc(缠K序列->长度 * sizeof(缠论K线*));
    size_t eqn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线* c = 动态数组_获取(缠K序列, i);
        if (c->高 == max_k->高) eq[eqn++] = c;
    }
    qsort(eq, eqn, sizeof(缠论K线*), 缠K_按时间比较);
    缠论K线* result = 笔内相同终点取舍 ? eq[eqn-1] : eq[0];
    free(eq);
    return result;
}

缠论K线* 笔_实际低点(动态数组* 缠K序列, bool 笔内相同终点取舍) {
    if (缠K序列->长度 == 0) return NULL;
    缠论K线* min_k = 动态数组_获取(缠K序列, 0);
    for (size_t i = 1; i < 缠K序列->长度; i++) {
        缠论K线* c = 动态数组_获取(缠K序列, i);
        if (c->低 < min_k->低) min_k = c;
    }
    缠论K线** eq = malloc(缠K序列->长度 * sizeof(缠论K线*));
    size_t eqn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线* c = 动态数组_获取(缠K序列, i);
        if (c->低 == min_k->低) eq[eqn++] = c;
    }
    qsort(eq, eqn, sizeof(缠论K线*), 缠K_按时间比较);
    缠论K线* result = 笔内相同终点取舍 ? eq[eqn-1] : eq[0];
    free(eq);
    return result;
}

缠论K线* 笔_次高(动态数组* 缠K序列, bool 笔内相同终点取舍) {
    缠论K线* actual = 笔_实际高点(缠K序列, 笔内相同终点取舍);
    /* 排除最->高 */
    缠论K线** filtered = malloc(缠K序列->长度 * sizeof(缠论K线*));
    size_t fn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线* c = 动态数组_获取(缠K序列, i);
        if (c->高 != actual->高) filtered[fn++] = c;
    }
    /* 找次->高 */
    if (fn == 0) { free(filtered); return actual; }
    缠论K线* max2 = filtered[0];
    for (size_t i = 1; i < fn; i++)
        if (filtered[i]->高 > max2->高) max2 = filtered[i];
    /* 收集次->高 */
    size_t eqn = 0;
    for (size_t i = 0; i < fn; i++)
        if (filtered[i]->高 == max2->高) filtered[eqn++] = filtered[i];
    qsort(filtered, eqn, sizeof(缠论K线*), 缠K_按时间比较);
    缠论K线* result = 笔内相同终点取舍 ? filtered[eqn-1] : filtered[0];
    free(filtered);
    return result;
}

缠论K线* 笔_次低(动态数组* 缠K序列, bool 笔内相同终点取舍) {
    缠论K线* actual = 笔_实际低点(缠K序列, 笔内相同终点取舍);
    缠论K线** filtered = malloc(缠K序列->长度 * sizeof(缠论K线*));
    size_t fn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线* c = 动态数组_获取(缠K序列, i);
        if (c->低 != actual->低) filtered[fn++] = c;
    }
    if (fn == 0) { free(filtered); return actual; }
    缠论K线* min2 = filtered[0];
    for (size_t i = 1; i < fn; i++)
        if (filtered[i]->低 < min2->低) min2 = filtered[i];
    size_t eqn = 0;
    for (size_t i = 0; i < fn; i++)
        if (filtered[i]->低 == min2->低) filtered[eqn++] = filtered[i];
    qsort(filtered, eqn, sizeof(缠论K线*), 缠K_按时间比较);
    缠论K线* result = 笔内相同终点取舍 ? filtered[eqn-1] : filtered[0];
    free(filtered);
    return result;
}

bool 笔_相对关系(虚线* 筆, 缠论配置* 配置) {
    相对方向 rel;
    if (配置->笔内起始分型包含整笔) {
        缠论K线* ws[] = {筆->文->左, 筆->文->中, 筆->文->右};
        缠论K线* wmax = ws[0];
        for (int i = 0; i < 3; i++) if (ws[i] && ws[i]->高 > wmax->高) wmax = ws[i];
        缠论K线* wmin = ws[0];
        for (int i = 0; i < 3; i++) if (ws[i] && ws[i]->低 < wmin->低) wmin = ws[i];
        double wh = wmax ? wmax->高 : 0, wl = wmin ? wmin->低 : 0;

        缠论K线* us[3] = {筆->武->左, 筆->武->中, 配置->笔内起始分型包含整笔_包括右 ? 筆->武->右 : NULL};
        缠论K线* umax = us[0]; for (int i = 0; i < 3; i++) if (us[i] && us[i]->高 > umax->高) umax = us[i];
        缠论K线* umin = us[0]; for (int i = 0; i < 3; i++) if (us[i] && us[i]->低 < umin->低) umin = us[i];
        double uh = umax ? umax->高 : 0, ul = umin ? umin->低 : 0;

        rel = 相对方向_分析(wh, wl, uh, ul);
    } else {
        rel = 相对方向_分析(筆->文->中->高, 筆->文->中->低, 筆->武->中->高, 筆->武->中->低);
        if (配置->笔内原始K线包含整笔 &&
            相对方向_是否包含(相对方向_分析(筆->文->中->标的K线->高, 筆->文->中->标的K线->低,
                                  筆->武->中->标的K线->高, 筆->武->中->标的K线->低)))
            return false;
    }
    if (虚线_方向(筆) == 相对方向_向下) return 相对方向_是否向下(rel);
    return 相对方向_是否向上(rel);
}

static void 笔_弹出旧笔(动态数组* 分型序列, 动态数组* 笔序列) {
    分型* 旧分型 = 动态数组_弹出(分型序列);
    解引用(旧分型);
    入回收站(旧分型);
    if (笔序列->长度 > 0) {
        虚线* 旧笔 = 动态数组_弹出(笔序列);
        assert(旧笔->武 == 旧分型);
        旧笔->有效性 = false;
        解引用(旧笔);
        笔释放于弹出旧笔++;
        入回收站(旧笔);
    }
}

static void 笔_添加新笔(动态数组* 分型序列, 动态数组* 笔序列, 分型* 待添加分型, 虚线* 待添加新笔) {
    if (分型序列->长度 == 0 && (待添加分型->结构 != 分型结构_顶 && 待添加分型->结构 != 分型结构_底)) {
        fprintf(stderr, "笔_添加新笔: 首次添加不为顶底\n");
        解引用(待添加新笔);
        return;
    }
    if (分型序列->长度 > 0) {
        分型* 末 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (末->结构 == 待添加分型->结构) {
            fprintf(stderr, "笔_添加新笔: 分型相同\n");
            解引用(待添加新笔);
            return;
        }
    }
    动态数组_追加(分型序列, 待添加分型);
    引用(待添加分型);
    if (笔序列->长度 > 0) {
        虚线* 末笔 = 动态数组_获取(笔序列, 笔序列->长度 - 1);
        if (!虚线_之后是(末笔, 待添加新笔)) {
            fprintf(stderr, "笔_添加新笔: 不连续\n");
            解引用(待添加新笔);
            /* 回滚刚加入的分型: 解引用平衡序列引用, 入回收站处理创建引用 */
            分型* 回滚 = 动态数组_弹出(分型序列);
            解引用(回滚);
            入回收站(回滚);
            return;
        }
        待添加新笔->序号 = 末笔->序号 + 1;
        if (待添加新笔->武->左 == NULL && 待添加新笔->武->右 == NULL)
            待添加新笔->有效性 = false;
    }
    动态数组_追加(笔序列, 待添加新笔);
    引用(待添加新笔);
    笔加入序列计数++;
}

/*
 * 笔_分析 — 显式栈版本（不依赖 C 调用栈）
 *
 * 原 Python 使用深度递归（max 64 层），C99 中以显式栈模拟。
 * 栈帧按 LIFO 顺序压入：后压入者先执行。
 */
#define 笔分析_MAX栈深 128

typedef struct {
    分型* 待分析;
    int   层级;
} 笔分析帧;

void 笔_分析(分型* 初始分型, 动态数组* 分型序列, 动态数组* 笔序列,
            动态数组* 缠K序列, 动态数组* 普K序列,
            缠论配置* 配置) {

    笔分析帧 栈[笔分析_MAX栈深];
    int 栈顶 = 0;
    栈[栈顶++] = (笔分析帧){初始分型, 0};

    while (栈顶 > 0) {
        笔分析帧 帧 = 栈[--栈顶];
        分型* 当前分型 = 帧.待分析;
        int 层次 = 帧.层级;

        if (!当前分型) continue;
        if (层次 > 64) {
            fprintf(stderr, "笔_分析: 深度超出 %d\n", 层次);
            解引用(当前分型);
            continue;
        }
        if (当前分型->结构 != 分型结构_顶 && 当前分型->结构 != 分型结构_底) {
            解引用(当前分型);
            continue;
        }

        if (分型序列->长度 == 0) {
            if (当前分型->结构 == 分型结构_顶 || 当前分型->结构 == 分型结构_底)
                分型_向序列中添加(分型序列, 当前分型);
            continue;
        }

        分型* 之前分型 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (之前分型->中->时间戳 == 当前分型->中->时间戳 ||
            之前分型->结构 == 分型结构_上 || 之前分型->结构 == 分型结构_下) {
            笔_弹出旧笔(分型序列, 笔序列);
            if (分型序列->长度 == 0) {
                if (当前分型->右 != NULL) {
                    分型_向序列中添加(分型序列, 当前分型);
                } else {
                    解引用(当前分型);
                }
                continue;
            }
        }

        之前分型 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (之前分型->中->时间戳 > 当前分型->中->时间戳 &&
            之前分型->中->序号 - 当前分型->中->序号 > 1) {
            fprintf(stderr, "笔_分析: 时序错误 层次%d\n", 层次);
            解引用(当前分型);
            continue;
        }

        /* --- 弱化：弹出旧笔后重新分析 --- */
        if (配置->笔弱化 && 笔序列->长度 > 0) {
            虚线* 前一笔 = 动态数组_获取(笔序列, 笔序列->长度 - 1);
            if (前一笔->武->中->序号 - 前一笔->文->中->序号 + 1 == 3) {
                if ((虚线_方向(前一笔) == 相对方向_向上 && 虚线_低(前一笔) > 当前分型->分型特征值 && 当前分型->结构 == 分型结构_底) ||
                    (虚线_方向(前一笔) == 相对方向_向下 && 虚线_高(前一笔) < 当前分型->分型特征值 && 当前分型->结构 == 分型结构_顶)) {
                    笔_弹出旧笔(分型序列, 笔序列);
                    if (栈顶 < 笔分析_MAX栈深)
                        栈[栈顶++] = (笔分析帧){当前分型, 层次 + 1};
                    else
                        解引用(当前分型);
                    continue;
                }
            }
        }

        /* --- 不同结构：尝试成笔 --- */
        if (之前分型->结构 != 当前分型->结构) {
            size_t si = 动态数组_查找(缠K序列, 之前分型->中);
            size_t ei = 动态数组_查找(缠K序列, 当前分型->中);
            size_t 基础长 = 0;
            if (si != (size_t)-1 && ei != (size_t)-1 && ei >= si)
                基础长 = ei - si + 1;

            虚线* 当前笔 = 虚线_创建笔(之前分型, 当前分型, true);
            笔创建计数++;
            int 缠K数 = 0;
            if (基础长 > 0) {
                动态数组 tmp = { .数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长 };
                缠K数 = 笔_获取缠K数量(&tmp, 笔序列, 配置);
            }

            if (缠K数 >= 配置->笔内元素数量) {
                缠论K线* 文官 = NULL;
                if (基础长 > 0) {
                    动态数组 tmp = { .数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长 };
                    文官 = (之前分型->结构 == 分型结构_顶 && 当前分型->结构 == 分型结构_底)
                        ? 笔_实际高点(&tmp, false) : 笔_实际低点(&tmp, false);
                }

                if (文官 && 文官 != 之前分型->中) {
                    分型* 临时分型 = 分型_从缠K序列中获取分型(
                        (缠论K线**)缠K序列->数据, 缠K序列->长度, 文官);
                    if (临时分型) {
                        /* 压入：先压 当前分型，后压 临时分型 → 临时分型先执行 */
                        if (栈顶 + 1 < 笔分析_MAX栈深) {
                            栈[栈顶++] = (笔分析帧){当前分型, 层次 + 1};
                            栈[栈顶++] = (笔分析帧){临时分型, 层次 + 1};
                        } else {
                            解引用(临时分型);
                            解引用(当前分型);
                        }
                        解引用(当前笔);
                        笔释放于文官路径++;
                        continue;
                    }
                }

                /* 武将判断 */
                缠论K线* 武将 = NULL;
                if (基础长 > 0) {
                    动态数组 tmp = { .数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长 };
                    武将 = (之前分型->结构 == 分型结构_顶 && 当前分型->结构 == 分型结构_底)
                        ? 笔_实际低点(&tmp, 配置->笔内相同终点取舍)
                        : 笔_实际高点(&tmp, 配置->笔内相同终点取舍);
                }

                if (笔_相对关系(当前笔, 配置) && 当前分型->中 == 武将) {
                    笔_添加新笔(分型序列, 笔序列, 当前分型, 当前笔);
                    continue;
                }

                if (配置->笔次级成笔 && 基础长 > 0) {
                    动态数组 tmp = { .数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长 };
                    武将 = (之前分型->结构 == 分型结构_顶 && 当前分型->结构 == 分型结构_底)
                        ? 笔_次低(&tmp, 配置->笔内相同终点取舍)
                        : 笔_次高(&tmp, 配置->笔内相同终点取舍);
                    if (笔_相对关系(当前笔, 配置) && 当前分型->中 == 武将) {
                        笔_添加新笔(分型序列, 笔序列, 当前分型, 当前笔);
                        continue;
                    }
                }
            } else {
                /* 长度不足：尝试以当前分型的右分型重新分析 */
                if (当前分型->右) {
                    分型* 临时分型 = 分型_从缠K序列中获取分型(
                        (缠论K线**)缠K序列->数据, 缠K序列->长度, 当前分型->右);
                    if (临时分型 && 栈顶 < 笔分析_MAX栈深)
                        栈[栈顶++] = (笔分析帧){临时分型, 层次 + 1};
                    else if (临时分型)
                        解引用(临时分型);
                }
            }
            解引用(当前笔);
            笔释放于底部++;
            解引用(当前分型);
        } else {
            /* --- 相同结构：新分型更极端时弹出并重建 --- */
            double fv = 当前分型->分型特征值;
            if ((之前分型->结构 == 分型结构_顶 && 之前分型->分型特征值 < fv) ||
                (之前分型->结构 == 分型结构_底 && 之前分型->分型特征值 > fv)) {

                笔_弹出旧笔(分型序列, 笔序列);

                size_t si = 动态数组_查找(缠K序列, 之前分型->中);
                size_t ei = 动态数组_查找(缠K序列, 当前分型->中);
                if (si != (size_t)-1 && ei != (size_t)-1 && ei >= si) {
                    动态数组 tmp = { .数据 = 缠K序列->数据 + si, .长度 = ei - si + 1, .容量 = ei - si + 1 };
                    缠论K线* 武将 = (之前分型->结构 == 分型结构_顶)
                        ? 笔_实际低点(&tmp, 配置->笔内相同终点取舍)
                        : 笔_实际高点(&tmp, 配置->笔内相同终点取舍);
                    分型* 临时分型 = 分型_从缠K序列中获取分型(
                        (缠论K线**)缠K序列->数据, 缠K序列->长度, 武将);

                    if (分型序列->长度 > 0 && 临时分型) {
                        /* 修复错过笔：Python 按正序递归处理错过笔。
                           LIFO 栈需逆序压入，使正序执行：
                           执行序: 临时分型 → 错过笔1 → 错过笔2 → ... → 当前分型 */
                        int 错过起始 = 栈顶;
                        /* 先压入 当前分型（最后执行） */
                        if (栈顶 < 笔分析_MAX栈深) {
                            栈[栈顶++] = (笔分析帧){当前分型, 层次 + 1};
                        } else {
                            解引用(临时分型);
                            分型_向序列中添加(分型序列, 当前分型);
                            continue;
                        }
                        /* 收集期间错过的笔 */
                        size_t si2 = 动态数组_查找(缠K序列, 武将);
                        if (si2 != (size_t)-1) {
                            for (size_t j = si2; j < 缠K序列->长度; j++) {
                                缠论K线* ck = 动态数组_获取(缠K序列, j);
                                if (ck->分型 == 分型结构_底 || ck->分型 == 分型结构_顶) {
                                    分型* tf = 分型_从缠K序列中获取分型(
                                        (缠论K线**)缠K序列->数据, 缠K序列->长度, ck);
                                    if (tf && 栈顶 < 笔分析_MAX栈深) {
                                        栈[栈顶++] = (笔分析帧){tf, 层次 + 1};
                                    } else if (tf) {
                                        解引用(tf);
                                    }
                                }
                            }
                        }
                        /* 逆序错过笔段，使正序执行 */
                        for (int a = 错过起始 + 1, b = 栈顶 - 1; a < b; a++, b--) {
                            笔分析帧 t = 栈[a]; 栈[a] = 栈[b]; 栈[b] = t;
                        }
                        /* 压入 临时分型（最先执行） */
                        if (栈顶 < 笔分析_MAX栈深)
                            栈[栈顶++] = (笔分析帧){临时分型, 层次 + 1};
                        else
                            解引用(临时分型);
                    } else {
                        if (临时分型) 解引用(临时分型);
                        分型_向序列中添加(分型序列, 当前分型);
                    }
                }
            } else {
                解引用(当前分型);
            }
        }
    }
}

虚线* 笔_以文会友(动态数组* 笔序列, 分型* 文) {
    for (size_t i = 0; i < 笔序列->长度; i++) {
        虚线* b = 动态数组_获取(笔序列, i);
        if (b->文 == 文) return b;
    }
    return NULL;
}

虚线* 笔_以武会友(动态数组* 笔序列, 分型* 武) {
    for (size_t i = 笔序列->长度; i > 0; i--) {
        虚线* b = 动态数组_获取(笔序列, i - 1);
        if (b->武 == 武) return b;
    }
    return NULL;
}

虚线* 笔_根据缠K找笔(动态数组* 笔序列, 缠论K线* 缠K, int 偏移) {
    for (size_t i = 笔序列->长度; i > 0; i--) {
        虚线* b = 动态数组_获取(笔序列, i - 1);
        if (b->文->中->序号 - 偏移 <= 缠K->序号 && 缠K->序号 <= b->武->中->序号)
            return b;
    }
    return NULL;
}

/* ================================================================
 * 线段特征
 * ================================================================ */

线段特征* 线段特征_新建(const char* 标识, 动态数组* 基础序列, 相对方向 线段方向) {
    线段特征* t = 分配(sizeof(线段特征), CHAN_TYPE_线段特征);
    t->序号 = 0;
    strncpy(t->标识, 标识, 127); t->标识[127] = '\0';
    t->线段方向 = 线段方向;
    动态数组_初始化(&t->元素, 4);
    for (size_t i = 0; i < 基础序列->长度; i++) {
        虚线* b = 动态数组_获取(基础序列, i);
        动态数组_追加(&t->元素, b);
        引用(b);
    }
    return t;
}

分型* 线段特征_文(线段特征* self) {
    if (self->元素.长度 == 0) return NULL;
    double (*func)(double, double) = (self->线段方向 == 相对方向_向上) ? fmax : fmin;
    虚线* best = 动态数组_获取(&self->元素, 0);
    for (size_t i = 1; i < self->元素.长度; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (func(x->文->中->分型特征值, best->文->中->分型特征值) == x->文->中->分型特征值)
            best = x;
    }
    return best->文;
}

分型* 线段特征_武(线段特征* self) {
    if (self->元素.长度 == 0) return NULL;
    double (*func)(double, double) = (self->线段方向 == 相对方向_向上) ? fmax : fmin;
    虚线* best = 动态数组_获取(&self->元素, 0);
    for (size_t i = 1; i < self->元素.长度; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (func(x->武->中->分型特征值, best->武->中->分型特征值) == x->武->中->分型特征值)
            best = x;
    }
    return best->武;
}

double 线段特征_高(线段特征* self) {
    分型* w = 线段特征_文(self);
    分型* u = 线段特征_武(self);
    double hw = w ? w->中->分型特征值 : 0;
    double hu = u ? u->中->分型特征值 : 0;
    return fmax(hw, hu);
}

double 线段特征_低(线段特征* self) {
    分型* w = 线段特征_文(self);
    分型* u = 线段特征_武(self);
    double lw = w ? w->中->分型特征值 : 0;
    double lu = u ? u->中->分型特征值 : 0;
    return fmin(lw, lu);
}

相对方向 线段特征_方向(线段特征* self) {
    return 相对方向_翻转(self->线段方向);
}

void 线段特征_添加(线段特征* self, 虚线* 待添加) {
    if (虚线_方向(待添加) == self->线段方向) {
        fprintf(stderr, "线段特征_添加: 方向不匹配\n");
        return;
    }
    动态数组_追加(&self->元素, 待添加);
    引用(待添加);
}

void 线段特征_删除(线段特征* self, 虚线* 待删除) {
    size_t idx = 动态数组_查找(&self->元素, 待删除);
    if (idx != (size_t)-1) {
        for (size_t i = idx; i < self->元素.长度 - 1; i++)
            self->元素.数据[i] = self->元素.数据[i + 1];
        解引用(待删除);
        self->元素.长度--;
    }
}

void 线段特征_静态分析(动态数组* 虚线序列, 相对方向 线段方向, const char* 四象,
                       bool 是否忽视, 动态数组* 结果) {
    /* 确定需要合并的方向序列 */
    bool is_老 = (strcmp(四象, "老阳") == 0 || strcmp(四象, "老阴") == 0);
    bool 严格包含 = is_老 && !是否忽视;

    for (size_t idx = 0; idx < 虚线序列->长度; idx++) {
        虚线* 当前虚线 = 动态数组_获取(虚线序列, idx);

        /* 情况1：方向相同（可能触发分型替换） */
        if (虚线_方向(当前虚线) == 线段方向) {
            if (结果->长度 < 3) continue;
            线段特征* 左 = 动态数组_获取(结果, 结果->长度 - 3);
            线段特征* 中 = 动态数组_获取(结果, 结果->长度 - 2);
            线段特征* 右 = 动态数组_获取(结果, 结果->长度 - 1);

            /* 使用分型分析判断结构 */
            缺口 fl = { .高 = 线段特征_高(左), .低 = 线段特征_低(左) };
            缺口 fm = { .高 = 线段特征_高(中), .低 = 线段特征_低(中) };
            缺口 fr = { .高 = 线段特征_高(右), .低 = 线段特征_低(右) };

            分型结构 结构 = 分型结构_分析(&fl, &fm, &fr, true, true);

            if ((线段方向 == 相对方向_向上 && 结构 == 分型结构_顶 && 虚线_高(当前虚线) > 线段特征_高(中)) ||
                (线段方向 == 相对方向_向下 && 结构 == 分型结构_底 && 虚线_低(当前虚线) < 线段特征_低(中))) {
                /* 找序号最小和最大的虚线 */
                虚线* 小号 = 中->元素.数据[0];
                虚线* 大号 = 右->元素.数据[0];
                for (size_t j = 1; j < 中->元素.长度; j++) {
                    if (((虚线*)中->元素.数据[j])->序号 < 小号->序号) 小号 = 中->元素.数据[j];
                }
                for (size_t j = 1; j < 右->元素.长度; j++) {
                    if (((虚线*)右->元素.数据[j])->序号 > 大号->序号) 大号 = 右->元素.数据[j];
                }
                虚线* fake = 虚线_创建笔(小号->文, 大号->武, false);
                解引用(动态数组_弹出(结果)); /* pop & free old right */
                动态数组 tmp2 = { .数据 = malloc(sizeof(void*)), .长度 = 0, .容量 = 1 };
                if (!tmp2.数据) { perror("线段特征_静态分析"); abort(); }
                动态数组_追加(&tmp2, fake);
                线段特征* new_ft = 线段特征_新建("特征", &tmp2, 线段方向);
                解引用(fake); /* 线段特征_新建已引用，平衡创建引用 */
                free(tmp2.数据);
                解引用(动态数组_获取(结果, 结果->长度 - 1));
                动态数组_设置(结果, 结果->长度 - 1, new_ft);
            }
            continue;
        }

        /* 情况2：方向不同 */
        if (结果->长度 == 0) {
            动态数组 tmp = { .数据 = malloc(sizeof(void*)), .长度 = 0, .容量 = 1 };
            if (!tmp.数据) { perror("线段特征_静态分析"); abort(); }
            动态数组_追加(&tmp, 当前虚线);
            线段特征* ft = 线段特征_新建("特征", &tmp, 线段方向);
            free(tmp.数据);
            动态数组_追加(结果, ft);
            continue;
        }

        线段特征* 之前 = 动态数组_获取(结果, 结果->长度 - 1);
        bool 需要合并 = false;
        相对方向 rel = 相对方向_分析(线段特征_高(之前), 线段特征_低(之前), 虚线_高(当前虚线), 虚线_低(当前虚线));
        if (严格包含) {
            需要合并 = (rel == 相对方向_顺 || rel == 相对方向_逆 || rel == 相对方向_同);
        } else {
            需要合并 = (rel == 相对方向_顺 || rel == 相对方向_同);
        }
        if (需要合并) {
            线段特征_添加(之前, 当前虚线);
        } else {
            动态数组 tmp = { .数据 = malloc(sizeof(void*)), .长度 = 0, .容量 = 1 };
            if (!tmp.数据) { perror("线段特征_静态分析"); abort(); }
            动态数组_追加(&tmp, 当前虚线);
            线段特征* ft = 线段特征_新建("特征", &tmp, 线段方向);
            free(tmp.数据);
            动态数组_追加(结果, ft);
        }
    }
}

void 线段特征_获取分型序列(动态数组* 特征序列, 动态数组* 结果) {
    if (特征序列->长度 < 3) return;
    for (size_t i = 1; i < 特征序列->长度 - 1; i++) {
        线段特征* 左 = 动态数组_获取(特征序列, i - 1);
        线段特征* 中 = 动态数组_获取(特征序列, i);
        线段特征* 右 = 动态数组_获取(特征序列, i + 1);
        缺口 fl = { .高 = 线段特征_高(左), .低 = 线段特征_低(左) };
        缺口 fm = { .高 = 线段特征_高(中), .低 = 线段特征_低(中) };
        缺口 fr = { .高 = 线段特征_高(右), .低 = 线段特征_低(右) };
        分型结构 s = 分型结构_分析(&fl, &fm, &fr, true, true);
        特征分型* tf = 特征分型_新建(左, 中, 右, s);
        动态数组_追加(结果, tf);
    }
}

void 释放线段特征(线段特征* obj) {
    动态数组_释放(&obj->元素, true);
}

/* ================================================================
 * 特征分型
 * ================================================================ */

特征分型* 特征分型_新建(线段特征* 左, 线段特征* 中, 线段特征* 右, 分型结构 结构) {
    特征分型* t = 分配(sizeof(特征分型), CHAN_TYPE_特征分型);
    t->左 = 左; t->中 = 中; t->右 = 右; t->结构 = 结构;
    return t;
}

void 释放特征分型(特征分型* obj) {
    (void)obj;
}

/* ================================================================
 * 线段 — 所有递归转为 while 循环 + 显式深度计数器
 * ================================================================ */

/* ---------- 内部助手 ---------- */

bool 线段_添加虚线(虚线* 段, 虚线* 筆) {
    if (段->基础序列.长度 > 0) {
        虚线* 末 = 动态数组_获取(&段->基础序列, 段->基础序列.长度 - 1);
        if (!分型_判断分型(末->武, 筆->文, "中")) return false;
        if (strcmp(末->标识, 筆->标识) != 0) return false;
    }
    虚线指针列表_追加(&段->基础序列, 筆);
    return true;
}

void 线段_武斗(虚线* 段, 分型* 武, int 行号) {
    (void)行号;
    if (段->武->分型特征值 == 武->分型特征值) { 段->武 = 武; goto 更新高低; }
    assert(段->文->结构 != 武->结构);
    if (虚线_方向(段) == 相对方向_向上) {
        if (武->分型特征值 < 段->文->分型特征值) {
            fprintf(stderr, "线段_武斗: 向上段, 终点<起点\n");
            return;
        }
    } else {
        if (武->分型特征值 > 段->文->分型特征值) {
            fprintf(stderr, "线段_武斗: 向下段, 终点>起点\n");
            return;
        }
    }
    段->武 = 武;
更新高低:
    if (虚线_方向(段) == 相对方向_向上) {
        段->高 = 武->中->高;
    } else {
        段->低 = 武->中->低;
    }
}

bool 线段_特征分型终结(虚线* 段) {
    动态数组 特征序列;
    动态数组_初始化(&特征序列, 8);
    线段特征_静态分析(&段->基础序列, 虚线_方向(段), 线段_四象(段), false, &特征序列);
    bool result = false;
    if (特征序列.长度 >= 3) {
        线段特征* 左 = 动态数组_获取(&特征序列, 特征序列.长度 - 3);
        线段特征* 中 = 动态数组_获取(&特征序列, 特征序列.长度 - 2);
        线段特征* 右 = 动态数组_获取(&特征序列, 特征序列.长度 - 1);
        /* 用缺口进行结构分析 */
        缺口 fl = { .高 = 线段特征_高(左), .低 = 线段特征_低(左) };
        缺口 fm = { .高 = 线段特征_高(中), .低 = 线段特征_低(中) };
        缺口 fr = { .高 = 线段特征_高(右), .低 = 线段特征_低(右) };
        分型结构 s = 分型结构_分析(&fl, &fm, &fr, true, true);
        if (虚线_方向(段) == 相对方向_向上 && s == 分型结构_顶) result = true;
        if (虚线_方向(段) == 相对方向_向下 && s == 分型结构_底) result = true;
    }
    /* 释放特征序列中的元素 */
    for (size_t i = 0; i < 特征序列.长度; i++) 解引用(动态数组_获取(&特征序列, i));
    free(特征序列.数据);
    return result;
}

void 线段_特征序列状态(虚线* 段, bool* 左, bool* 中, bool* 右) {
    *左 = 动态数组_获取(&段->特征序列, 0) != NULL;
    *中 = 动态数组_获取(&段->特征序列, 1) != NULL;
    *右 = 动态数组_获取(&段->特征序列, 2) != NULL;
}

缺口* 线段_获取缺口(虚线* 段) {
    if (strcmp(段->模式, "文武") != 0) return NULL;
    线段特征* 左 = 动态数组_获取(&段->特征序列, 0);
    线段特征* 中 = 动态数组_获取(&段->特征序列, 1);
    if (!左 || !中) return NULL;
    相对方向 rel = 相对方向_分析(线段特征_高(左), 线段特征_低(左), 线段特征_高(中), 线段特征_低(中));
    if (相对方向_是否缺口(rel)) {
        double hh = fmax(线段特征_文(左)->中->分型特征值, 线段特征_文(中)->中->分型特征值);
        double ll = fmin(线段特征_文(左)->中->分型特征值, 线段特征_文(中)->中->分型特征值);
        return 缺口_新建(hh, ll);
    }
    return NULL;
}

const char* 线段_四象(虚线* 段) {
    if (段->前一缺口) return (虚线_方向(段) == 相对方向_向上) ? "老阳" : "老阴";
    return (虚线_方向(段) == 相对方向_向上) ? "小阳" : "少阴";
}

void 线段_设置特征序列(虚线* 段, 线段特征** 序列, int 行号) {
    (void)行号;
    if (strcmp(段->模式, "文武") != 0) return;
    for (int i = 0; i < 3; i++) {
        解引用(动态数组_获取(&段->特征序列, i));
        动态数组_设置(&段->特征序列, i, 序列[i]);
        if (序列[i]) 引用(序列[i]);
    }
    if (序列[2]) {
        if (动态数组_查找(&段->基础序列, 序列[2]->元素.数据[序列[2]->元素.长度 - 1]) == (size_t)-1) {
            fprintf(stderr, "线段_设置特征序列: 右[-1]不在基础序列\n");
            return;
        }
        /* 截断基础序列到右[-1] */
        虚线* last = 序列[2]->元素.数据[序列[2]->元素.长度 - 1];
        动态数组 new_base;
        动态数组_初始化(&new_base, 4);
        for (size_t i_ = 0; i_ < 段->基础序列.长度; i_++) {
            虚线* e = 动态数组_获取(&段->基础序列, i_);
            动态数组_追加(&new_base, e);
            if (e == last) break;
        }
        if (new_base.长度 >= 6 && new_base.长度 % 2 == 0) {
            for (size_t i_ = 0; i_ < new_base.长度; i_++)
                引用(动态数组_获取(&new_base, i_));
            for (size_t i_ = 0; i_ < 段->基础序列.长度; i_++)
                解引用(动态数组_获取(&段->基础序列, i_));
            动态数组_释放(&段->基础序列, false);
            段->基础序列 = new_base;
        } else {
            动态数组_释放(&new_base, false);
            解引用(动态数组_获取(&段->特征序列, 2));
            动态数组_设置(&段->特征序列, 2, NULL);
        }
    }
}

void 线段_刷新特征序列(虚线* 段, 缠论配置* 配置) {
    if (strcmp(段->模式, "文武") != 0) return;

    /* 构建用于分析的基础序列（考虑前一结束位置） */
    动态数组 分析用序列;
    动态数组_初始化(&分析用序列, 段->基础序列.长度);
    size_t start = 0;
    if (段->前一结束位置) {
        size_t idx = 动态数组_查找(&段->基础序列, 段->前一结束位置);
        if (idx != (size_t)-1 && idx > 0) start = idx - 1;
    }
    for (size_t i = start; i < 段->基础序列.长度; i++)
        动态数组_追加(&分析用序列, 动态数组_获取(&段->基础序列, i));

    动态数组 特征序列结果;
    动态数组_初始化(&特征序列结果, 8);
    线段特征_静态分析(&分析用序列, 虚线_方向(段), 线段_四象(段),
                      配置->线段_特征序列忽视老阴老阳, &特征序列结果);
    线段特征* seq[3] = {NULL, NULL, NULL};
    if (特征序列结果.长度 >= 3) {
        动态数组 分型序列;
        动态数组_初始化(&分型序列, 4);
        线段特征_获取分型序列(&特征序列结果, &分型序列);
        if (分型序列.长度 > 0) {
            特征分型* last_tf = 动态数组_获取(&分型序列, 分型序列.长度 - 1);
            if ((虚线_方向(段) == 相对方向_向上 && last_tf->结构 == 分型结构_顶) ||
                (虚线_方向(段) == 相对方向_向下 && last_tf->结构 == 分型结构_底)) {
                /* Only set seq[2] if the resulting truncation would be valid (>=6, even) */
                虚线* last_stroke = last_tf->右->元素.数据[last_tf->右->元素.长度 - 1];
                size_t last_idx = 动态数组_查找(&段->基础序列, last_stroke);
                if (last_idx != (size_t)-1 && (last_idx + 1) >= 6 && (last_idx + 1) % 2 == 0) {
                    seq[0] = last_tf->左; seq[1] = last_tf->中; seq[2] = last_tf->右;
                } else {
                    seq[0] = 动态数组_获取(&特征序列结果, 特征序列结果.长度 - 2);
                    seq[1] = 动态数组_获取(&特征序列结果, 特征序列结果.长度 - 1);
                    seq[2] = NULL;
                }
            } else {
                seq[0] = 动态数组_获取(&特征序列结果, 特征序列结果.长度 - 2);
                seq[1] = 动态数组_获取(&特征序列结果, 特征序列结果.长度 - 1);
                seq[2] = NULL;
            }
        }
        动态数组_释放(&分型序列, true);
    } else {
        for (size_t i = 0; i < 特征序列结果.长度 && i < 3; i++)
            seq[i] = 动态数组_获取(&特征序列结果, i);
    }

    线段_设置特征序列(段, seq, __LINE__);

    动态数组_释放(&分析用序列, false);
    动态数组_释放(&特征序列结果, true);
}

void 线段_分割序列(虚线* 段, 中枢* 所属中枢,
                   动态数组* 前, 动态数组* 后, 动态数组* 第三买卖线, 虚线** 贯穿伤) {
    *贯穿伤 = NULL;
    前->长度 = 0; 后->长度 = 0; 第三买卖线->长度 = 0;

    if (strcmp(段->模式, "文武") != 0) {
        for (size_t i = 0; i < 段->基础序列.长度; i++)
            动态数组_追加(前, 动态数组_获取(&段->基础序列, i));
        return;
    }
    assert(段->基础序列.长度 > 0);

    bool 在之后 = false;
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        虚线* b = 动态数组_获取(&段->基础序列, i);
        if (!在之后 && (i == 0 || ((虚线*)动态数组_获取(&段->基础序列, i-1))->武 != 段->武)) {
            动态数组_追加(前, b);
        }
        if (在之后) 动态数组_追加(后, b);
        if (b->文 == 段->武) { 动态数组_追加(后, b); 在之后 = true; }
    }

    const char* 状态 = NULL;
    if (所属中枢) {
        所属中枢->本级_第三买卖线 = NULL;
        分型* 尾部 = 段->武;
        if (后->长度 > 0) {
            虚线* last_b = 动态数组_获取(后, 后->长度 - 1);
            尾部 = last_b->武;
        }
        if (中枢_高(所属中枢) >= 尾部->分型特征值 && 尾部->分型特征值 >= 中枢_低(所属中枢))
            状态 = "中枢之中";
        else if (中枢_高(所属中枢) < 尾部->分型特征值)
            状态 = "中枢之上";
        else if (中枢_低(所属中枢) > 尾部->分型特征值)
            状态 = "中枢之下";
    }

    if (状态 && strcmp(状态, "中枢之上") == 0) {
        for (size_t i_ = 段->基础序列.长度; i_ > 0; i_--) {
            size_t i = i_ - 1;
            虚线* b = 动态数组_获取(&段->基础序列, i);
            if (虚线_方向(b) == 相对方向_向下) {
                相对方向 rel = 相对方向_分析(中枢_高(所属中枢), 中枢_低(所属中枢), b->高, b->低);
                if (rel == 相对方向_向上缺口) 动态数组_追加(第三买卖线, b);
                else break;
            }
        }
    }
    if (状态 && strcmp(状态, "中枢之下") == 0) {
        for (size_t i_ = 段->基础序列.长度; i_ > 0; i_--) {
            size_t i = i_ - 1;
            虚线* b = 动态数组_获取(&段->基础序列, i);
            if (虚线_方向(b) == 相对方向_向上) {
                相对方向 rel = 相对方向_分析(中枢_高(所属中枢), 中枢_低(所属中枢), b->高, b->低);
                if (rel == 相对方向_向下缺口) 动态数组_追加(第三买卖线, b);
                else break;
            }
        }
    }

    if (第三买卖线->长度 > 0 && 所属中枢) {
        /* reverse */
        for (size_t i = 0; i < 第三买卖线->长度 / 2; i++) {
            void* tmp = 第三买卖线->数据[i];
            第三买卖线->数据[i] = 第三买卖线->数据[第三买卖线->长度 - 1 - i];
            第三买卖线->数据[第三买卖线->长度 - 1 - i] = tmp;
        }
        所属中枢->本级_第三买卖线 = 动态数组_获取(第三买卖线, 0);
    }

    if (后->长度 > 0) {
        虚线* first_in_后 = 动态数组_获取(后, 0);
        if (虚线_方向(段) == 相对方向_向上) {
            if (first_in_后->武->分型特征值 < 段->文->分型特征值) *贯穿伤 = first_in_后;
        } else {
            if (first_in_后->武->分型特征值 > 段->文->分型特征值) *贯穿伤 = first_in_后;
        }
    }
}

void 线段_刷新(虚线* 段, 缠论配置* 配置) {
    if (strcmp(段->模式, "文武") != 0) return;
    if (段->基础序列.长度 == 0) { fprintf(stderr, "线段_刷新: 基础序列为空\n"); return; }

    线段_刷新特征序列(段, 配置);

    /* 统计有效特征序列 */
    线段特征** eff = malloc(3 * sizeof(线段特征*));
    if (!eff) { perror("线段_武斗"); abort(); }
    int effn = 0;
    for (int i = 0; i < 3; i++) {
        线段特征* ft = 动态数组_获取(&段->特征序列, i);
        if (ft) eff[effn++] = ft;
    }

    if (effn == 3) {
        线段_武斗(段, 线段特征_文((线段特征*)动态数组_获取(&段->特征序列, 1)), __LINE__);
    } else if (effn >= 1) {
        线段特征* 最近 = eff[effn - 1];
        虚线* last_in_ft = 最近->元素.数据[最近->元素.长度 - 1];
        虚线* 特征后一笔 = NULL;
        if (动态数组_查找(&段->基础序列, last_in_ft) != (size_t)-1) {
            特征后一笔 = last_in_ft;
        } else {
            特征后一笔 = 笔_以武会友(&段->基础序列, last_in_ft->武);
        }
        if (特征后一笔) {
            size_t idx = 动态数组_查找(&段->基础序列, 特征后一笔);
            if (idx != (size_t)-1 && idx < 段->基础序列.长度 - 1) {
                虚线* 下一笔 = 动态数组_获取(&段->基础序列, idx + 1);
                if ((虚线_方向(段) == 相对方向_向上 && 虚线_高(段) <= 下一笔->高) ||
                    (虚线_方向(段) == 相对方向_向下 && 虚线_低(段) >= 下一笔->低)) {
                    线段_武斗(段, 下一笔->武, __LINE__);
                }
            }
        }
    }
    free(eff);

    线段_获取内部中枢序列(段, 配置, NULL, NULL, NULL);
}

void 线段_序列重置(虚线* 段, 动态数组* 序列) {
    动态数组 new_base;
    动态数组_初始化(&new_base, 4);
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        虚线* e = 动态数组_获取(&段->基础序列, i);
        if (动态数组_查找(序列, e) == (size_t)-1) {
            bool found = false;
            for (size_t j = 0; j < 序列->长度; j++) {
                虚线* s = 动态数组_获取(序列, j);
                if (s->序号 == e->序号 && s->有效性) { e = s; found = true; break; }
            }
            if (!found) break;
        }
        if (new_base.长度 > 0 && !虚线_之后是(动态数组_获取(&new_base, new_base.长度 - 1), e)) {
            break;
        }
        动态数组_追加(&new_base, e);
    }
    /* 引用 new_base 元素，再解引用旧基础序列元素，防止引用计数归零 */
    for (size_t i = 0; i < new_base.长度; i++)
        引用(动态数组_获取(&new_base, i));
    for (size_t i = 0; i < 段->基础序列.长度; i++)
        解引用(动态数组_获取(&段->基础序列, i));
    动态数组_释放(&段->基础序列, false);
    段->基础序列 = new_base;
    解引用(动态数组_获取(&段->特征序列, 2));
    动态数组_设置(&段->特征序列, 2, NULL);
}

虚线* 线段_查找贯穿伤(虚线* 段) {
    for (size_t i = 3; i < 段->基础序列.长度; i++) {
        虚线* b = 动态数组_获取(&段->基础序列, i);
        if (虚线_方向(段) == 相对方向_向上) {
            if (b->武->分型特征值 < 段->文->分型特征值) return b;
        } else {
            if (b->武->分型特征值 > 段->文->分型特征值) return b;
        }
    }
    return NULL;
}

void 线段_获取内部中枢序列(虚线* 段, 缠论配置* 配置,
                          动态数组* 虚_out, 动态数组* 实_out, 动态数组* 合_out) {
    if (strcmp(段->模式, "文武") != 0) return;
    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4); 动态数组_初始化(&后, 4); 动态数组_初始化(&第三, 4);
    虚线* 贯穿伤 = NULL;
    线段_分割序列(段, NULL, &前, &后, &第三, &贯穿伤);

    char buf[128];
    snprintf(buf, 127, "%s_%d_实_", 段->标识, 段->序号);
    中枢_分析(&前, &段->实_中枢序列, false, buf);
    snprintf(buf, 127, "%s_%d_虚_", 段->标识, 段->序号);
    中枢_分析(&后, &段->虚_中枢序列, false, buf);
    snprintf(buf, 127, "%s_%d_合_", 段->标识, 段->序号);
    中枢_分析(&段->基础序列, &段->合_中枢序列, false, buf);

    if (虚_out) *虚_out = 段->虚_中枢序列;
    if (实_out) *实_out = 段->实_中枢序列;
    if (合_out) *合_out = 段->合_中枢序列;

    动态数组_释放(&前, false);
    动态数组_释放(&后, false);
    动态数组_释放(&第三, false);
}

bool 线段_基础判断(虚线* 左, 虚线* 中, 虚线* 右, 相对方向* 关系序列, size_t 关系数) {
    if (!虚线_之后是(左, 中)) return false;
    if (!虚线_之后是(中, 右)) return false;
    if (!相对方向_是否包含(相对方向_分析(左->高, 左->低, 中->高, 中->低))) return false;
    if (!相对方向_是否包含(相对方向_分析(中->高, 中->低, 右->高, 右->低))) return false;
    相对方向 rel = 相对方向_分析(左->高, 左->低, 右->高, 右->低);
    bool found = false;
    for (size_t i = 0; i < 关系数; i++) if (rel == 关系序列[i]) { found = true; break; }
    if (!found) return false;
    if (虚线_方向(左) == 相对方向_向下 && !相对方向_是否向下(rel)) return false;
    if (虚线_方向(左) == 相对方向_向上 && !相对方向_是否向上(rel)) return false;
    return true;
}

/* ---------- 线段._添加线段 ---------- */
static void 线段_添加线段(动态数组* 线段序列, 虚线* 待添加, 缠论配置* 配置) {
    if (线段序列->长度 > 0) {
        虚线* prev = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        if (!虚线_之后是(prev, 待添加)) {
            fprintf(stderr, "线段_添加线段: 不连续\n");
            解引用(待添加);
            return;
        }
    }
    待添加->模式[0] = '\0'; strcpy(待添加->模式, "文武");

    if (线段序列->长度 == 0) { 动态数组_追加(线段序列, 待添加); 引用(待添加); return; }

    虚线* prev = 动态数组_获取(线段序列, 线段序列->长度 - 1);

    if (!动态数组_获取(&prev->特征序列, 2) && !prev->短路修正)
        assert(动态数组_获取(&prev->特征序列, 2) != NULL);

    待添加->序号 = prev->序号 + 1;
    解引用(待添加->前一缺口);
    待添加->前一缺口 = 线段_获取缺口(prev);
    待添加->前一结束位置 = prev->基础序列.数据[prev->基础序列.长度 - 1];

    if (strcmp(线段_四象(prev), "老阴") == 0 || strcmp(线段_四象(prev), "老阳") == 0) {
        解引用(待添加->前一缺口);
        待添加->前一缺口 = NULL;
    }

    动态数组_追加(线段序列, 待添加);
    引用(待添加);
}

/* ---------- 线段._弹出线段 ---------- */
static 虚线* 线段_弹出线段(动态数组* 线段序列, 虚线* 待弹出, 缠论配置* 配置) {
    (void)配置;
    if (线段序列->长度 == 0) return NULL;
    if (动态数组_获取(线段序列, 线段序列->长度 - 1) != 待弹出) {
        fprintf(stderr, "线段_弹出线段: 不在列表中\n");
        return NULL;
    }
    虚线* 弹出 = 动态数组_弹出(线段序列);
    入回收站(弹出);
    弹出->有效性 = false;
    弹出->前一结束位置 = NULL;
    解引用(弹出);
    return 弹出;
}

/* ---------- 四种修正 ---------- */

static bool 线段_缺口突破(动态数组* 线段序列, 缠论配置* 配置) {
    虚线* cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    if (cur->基础序列.长度 == 0) return false;
    虚线* last_stroke = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
    const char* sx = 线段_四象(cur);
    bool 同向 = (虚线_方向(last_stroke) == 虚线_方向(cur));
    if (同向) return false;
    if (strcmp(sx, "老阳") != 0 && strcmp(sx, "老阴") != 0) return false;
    if (动态数组_获取(&cur->特征序列, 2) != NULL) return false;
    if (!((strcmp(sx, "老阳") == 0 && last_stroke->低 < 虚线_低(cur)) ||
          (strcmp(sx, "老阴") == 0 && last_stroke->高 > 虚线_高(cur)))) return false;

    /* 执行修正 */
    动态数组 saved;
    动态数组_初始化(&saved, cur->基础序列.长度);
    for (size_t i = 0; i < cur->基础序列.长度; i++)
        动态数组_追加(&saved, cur->基础序列.数据[i]);
    线段_弹出线段(线段序列, cur, 配置);
    虚线* cur2 = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    assert(动态数组_获取(&cur2->特征序列, 2) != NULL);
    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4); 动态数组_初始化(&后, 4); 动态数组_初始化(&第三, 4);
    虚线* gs = NULL;
    线段_分割序列(cur2, NULL, &前, &后, &第三, &gs);
    for (size_t i = 0; i < saved.长度; i++) 动态数组_追加(&前, saved.数据[i]);
    for (size_t i = 0; i < 前.长度; i++)
        引用(动态数组_获取(&前, i));
    for (size_t i = 0; i < cur2->基础序列.长度; i++)
        解引用(动态数组_获取(&cur2->基础序列, i));
    动态数组_释放(&cur2->基础序列, false);
    cur2->基础序列 = 前;
    线段_刷新(cur2, 配置);
    动态数组_释放(&saved, false);
    动态数组_释放(&后, false);
    动态数组_释放(&第三, false);
    return true;
}

static bool 线段_非缺口下穿刺(动态数组* 线段序列, 缠论配置* 配置) {
    虚线* cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    const char* sx = 线段_四象(cur);
    if (!(配置->线段_非缺口下穿刺 &&
          (strcmp(sx, "小阳") == 0 || strcmp(sx, "少阴") == 0) &&
          动态数组_获取(&cur->特征序列, 2) == NULL)) return false;

    虚线* gs = 线段_查找贯穿伤(cur);
    if (!gs) return false;

    size_t idx = 动态数组_查找(&cur->基础序列, gs);
    if (idx == (size_t)-1) return false;
    size_t base_len = cur->基础序列.长度 - idx;
    if (!(base_len == 4 && 线段序列->长度 >= 2)) return false;

    虚线* 左 = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 3);
    虚线* 右 = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
    if (相对方向_分析(左->高, 左->低, 右->高, 右->低) != 虚线_方向(cur))
        return false;

    /* 执行修正 */
    fprintf(stderr, "[警告] 线段_非缺口下穿刺\n");
    动态数组 saved;
    动态数组_初始化(&saved, cur->基础序列.长度);
    for (size_t i = 0; i < cur->基础序列.长度; i++)
        动态数组_追加(&saved, cur->基础序列.数据[i]);
    线段_弹出线段(线段序列, cur, 配置);
    虚线* cur2 = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    解引用(动态数组_获取(&cur2->特征序列, 2));
    动态数组_设置(&cur2->特征序列, 2, NULL);
    size_t start_idx = 动态数组_查找(&saved, cur2->基础序列.数据[cur2->基础序列.长度 - 1]);
    for (size_t i = start_idx + 1; i < saved.长度; i++)
        线段_添加虚线(cur2, saved.数据[i]);
    线段_刷新(cur2, 配置);

    if (动态数组_获取(&cur2->特征序列, 2)) {
        动态数组 new_seg;
        动态数组_初始化(&new_seg, 3);
        动态数组_追加(&new_seg, saved.数据[saved.长度 - 3]);
        动态数组_追加(&new_seg, saved.数据[saved.长度 - 2]);
        动态数组_追加(&new_seg, saved.数据[saved.长度 - 1]);
        虚线* seg = 虚线_创建线段(&new_seg);
        线段_添加线段(线段序列, seg, 配置);
        /* seg->特征序列 already initialized with 3 NULLs by 虚线_创建线段 */
        动态数组 tmp = { .数据 = malloc(sizeof(void*)), .长度 = 0, .容量 = 1 };
        if (!tmp.数据) { perror("线段_非缺口下穿刺"); abort(); }
        动态数组_追加(&tmp, saved.数据[new_seg.长度 - 2]);
        线段特征* ft = 线段特征_新建("特征", &tmp, 虚线_方向(seg));
        free(tmp.数据);
        解引用(动态数组_获取(&seg->特征序列, 0));
        动态数组_设置(&seg->特征序列, 0, ft);
        动态数组_释放(&new_seg, false);
    }
    动态数组_释放(&saved, false);
    return true;
}

static bool 线段_缺口后紧急修正(动态数组* 线段序列, 缠论配置* 配置) {
    虚线* cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    const char* sx = 线段_四象(cur);
    if (!(配置->线段_缺口后紧急修正 && !配置->线段_特征序列忽视老阴老阳 &&
          (strcmp(sx, "小阳") == 0 || strcmp(sx, "少阴") == 0) &&
          动态数组_获取(&cur->特征序列, 2) == NULL)) return false;
    if (!(线段序列->长度 >= 2 &&
          (strcmp(线段_四象(动态数组_获取(线段序列, 线段序列->长度 - 2)), "老阴") == 0 ||
           strcmp(线段_四象(动态数组_获取(线段序列, 线段序列->长度 - 2)), "老阳") == 0))) return false;

    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4); 动态数组_初始化(&后, 4); 动态数组_初始化(&第三, 4);
    虚线* gs = NULL;
    线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
    if (后.长度 < 3) { 动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false); return false; }

    bool 需要修正 = false;
    if (虚线_方向(cur) == 相对方向_向上) {
        if (相对方向_分析(((虚线*)后.数据[0])->高, ((虚线*)后.数据[0])->低,
                      ((虚线*)后.数据[2])->高, ((虚线*)后.数据[2])->低) == 相对方向_向下)
            需要修正 = true;
    } else {
        if (相对方向_分析(((虚线*)后.数据[0])->高, ((虚线*)后.数据[0])->低,
                      ((虚线*)后.数据[2])->高, ((虚线*)后.数据[2])->低) == 相对方向_向上)
            需要修正 = true;
    }
    if (!需要修正) { 动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false); return false; }

    cur->短路修正 = true;
    虚线* seg = 虚线_创建线段(&后);
    线段_添加线段(线段序列, seg, 配置);
    动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false);
    return true;
}

static bool 线段_修正(动态数组* 线段序列, 缠论配置* 配置) {
    虚线* cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    if (!(配置->线段_修正 && cur->基础序列.长度 >= 9)) return false;

    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4); 动态数组_初始化(&后, 4); 动态数组_初始化(&第三, 4);
    虚线* gs = NULL;
    线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
    if (后.长度 < 6) { 动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false); return false; }

    虚线* a = 动态数组_获取(&后, 后.长度 - 3);
    虚线* b = 动态数组_获取(&后, 后.长度 - 1);
    if (虚线_方向(cur) != 相对方向_分析(a->高, a->低, b->高, b->低)) {
        动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false);
        return false;
    }

    cur->短路修正 = true;
    /* 创建第一个新段 */
    动态数组 seg1_base;
    动态数组_初始化(&seg1_base, 后.长度 - 3);
    for (size_t i = 0; i < 后.长度 - 3; i++) 动态数组_追加(&seg1_base, 后.数据[i]);
    虚线* seg1 = 虚线_创建线段(&seg1_base);
    seg1->短路修正 = true;
    线段_添加线段(线段序列, seg1, 配置);
    if (strcmp(线段_四象(cur), "老阴") == 0 || strcmp(线段_四象(cur), "老阳") == 0) {
        解引用(seg1->前一缺口); seg1->前一缺口 = NULL;
    }
    动态数组_释放(&seg1_base, false);

    /* 创建第二个新段 */
    动态数组 seg2_base;
    动态数组_初始化(&seg2_base, 3);
    for (size_t i = 后.长度 - 3; i < 后.长度; i++) 动态数组_追加(&seg2_base, 后.数据[i]);
    虚线* seg2 = 虚线_创建线段(&seg2_base);
    线段_添加线段(线段序列, seg2, 配置);
    动态数组_释放(&seg2_base, false);

    动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false);
    return true;
}

/* ================================================================
 * 线段_分析 — 将原 Python 的尾递归转为 while 循环
 * ================================================================ */

void 线段_分析(动态数组* 笔序列, 动态数组* 线段序列, 缠论配置* 配置) {
    相对方向 关系序列[] = {相对方向_向上, 相对方向_向下};
    int 深度 = 0;

    while (深度 < 512) {
        /* ---- 1. 初始化第一个线段 ---- */
        if (线段序列->长度 == 0) {
            for (size_t i = 1; i + 1 < 笔序列->长度; i++) {
                虚线* 左 = 动态数组_获取(笔序列, i - 1);
                虚线* 中 = 动态数组_获取(笔序列, i);
                虚线* 右 = 动态数组_获取(笔序列, i + 1);
                if (!线段_基础判断(左, 中, 右, 关系序列, 2)) continue;
                动态数组 segs;
                动态数组_初始化(&segs, 3);
                动态数组_追加(&segs, 左); 动态数组_追加(&segs, 中); 动态数组_追加(&segs, 右);
                虚线* seg = 虚线_创建线段(&segs);
                线段_添加线段(线段序列, seg, 配置);
                /* seg->特征序列 already initialized with 3 NULLs by 虚线_创建线段 */
                动态数组 tmp = { .数据 = malloc(sizeof(void*)), .长度 = 0, .容量 = 1 };
                if (!tmp.数据) { perror("线段_扩展分析"); abort(); }
                动态数组_追加(&tmp, 中);
                线段特征* ft = 线段特征_新建("特征", &tmp, 虚线_方向(seg));
                free(tmp.数据);
                解引用(动态数组_获取(&seg->特征序列, 0));
                动态数组_设置(&seg->特征序列, 0, ft);
                动态数组_释放(&segs, false);
                break;
            }
            if (线段序列->长度 == 0) return;
        }

        /* ---- 2. 清理无效尾部 ---- */
        while (线段序列->长度 > 0) {
            虚线* last = 动态数组_获取(线段序列, 线段序列->长度 - 1);
            if (last->前一结束位置 && 动态数组_查找(笔序列, last->前一结束位置) == (size_t)-1)
                线段_弹出线段(线段序列, last, 配置);
            else break;
        }
        if (线段序列->长度 == 0) { 深度++; continue; }

        /* ---- 3. 确保当前线段有效 ---- */
        虚线* cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        线段_序列重置(cur, 笔序列);
        if (cur->基础序列.长度 < 3) {
            线段_弹出线段(线段序列, cur, 配置);
            if (线段序列->长度 == 0) { 深度++; continue; }
        }

        cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);

        /* ---- 4. 特征序列完整时创建新段 ---- */
        step4:
        if (动态数组_获取(&cur->特征序列, 2) != NULL) {
            动态数组 前, 后, 第三;
            动态数组_初始化(&前, 4); 动态数组_初始化(&后, 4); 动态数组_初始化(&第三, 4);
            虚线* gs = NULL;
            线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
            虚线* seg = 虚线_创建线段(&后);
            线段创建于step4++;
            线段_添加线段(线段序列, seg, 配置);
            const char* sx_cur = 线段_四象(cur);
            if (strcmp(sx_cur, "老阴") == 0 || strcmp(sx_cur, "老阳") == 0) {
                解引用(seg->前一缺口); seg->前一缺口 = NULL;
            }
            动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false);
        }

        cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        线段_刷新(cur, 配置);

        /* After refresh,笔 values may have been updated by the latest K-line.
           Re-check seq[2] — if now set, jump back to step 4 to split. */
        if (动态数组_获取(&cur->特征序列, 2) != NULL) {
            goto step4;
        }

        /* ---- 5. 调用四种修正（各一次） ---- */
        线段_缺口突破(线段序列, 配置);
        线段_非缺口下穿刺(线段序列, 配置);
        线段_缺口后紧急修正(线段序列, 配置);
        线段_修正(线段序列, 配置);

        /* ---- 6. 循环处理后续的笔 ---- */
        cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        if (cur->基础序列.长度 == 0) { fprintf(stderr, "线段_分析: 基础序列为空\n"); return; }
        虚线* last_stroke = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
        size_t si = 动态数组_查找(笔序列, last_stroke);
        if (si == (size_t)-1) {
            for (size_t j = 0; j < 笔序列->长度; j++) {
                虚线* s = 动态数组_获取(笔序列, j);
                if (s->序号 == last_stroke->序号) { si = j; break; }
            }
        }
        if (si == (size_t)-1) { fprintf(stderr, "线段_分析: 索引异常\n"); return; }
        size_t start = si + 1;

        for (size_t idx = start; idx < 笔序列->长度; idx++) {
            虚线* stroke = 动态数组_获取(笔序列, idx);
            cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
            const char* sx = 线段_四象(cur);

            if (!线段_添加虚线(cur, stroke)) break;
            线段_刷新(cur, 配置);

            if (线段_缺口突破(线段序列, 配置)) continue;
            if (线段_非缺口下穿刺(线段序列, 配置)) continue;
            if (线段_缺口后紧急修正(线段序列, 配置)) continue;
            if (线段_修正(线段序列, 配置)) {
                continue;
            }

            cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
            if (动态数组_获取(&cur->特征序列, 2) == NULL) continue;

            动态数组 前, 后, 第三;
            动态数组_初始化(&前, 4); 动态数组_初始化(&后, 4); 动态数组_初始化(&第三, 4);
            虚线* gs = NULL;
            线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
            虚线* seg = 虚线_创建线段(&后);
            线段创建于loop++;
            线段_添加线段(线段序列, seg, 配置);
            if (strcmp(sx, "老阴") == 0 || strcmp(sx, "老阳") == 0) {
                解引用(seg->前一缺口); seg->前一缺口 = NULL;
            }

            if (seg->基础序列.长度 > 0) {
                虚线* seg_last = 动态数组_获取(&seg->基础序列, seg->基础序列.长度 - 1);
                if (seg_last != stroke) {
                    if (!虚线_之后是(seg_last, stroke)) {
                        动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false);
                        深度++; goto restart;
                    }
                    线段_添加虚线(seg, stroke);
                }
            }
            线段_刷新(seg, 配置);
            动态数组_释放(&前, false); 动态数组_释放(&后, false); 动态数组_释放(&第三, false);
        }
        return;
restart:;
    }
    fprintf(stderr, "线段_分析: 深度超出 %d\n", 深度);
}

void 线段_武终(虚线* 段, int 行号) {
    if (strcmp(段->模式, "文武") != 0 && 段->基础序列.长度 > 0)
        线段_武斗(段, ((虚线*)动态数组_获取(&段->基础序列, 段->基础序列.长度 - 1))->武, 行号);
}

void 线段_验证序列(虚线* 段, 动态数组* 序列) {
    动态数组 new_base;
    动态数组_初始化(&new_base, 4);
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        虚线* e = 动态数组_获取(&段->基础序列, i);
        if (动态数组_查找(序列, e) == (size_t)-1) break;
        if (new_base.长度 > 0 && !虚线_之后是(动态数组_获取(&new_base, new_base.长度 - 1), e)) break;
        动态数组_追加(&new_base, e);
    }
    for (size_t i = 0; i < new_base.长度; i++)
        引用(动态数组_获取(&new_base, i));
    for (size_t i = 0; i < 段->基础序列.长度; i++)
        解引用(动态数组_获取(&段->基础序列, i));
    动态数组_释放(&段->基础序列, false);
    段->基础序列 = new_base;
    if (段->基础序列.长度 % 2 == 0 && 段->基础序列.长度 > 0) {
        解引用(动态数组_获取(&段->基础序列, 段->基础序列.长度 - 1));
        段->基础序列.长度--;
    }
}

/* ---------- 扩展线段 助手 ---------- */
static void 线段_添加扩展线段(动态数组* 线段序列, 虚线* seg) {
    seg->模式[0] = '\0'; strcpy(seg->模式, "高低");
    char buf[128];
    if (seg->基础序列.长度 > 0) {
        虚线* first = 动态数组_获取(&seg->基础序列, 0);
        if (strcmp(first->标识, "笔") != 0) snprintf(buf, 127, "扩展%s", seg->标识);
        else strcpy(buf, "扩展线段");
    } else strcpy(buf, "扩展线段");
    strncpy(seg->标识, buf, 63); seg->标识[63] = '\0';

    if (线段序列->长度 > 0) {
        虚线* prev = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        seg->序号 = prev->序号 + 1;
    }
    动态数组_追加(线段序列, seg);
    引用(seg);
}

/* ================================================================
 * 线段_扩展分析 — while 循环代替尾递归
 * ================================================================ */

void 线段_扩展分析(动态数组* 虚线序列, 动态数组* 线段序列, 缠论配置* 配置) {
    int 深度 = 0;
    while (深度 < 128) {
        if (虚线序列->长度 < 3) return;

        if (线段序列->长度 == 0) {
            for (size_t i = 1; i + 1 < 虚线序列->长度; i++) {
                虚线* 左 = 动态数组_获取(虚线序列, i - 1);
                虚线* 中 = 动态数组_获取(虚线序列, i);
                虚线* 右 = 动态数组_获取(虚线序列, i + 1);
                相对方向 rel = 相对方向_分析(左->高, 左->低, 右->高, 右->低);
                if (rel == 相对方向_向下 || rel == 相对方向_向上 ||
                    rel == 相对方向_顺 || rel == 相对方向_逆 || rel == 相对方向_同) {
                    动态数组 segs;
                    动态数组_初始化(&segs, 3);
                    动态数组_追加(&segs, 左); 动态数组_追加(&segs, 中); 动态数组_追加(&segs, 右);
                    虚线* seg = 虚线_创建线段(&segs);
                    线段_添加扩展线段(线段序列, seg);
                    动态数组_释放(&segs, false);
                    break;
                }
            }
            if (线段序列->长度 == 0) return;
        }

        虚线* cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        线段_验证序列(cur, 虚线序列);
        if (cur->基础序列.长度 < 3) {
            入回收站(cur);
            cur->有效性 = false;
            解引用(cur);
            线段序列->长度--;
            深度++; continue;
        }

        if (!配置->扩展线段_当下分析) {
            虚线* 左 = cur->基础序列.数据[0];
            虚线* 右 = cur->基础序列.数据[2];
            if (!相对方向_是否缺口(相对方向_分析(左->高, 左->低, 右->高, 右->低))) {
                cur->基础序列.长度 = 3;
                线段_武终(cur, __LINE__);
            } else {
                入回收站(cur);
                cur->有效性 = false;
                解引用(cur);
                线段序列->长度--;
                深度++; continue;
            }
        }

        线段_武终(cur, __LINE__);
        虚线* last = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
        if ((size_t)last->序号 + 3 > ((虚线*)动态数组_获取(虚线序列, 虚线序列->长度 - 1))->序号) return;

        size_t idx = 动态数组_查找(虚线序列, last);
        if (idx == (size_t)-1 || idx + 1 >= 虚线序列->长度) return;

        for (size_t i = idx + 2; i + 1 < 虚线序列->长度; i++) {
            虚线* 左 = 动态数组_获取(虚线序列, i - 1);
            虚线* 中 = 动态数组_获取(虚线序列, i);
            虚线* 右 = 动态数组_获取(虚线序列, i + 1);
            相对方向 rel = 相对方向_分析(左->高, 左->低, 右->高, 右->低);
            if (相对方向_是否缺口(rel)) {
                线段_添加虚线(cur, 左);
                线段_添加虚线(cur, 中);
                线段_武终(cur, __LINE__);
                continue;
            }
            if (动态数组_查找(&cur->基础序列, 左) != (size_t)-1) continue;
            动态数组 segs;
            动态数组_初始化(&segs, 3);
            动态数组_追加(&segs, 左); 动态数组_追加(&segs, 中); 动态数组_追加(&segs, 右);
            虚线* seg = 虚线_创建线段(&segs);
            线段创建于扩展++;
            线段_添加扩展线段(线段序列, seg);
            动态数组_释放(&segs, false);
            深度++; goto restart;
        }
        return;
restart:;
    }
}

/* ================================================================
 * 中枢
 * ================================================================ */

中枢* 中枢_新建(int 序号, const char* 标识, int 级别, 动态数组* 基础序列) {
    中枢* z = 分配(sizeof(中枢), CHAN_TYPE_中枢);
    z->序号 = 序号;
    strncpy(z->标识, 标识, 127); z->标识[127] = '\0';
    z->级别 = 级别;
    动态数组_初始化(&z->元素, 4);
    for (size_t i = 0; i < 基础序列->长度 && i < 3; i++)
        动态数组_追加(&z->元素, 动态数组_获取(基础序列, i));
    z->第三买卖线 = NULL;
    z->本级_第三买卖线 = NULL;
    return z;
}

中枢* 中枢_创建(虚线* 左, 虚线* 中, 虚线* 右, int 级别, const char* 标识) {
    assert(中枢_基础检查(左, 中, 右));
    动态数组 tmp = { .数据 = (void*[]){左, 中, 右}, .长度 = 3, .容量 = 3 };
    char buf[128];
    snprintf(buf, 127, "%s中枢<%s>", 标识, 中->标识);
    return 中枢_新建(0, buf, 级别, &tmp);
}

中枢* 中枢_从序列中获取中枢(动态数组* 虚线序列, 相对方向 起始方向, const char* 标识) {
    if (虚线序列->长度 < 3) return NULL;
    for (size_t i = 1; i + 1 < 虚线序列->长度; i++) {
        虚线* 左 = 动态数组_获取(虚线序列, i - 1);
        虚线* 中 = 动态数组_获取(虚线序列, i);
        虚线* 右 = 动态数组_获取(虚线序列, i + 1);
        if (中枢_基础检查(左, 中, 右) && 虚线_方向(左) == 起始方向)
            return 中枢_创建(左, 中, 右, 0, 标识);
    }
    return NULL;
}

double 中枢_高(中枢* self) {
    double h = INFINITY;
    for (size_t i = 0; i < self->元素.长度 && i < 3; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (虚线_高(x) < h) h = 虚线_高(x);
    }
    return h;
}

double 中枢_低(中枢* self) {
    double l = -INFINITY;
    for (size_t i = 0; i < self->元素.长度 && i < 3; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (虚线_低(x) > l) l = 虚线_低(x);
    }
    return l;
}

double 中枢_高高(中枢* self) {
    double h = -INFINITY;
    size_t n = self->元素.长度 > 3 ? self->元素.长度 : 3;
    for (size_t i = 0; i < self->元素.长度; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (虚线_高(x) > h) h = 虚线_高(x);
    }
    return h;
}

double 中枢_低低(中枢* self) {
    double l = INFINITY;
    for (size_t i = 0; i < self->元素.长度; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (虚线_低(x) < l) l = 虚线_低(x);
    }
    return l;
}

分型* 中枢_文(中枢* self) {
    return ((虚线*)动态数组_获取(&self->元素, 0))->文;
}

分型* 中枢_武(中枢* self) {
    return ((虚线*)动态数组_获取(&self->元素, self->元素.长度 - 1))->武;
}

相对方向 中枢_方向(中枢* self) {
    return 相对方向_翻转(虚线_方向(动态数组_获取(&self->元素, 0)));
}

虚线* 中枢_离开段(中枢* self) {
    return 动态数组_获取(&self->元素, self->元素.长度 - 1);
}

bool 中枢_完整性(中枢* self) {
    if (self->元素.长度 == 0) return false;
    虚线* first = 动态数组_获取(&self->元素, 0);
    if (strcmp(first->标识, "线段") != 0 && strcmp(first->标识, "扩展线段") != 0) {
        return self->第三买卖线 != NULL;
    }
    虚线* last = 动态数组_获取(&self->元素, self->元素.长度 - 1);
    for (size_t i = 0; i < last->合_中枢序列.长度; i++) {
        中枢* inner = 动态数组_获取(&last->合_中枢序列, i);
        if (相对方向_是否缺口(相对方向_分析(中枢_高(self), 中枢_低(self), 中枢_高(inner), 中枢_低(inner))))
            return true;
    }
    return false;
}

void 中枢_获取序列(中枢* self, 动态数组* 结果) {
    for (size_t i = 0; i < self->元素.长度; i++)
        动态数组_追加(结果, self->元素.数据[i]);
    if (self->第三买卖线) 动态数组_追加(结果, self->第三买卖线);
}

void 中枢_获取扩展中枢(中枢* self, 动态数组* 扩展中枢, 缠论配置* 配置) {
    if (self->元素.长度 >= 9) {
        动态数组 ext_segs;
        动态数组_初始化(&ext_segs, 4);
        线段_扩展分析(&self->元素, &ext_segs, 配置);
        char buf[128];
        snprintf(buf, 127, "%s_扩展中枢_", self->标识);
        中枢_分析(&ext_segs, 扩展中枢, false, buf);
        动态数组_释放(&ext_segs, false);
    }
}

bool 中枢_校验合法性(中枢* self, 动态数组* 序列, 动态数组* 中枢序列) {
    (void)中枢序列;
    /* 过滤无效元素 */
    size_t 有效长度 = self->元素.长度;
    for (size_t i = 0; i < self->元素.长度; i++) {
        if (动态数组_查找(序列, self->元素.数据[i]) == (size_t)-1) {
            有效长度 = i; break;
        }
    }
    if (有效长度 < 3) {
        self->第三买卖线 = NULL; self->本级_第三买卖线 = NULL;
        return false;
    }
    self->元素.长度 = 有效长度;

    /* 过滤缺口后的元素 */
    有效长度 = self->元素.长度;
    double z高 = 中枢_高(self), z低 = 中枢_低(self);
    for (size_t i = 0; i < self->元素.长度; i++) {
        虚线* x = 动态数组_获取(&self->元素, i);
        if (相对方向_是否缺口(相对方向_分析(z高, z低, 虚线_高(x), 虚线_低(x)))) {
            有效长度 = i; break;
        }
    }
    self->元素.长度 = 有效长度;
    if (self->元素.长度 < 3) return false;

    /* 检查连续性 */
    for (size_t i = 1; i < self->元素.长度; i++) {
        if (!虚线_之后是(动态数组_获取(&self->元素, i - 1), 动态数组_获取(&self->元素, i)))
            return false;
    }

    /* 检查重叠 */
    if (!相对方向_是否缺口(相对方向_分析(
            ((虚线*)self->元素.数据[0])->高, ((虚线*)self->元素.数据[0])->低,
            ((虚线*)self->元素.数据[2])->高, ((虚线*)self->元素.数据[2])->低))) {
        double 重叠高 = 中枢_高(self), 重叠低 = 中枢_低(self);
        if (重叠低 > 重叠高) return false;
    }

    /* 检查第三买卖线 */
    if (self->第三买卖线) {
        if (动态数组_查找(序列, self->第三买卖线) != (size_t)-1) {
            if (!虚线_之后是(动态数组_获取(&self->元素, self->元素.长度 - 1), self->第三买卖线)) {
                self->第三买卖线 = NULL;
            } else {
                if (!相对方向_是否缺口(相对方向_分析(z高, z低,
                        虚线_高(self->第三买卖线), 虚线_低(self->第三买卖线)))) {
                    动态数组_追加(&self->元素, self->第三买卖线);
                    self->第三买卖线 = NULL;
                }
            }
        } else {
            self->第三买卖线 = NULL;
        }
    }
    return true;
}

void 中枢_设置第三买卖线(中枢* self, 虚线* 线) {
    self->第三买卖线 = 线;
}

const char* 中枢_当前状态(中枢* self) {
    分型* 尾部 = ((虚线*)动态数组_获取(&self->元素, self->元素.长度 - 1))->武;
    虚线* last = 动态数组_获取(&self->元素, self->元素.长度 - 1);
    if (strcmp(last->标识, "线段") == 0) {
        尾部 = ((虚线*)动态数组_获取(&last->基础序列, last->基础序列.长度 - 1))->武;
    }
    相对方向 rel = 相对方向_分析(中枢_高(self), 中枢_低(self), 尾部->中->高, 尾部->中->低);
    if (rel == 相对方向_向上缺口) return "中枢之上";
    if (rel == 相对方向_向下缺口) return "中枢之下";
    return "中枢之中";
}

bool 中枢_基础检查(虚线* 左, 虚线* 中, 虚线* 右) {
    if (!虚线_之后是(左, 中)) return false;
    if (!虚线_之后是(中, 右)) return false;
    相对方向 rel = 相对方向_分析(左->高, 左->低, 右->高, 右->低);
    return rel == 相对方向_向下 || rel == 相对方向_向上 ||
           rel == 相对方向_顺 || rel == 相对方向_逆 || rel == 相对方向_同;
}

/* ================================================================
 * 中枢_分析 — while 循环代替尾递归
 * ================================================================ */

void 中枢_分析(动态数组* 虚线序列, 动态数组* 中枢序列,
               bool 跳过首部, const char* 标识) {
    if (虚线序列->长度 == 0) return;
    int 深度 = 0;

    while (深度 < 128) {
        if (中枢序列->长度 == 0) {
            for (size_t i = 1; i + 1 < 虚线序列->长度; i++) {
                虚线* 左 = 动态数组_获取(虚线序列, i - 1);
                虚线* 中 = 动态数组_获取(虚线序列, i);
                虚线* 右 = 动态数组_获取(虚线序列, i + 1);
                if (!中枢_基础检查(左, 中, 右)) continue;
                if (跳过首部 && (左->序号 == 0 || i == 0)) continue;
                if (i >= 2) {
                    相对方向 rel = 相对方向_分析(
                        ((虚线*)动态数组_获取(虚线序列, i - 2))->高,
                        ((虚线*)动态数组_获取(虚线序列, i - 2))->低,
                        左->高, 左->低);
                    if (相对方向_是否向上(rel) && 虚线_方向(左) == 相对方向_向上) continue;
                    if (相对方向_是否向下(rel) && 虚线_方向(左) == 相对方向_向下) continue;
                }
                中枢* z = 中枢_创建(左, 中, 右, 中->级别, 标识);
                z->序号 = 0;
                动态数组_追加(中枢序列, z);
                引用(z);
                深度++; goto restart;
            }
            return;
        }

        中枢* cur = 动态数组_获取(中枢序列, 中枢序列->长度 - 1);
        if (!中枢_校验合法性(cur, 虚线序列, 中枢序列)) {
            入回收站(cur);
            解引用(cur);
            中枢序列->长度--;
            深度++; continue;
        }

        size_t last_idx = 动态数组_查找(虚线序列, cur->元素.数据[cur->元素.长度 - 1]);
        if (last_idx == (size_t)-1) { 深度++; continue; }
        size_t start = last_idx + 1;

        /* 收集后续元素 */
        动态数组 后续;
        动态数组_初始化(&后续, 8);
        for (size_t idx = start; idx < 虚线序列->长度; idx++) {
            虚线* x = 动态数组_获取(虚线序列, idx);
            if (相对方向_是否缺口(相对方向_分析(中枢_高(cur), 中枢_低(cur), 虚线_高(x), 虚线_低(x)))) {
                动态数组_追加(&后续, x);
                if (虚线_之后是((虚线*)cur->元素.数据[cur->元素.长度 - 1], x))
                    cur->第三买卖线 = x;
            } else {
                if (后续.长度 == 0) {
                    assert(虚线_之后是((虚线*)cur->元素.数据[cur->元素.长度 - 1], x));
                    动态数组_追加(&cur->元素, x);
                } else {
                    动态数组_追加(&后续, x);
                }
            }

            while (后续.长度 >= 3) {
                中枢* z = 中枢_从序列中获取中枢(&后续, 相对方向_翻转(虚线_方向(
                    (虚线*)cur->元素.数据[cur->元素.长度 - 1])), 标识);
                if (!z) {
                    /* pop front */
                    for (size_t j = 1; j < 后续.长度; j++) 后续.数据[j-1] = 后续.数据[j];
                    后续.长度--;
                } else {
                    z->序号 = ((中枢*)动态数组_获取(中枢序列, 中枢序列->长度 - 1))->序号 + 1;
                    动态数组_追加(中枢序列, z);
                    引用(z);
                    cur = z;
                    后续.长度 = 0;
                }
            }
        }
        动态数组_释放(&后续, false);
        return;
restart:;
    }
}

void 释放中枢(中枢* obj) {
    动态数组_释放(&obj->元素, false);
}

/* ================================================================
 * 背驰分析
 * ================================================================ */

static void 背驰_获取MACD(虚线* 段, K线** 普K序列, size_t 普K长,
                          double* 阳, double* 阴, double* 合, double* 总) {
    K线** sub = NULL; size_t sub_len = 0;
    /* 找标的K线索引 */
    size_t si = (size_t)-1, ei = (size_t)-1;
    for (size_t i = 0; i < 普K长; i++) {
        if (普K序列[i] == 段->文->中->标的K线) si = i;
        if (普K序列[i] == 段->武->中->标的K线) { ei = i; break; }
    }
    if (si != (size_t)-1 && ei != (size_t)-1 && ei >= si) {
        sub = 普K序列 + si; sub_len = ei - si + 1;
    }
    K线_获取MACD(普K序列, 普K长, 段->文->中->标的K线, 段->武->中->标的K线, 阳, 阴, 合, 总);
    (void)sub; (void)sub_len;
}

bool 背驰分析_MACD背驰(虚线* 进入段, 虚线* 离开段, K线** 序列, size_t 长度, const char* 方式) {
    double 进阳, 进阴, 进合, 进总, 离阳, 离阴, 离合, 离总;
    背驰_获取MACD(进入段, 序列, 长度, &进阳, &进阴, &进合, &进总);
    背驰_获取MACD(离开段, 序列, 长度, &离阳, &离阴, &离合, &离总);

    double 进面积 = (strcmp(方式, "总") == 0) ? 进总 :
        (虚线_方向(进入段) == 相对方向_向上 ? 进阳 : 进阴);
    double 离面积 = (strcmp(方式, "总") == 0) ? 离总 :
        (虚线_方向(进入段) == 相对方向_向上 ? 离阳 : 离阴);
    return fabs(离面积) < fabs(进面积);
}

bool 背驰分析_斜率背驰(虚线* 进入段, 虚线* 离开段) {
    double dx1 = difftime(进入段->武->时间戳, 进入段->文->时间戳);
    double dy1 = 进入段->武->分型特征值 - 进入段->文->分型特征值;
    double s1 = dy1 / dx1;

    double dx2 = difftime(离开段->武->时间戳, 离开段->文->时间戳);
    double dy2 = 离开段->武->分型特征值 - 离开段->文->分型特征值;
    double s2 = dy2 / dx2;

    if (虚线_方向(进入段) == 相对方向_向上)
        return 虚线_高(离开段) > 虚线_高(进入段) && fabs(s2) < fabs(s1);
    else
        return 虚线_低(离开段) < 虚线_低(进入段) && fabs(s2) < fabs(s1);
}

bool 背驰分析_测度背驰(虚线* 进入段, 虚线* 离开段) {
    double dx1 = difftime(进入段->武->时间戳, 进入段->文->时间戳);
    double dy1 = 进入段->武->分型特征值 - 进入段->文->分型特征值;
    double m1 = sqrt(dx1*dx1 + dy1*dy1);

    double dx2 = difftime(离开段->武->时间戳, 离开段->文->时间戳);
    double dy2 = 离开段->武->分型特征值 - 离开段->文->分型特征值;
    double m2 = sqrt(dx2*dx2 + dy2*dy2);

    if (虚线_方向(进入段) == 相对方向_向上)
        return 虚线_高(离开段) > 虚线_高(进入段) && fabs(m2) < fabs(m1);
    else
        return 虚线_低(离开段) < 虚线_低(进入段) && fabs(m2) < fabs(m1);
}

bool 背驰分析_全量背驰(虚线* 进入段, 虚线* 离开段, K线** 序列, size_t 长度) {
    return 背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总") &&
           背驰分析_测度背驰(进入段, 离开段) &&
           背驰分析_斜率背驰(进入段, 离开段);
}

bool 背驰分析_任意背驰(虚线* 进入段, 虚线* 离开段, K线** 序列, size_t 长度) {
    return 背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总") ||
           背驰分析_测度背驰(进入段, 离开段) ||
           背驰分析_斜率背驰(进入段, 离开段);
}

bool 背驰分析_配置背驰(虚线* 进入段, 虚线* 离开段, K线** 序列, size_t 长度, 缠论配置* 配置) {
    bool a = 背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总");
    bool b = 背驰分析_测度背驰(进入段, 离开段);
    bool c = 背驰分析_斜率背驰(进入段, 离开段);

    if (配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) return a && b && c;
    if (!配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) return false;
    if (配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) return a && c;
    if (!配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) return b;
    if (配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) return a;
    if (!配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) return b && c;
    if (!配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) return c;
    if (配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) return a && b;
    return false;
}

bool 背驰分析_任选背驰(虚线* 进入段, 虚线* 离开段, K线** 序列, size_t 长度) {
    int count = 0;
    if (背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总")) count++;
    if (背驰分析_测度背驰(进入段, 离开段)) count++;
    if (背驰分析_斜率背驰(进入段, 离开段)) count++;
    return count >= 2;
}

bool 背驰分析_背驰模式(虚线* 进入段, 虚线* 离开段, K线** 序列, size_t 长度, 缠论配置* 配置, const char* 模式) {
    if (strcmp(模式, "全量") == 0) return 背驰分析_全量背驰(进入段, 离开段, 序列, 长度);
    if (strcmp(模式, "任意") == 0) return 背驰分析_任意背驰(进入段, 离开段, 序列, 长度);
    if (strcmp(模式, "配置") == 0) return 背驰分析_配置背驰(进入段, 离开段, 序列, 长度, 配置);
    if (strcmp(模式, "相对") == 0) return 背驰分析_任选背驰(进入段, 离开段, 序列, 长度);
    return false;
}

/* ================================================================
 * 观察者
 * ================================================================ */

观察者* 观察者_新建(const char* 符号, int 周期, 缠论配置* 配置) {
    观察者* o = 分配(sizeof(观察者), CHAN_TYPE_观察者);
    strncpy(o->符号, 符号, 63); o->符号[63] = '\0';
    o->周期 = 周期;
    配置->标识[0] = '\0'; strncpy(配置->标识, 符号, 63);
    o->配置 = 配置;
    引用(配置);

    o->有终止 = (配置->手动终止[0] != '\0');
    o->终止时间戳 = o->有终止 ? 转化为时间戳(配置->手动终止) : 0;

    动态数组_初始化(&o->基础缠K序列, 128);
    动态数组_初始化(&o->普通K线序列, 128);
    动态数组_初始化(&o->缠论K线序列, 128);
    动态数组_初始化(&o->分型序列, 64);
    动态数组_初始化(&o->笔序列, 64);
    动态数组_初始化(&o->笔_中枢序列, 16);
    动态数组_初始化(&o->线段序列, 32);
    动态数组_初始化(&o->中枢序列, 16);
    动态数组_初始化(&o->扩展线段序列, 32);
    动态数组_初始化(&o->扩展中枢序列, 16);
    动态数组_初始化(&o->扩展线段序列_线段, 32);
    动态数组_初始化(&o->扩展中枢序列_线段, 16);
    动态数组_初始化(&o->线段_线段序列, 32);
    动态数组_初始化(&o->线段_中枢序列, 16);
    动态数组_初始化(&o->扩展线段序列_扩展线段, 32);
    动态数组_初始化(&o->扩展中枢序列_扩展线段, 16);

    动态数组_初始化(&o->回收站, 128);

    return o;
}

void 观察者_增加原始K线(观察者* self, K线* 普K) {
    chan_回收站 = &self->回收站;
    if (self->有终止 && 普K->时间戳 > self->终止时间戳) return;

    const char* 状态 = NULL;
    分型* 当前分型 = NULL;
    缠论K线_分析(普K, &self->缠论K线序列, &self->普通K线序列, self->配置, &状态, &当前分型);
    if (!当前分型) return;

    size_t 笔序列_之前长度 = self->笔序列.长度;
    if (self->配置->分析笔)
        笔_分析(当前分型, &self->分型序列, &self->笔序列,
                 &self->缠论K线序列, &self->普通K线序列, self->配置);
    if (self->分型序列.长度 == 0) return;
    if (self->笔序列.长度 == 0) return;

    /* In C's incremental model, 笔_分析 may pop strokes without immediately
       adding replacements (unlike Python's recursive single-call pattern).
       Skip downstream analysis when笔序列 shrank to avoid segment corruption. */
    if (self->笔序列.长度 < 笔序列_之前长度) {
        return;
    }

    if (self->配置->分析笔中枢)
        中枢_分析(&self->笔序列, &self->笔_中枢序列, true, "");

    if (self->配置->分析线段)
        线段_分析(&self->笔序列, &self->线段序列, self->配置);
    if (self->配置->分析线段中枢)
        中枢_分析(&self->线段序列, &self->中枢序列, true, "");

    if (self->配置->分析扩展线段)
        线段_扩展分析(&self->笔序列, &self->扩展线段序列, self->配置);
    if (self->配置->分析线段中枢)
        中枢_分析(&self->扩展线段序列, &self->扩展中枢序列, true, "");

    if (self->配置->分析扩展线段)
        线段_扩展分析(&self->线段序列, &self->扩展线段序列_线段, self->配置);
    if (self->配置->分析线段中枢)
        中枢_分析(&self->扩展线段序列_线段, &self->扩展中枢序列_线段, true, "");

    if (self->配置->分析线段)
        线段_分析(&self->线段序列, &self->线段_线段序列, self->配置);
    if (self->配置->分析线段中枢)
        中枢_分析(&self->线段_线段序列, &self->线段_中枢序列, true, "");

    if (self->配置->分析扩展线段)
        线段_扩展分析(&self->扩展线段序列, &self->扩展线段序列_扩展线段, self->配置);
    if (self->配置->分析线段中枢)
        中枢_分析(&self->扩展线段序列_扩展线段, &self->扩展中枢序列_扩展线段, true, "");
}

观察者* 观察者_读取数据文件(const char* 文件路径, 缠论配置* 配置) {
    /* 解析文件名: symbol-period-start-end.nb */
    const char* name = strrchr(文件路径, '/');
    if (!name) name = 文件路径; else name++;
    char name_buf[256];
    strncpy(name_buf, name, 255); name_buf[255] = '\0';
    char* dot = strrchr(name_buf, '.');
    if (dot) *dot = '\0';

    char 符号[64] = "", 周期_str[32] = "";
    sscanf(name_buf, "%63[^-]-%31s", 符号, 周期_str);
    int 周期 = atoi(周期_str);

    观察者* obs = 观察者_新建(符号, 周期, 配置);

    FILE* f = fopen(文件路径, "rb");
    if (!f) { perror(文件路径); return obs; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buffer = malloc(fsize);
    if (!buffer) { fclose(f); return obs; }
    fread(buffer, 1, fsize, f);
    fclose(f);

    size_t record_size = 48; /* 6 * 8 bytes */
    for (long i = 0; i < fsize; i += record_size) {
        if (i + (long)record_size > fsize) break;
        K线* k = K线_读取大端字节数组(buffer + i, 周期, "Bar");
        k->序号 = (int)(i / record_size);
        观察者_增加原始K线(obs, k);
    }
    free(buffer);
    return obs;
}

void 释放观察者(观察者* obj) {
    /*
     * 三步清理：
     *   1. 自上而下移除序列引用（仅解引用一次）。
     *      幸存者（仍有创建引用或外部引用）入回收站。
     *   2. 去重，移除所有创建引用。
     *   3. 临时引用 + 多遍级联释放（仅叶节点逐轮释放，确保拓扑序）。
     */

    /* ======== Phase 1: 移除序列引用 ======== */
    #define 清理序列引用(seq) do { \
        for (size_t _i = 0; _i < (seq).长度; _i++) { \
            void* _e = 动态数组_获取(&(seq), _i); \
            if (!_e) continue; \
            int _rc = 引用计数(_e); \
            if (_rc > 1) { \
                解引用(_e); /* 序列引用 */ \
                if (引用计数(_e) > 0) 入回收站(_e); \
            } else { \
                解引用(_e); /* 仅序列引用就释放 */ \
            } \
        } \
        free((seq).数据); \
        (seq).数据 = NULL; (seq).长度 = 0; (seq).容量 = 0; \
    } while(0)

    /* 中枢层 */
    清理序列引用(obj->扩展中枢序列_扩展线段);
    清理序列引用(obj->扩展中枢序列_线段);
    清理序列引用(obj->扩展中枢序列);
    清理序列引用(obj->线段_中枢序列);
    清理序列引用(obj->中枢序列);
    清理序列引用(obj->笔_中枢序列);

    /* 扩展线段层 */
    清理序列引用(obj->扩展线段序列_扩展线段);
    清理序列引用(obj->扩展线段序列_线段);
    清理序列引用(obj->扩展线段序列);

    /* 线段层 */
    清理序列引用(obj->线段_线段序列);
    清理序列引用(obj->线段序列);

    /* 笔层 */
    清理序列引用(obj->笔序列);

    /* 分型层 */
    清理序列引用(obj->分型序列);

    /* 缠论K线层 */
    清理序列引用(obj->基础缠K序列);
    清理序列引用(obj->缠论K线序列);

    /* 普通K线层 */
    清理序列引用(obj->普通K线序列);

    #undef 清理序列引用

    /* ======== Phase 2: 去重，移除创建引用 ======== */
    /* 此刻回收站中：
     *   - 旧条目（pop 来源）：rc = 1（创建）+ 外部引用。序列引用已被 pop 的 解引用 移除。
     *   - 新条目（Phase 1 入回收站）：rc = 1（创建）+ 外部引用。序列引用已被 Phase 1 移除。
     *   所有条目统一为：rc = 1 + 外部引用。 */

    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* a = 动态数组_获取(&obj->回收站, i);
        if (!a) continue;
        for (size_t j = i + 1; j < obj->回收站.长度; j++) {
            if (动态数组_获取(&obj->回收站, j) == a)
                动态数组_设置(&obj->回收站, j, NULL);
        }
    }

    /* 对去重后的条目移除创建引用。
     * 使用临时引用保护：先 +1 临时引用，再移除创建引用（-1），
     * 使 rc 从 1+E 变为 1+E（临时引用抵消创建引用移除）。
     * 对于 E=0 的条目：rc 1→0 会释放；临时引用先于创建引用移除来防止此情况。 */
    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* e = 动态数组_获取(&obj->回收站, i);
        if (!e) continue;
        引用(e); /* 临时引用，防止以下解引用直接释放 E=0 条目 */
    }
    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* e = 动态数组_获取(&obj->回收站, i);
        if (!e) continue;
        解引用(e); /* 移除创建引用。rc = 1+E+1-1 = 1+E。若 E=0 → rc=1（临时引用存活） */
    }

    if (CHAN_DEBUG_MEM) {
        size_t 回收站类型计数[CHAN_TYPE_COUNT] = {0};
        for (size_t i = 0; i < obj->回收站.长度; i++) {
            void* e = 动态数组_获取(&obj->回收站, i);
            if (e) 回收站类型计数[对象类型_取(e)]++;
        }
        fprintf(stderr, "=== 回收站类型分布 ===\n");
        for (int ti = 0; ti < CHAN_TYPE_COUNT; ti++) {
            if (回收站类型计数[ti] > 0)
                fprintf(stderr, "  %s: %zu\n", 类型名称[ti], 回收站类型计数[ti]);
        }
    }

    /* ======== Phase 3: 多遍级联释放 ======== */
    /* 此刻 rc = 1（临时）+ 外部引用 = 1 + E。
     * 多遍扫描：每遍释放 rc==1 的条目（E=0，无外部持有者）。
     * 解引用移除临时引用，析构级联降低其他条目的 E。
     * 由于先释放 E=0 的叶节点，父节点在下轮 E 归零后才被释放，
     * 不会出现在同一轮父子同时释放的情况。 */

    {
        size_t n = 0;
        for (int pass = 0; pass < 1000; pass++) {
            size_t 本轮 = 0;
            for (size_t i = 0; i < obj->回收站.长度; i++) {
                void* e = 动态数组_获取(&obj->回收站, i);
                if (!e || 引用计数(e) != 1) continue;
                解引用(e); /* 移除临时引用。E=0 时 rc 1→0，析构触发 */
                动态数组_设置(&obj->回收站, i, NULL);
                本轮++;
            }
            n += 本轮; if (本轮 == 0) break;
        }
        if (CHAN_DEBUG_MEM) fprintf(stderr, "=== 回收站多遍扫描: 释放%zu ===\n", n);
    }

    /* ======== Phase 4: 移除临时引用 ======== */
    /* 每个幸存者当前 rc = 1(temp) + E。
     * 移除临时引用（解引用一次），使 rc = E。对象不会被释放（E >= 1），
     * 但引用计数恢复正常，析构级联可能减少其他条目的 E。 */
    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* e = 动态数组_获取(&obj->回收站, i);
        if (!e) continue;
        解引用(e); /* 移除临时引用, rc = E */
    }

    /* 多遍扫描：若临时引用移除触发了析构级联，继续释放 rc==0 的条目 */
    {
        size_t n2 = 0;
        for (int pass = 0; pass < 100; pass++) {
            size_t 本轮2 = 0;
            for (size_t i = 0; i < obj->回收站.长度; i++) {
                void* e = 动态数组_获取(&obj->回收站, i);
                if (!e || 引用计数(e) != 0) continue;
                /* rc==0 表示所有引用已释放，对象析构已在级联中触发，仅需清理 */
                动态数组_设置(&obj->回收站, i, NULL);
                本轮2++;
            }
            n2 += 本轮2; if (本轮2 == 0) break;
        }
        if (CHAN_DEBUG_MEM && n2 > 0)
            fprintf(stderr, "=== Phase 4 清理: %zu ===\n", n2);
    }

    /* ======== Phase 5: 打破引用循环，强制释放 ======== */
    /* 剩余幸存者形成引用循环（虚 ↔ 线段特征、虚 → 中枢 → 虚），
     * 纯引用计数无法清零。三步处理：
     *   5a. 对于虚，释放其内部中枢/线段特征（这些不在回收站中）
     *   5b. 清空所有内部数组（断开引用图的边）
     *   5c. 直接 free() 每个回收站对象 */
    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* e = 动态数组_获取(&obj->回收站, i);
        if (!e || 对象类型_取(e) != CHAN_TYPE_虚线) continue;
        虚线* x = (虚线*)e;
        /* 释放内部中枢（不在任何观察者序列中，Phase 1-4 未处理） */
        #define 释放内部中枢序列(_seq) do { \
            for (size_t _j = 0; _j < (_seq).长度; _j++) { \
                中枢* _z = 动态数组_获取(&(_seq), _j); \
                if (!_z) continue; \
                释放中枢(_z); \
                free(_z); \
            } \
        } while(0)
        释放内部中枢序列(x->实_中枢序列);
        释放内部中枢序列(x->虚_中枢序列);
        释放内部中枢序列(x->合_中枢序列);
        #undef 释放内部中枢序列
        /* 释放内部线段特征（含其元素中的假笔等非回收站对象） */
        for (size_t _j = 0; _j < x->特征序列.长度; _j++) {
            线段特征* _ft = 动态数组_获取(&x->特征序列, _j);
            if (!_ft) continue;
            for (size_t _k = 0; _k < _ft->元素.长度; _k++) {
                虚线* _elem = 动态数组_获取(&_ft->元素, _k);
                if (!_elem) continue;
                /* 在回收站中的笔由 Phase 5c 释放，此处仅释放假笔 */
                bool _in_gv = false;
                for (size_t _g = 0; _g < obj->回收站.长度; _g++) {
                    if (动态数组_获取(&obj->回收站, _g) == (void*)_elem)
                        { _in_gv = true; break; }
                }
                if (!_in_gv) {
                    释放虚线(_elem);
                    free(_elem);
                }
            }
            free(_ft->元素.数据);
            free(_ft);
        }
    }

    /* 清空所有内部数组（不调用解引用，已在上一步释放） */
    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* e = 动态数组_获取(&obj->回收站, i);
        if (!e) continue;
        switch (对象类型_取(e)) {
        case CHAN_TYPE_虚线: {
            虚线* x = (虚线*)e;
            free(x->基础序列.数据);
            free(x->特征序列.数据);
            free(x->实_中枢序列.数据);
            free(x->虚_中枢序列.数据);
            free(x->合_中枢序列.数据);
            解引用(x->前一缺口);
            break;
        }
        case CHAN_TYPE_中枢: {
            free(((中枢*)e)->元素.数据);
            break;
        }
        case CHAN_TYPE_线段特征: {
            free(((线段特征*)e)->元素.数据);
            break;
        }
        case CHAN_TYPE_K线: {
            K线* k = (K线*)e;
            解引用(k->macd);
            解引用(k->rsi);
            解引用(k->kdj);
            break;
        }
        default: break;
        }
    }

    /* 边已全部断开，直接释放对象 */
    size_t 强制清理 = 0;
    for (size_t i = 0; i < obj->回收站.长度; i++) {
        void* e = 动态数组_获取(&obj->回收站, i);
        if (!e) continue;
        free(e);
        动态数组_设置(&obj->回收站, i, NULL);
        强制清理++;
    }
    if (CHAN_DEBUG_MEM && 强制清理 > 0)
        fprintf(stderr, "=== Phase 5 强制清理: %zu ===\n", 强制清理);

    动态数组_释放(&obj->回收站, false);

    解引用(obj->配置);
}

/* ================================================================
 * main
 * ================================================================ */

int main(int argc, char** argv) {
    const char* 文件路径 = "./btcusd-300-1761327300-1776327900.nb";
    if (argc > 1) 文件路径 = argv[1];

    printf("加载文件: %s\n", 文件路径);

    缠论配置* 配置 = 缠论配置_不推送();
    配置->加载文件路径[0] = '\0'; strncpy(配置->加载文件路径, 文件路径, 255);

    time_t start = time(NULL);
    观察者* obs = 观察者_读取数据文件(文件路径, 配置);
    time_t elapsed = time(NULL) - start;

    printf("加载完成. 普K: %zu, 笔: %zu, 线段: %zu, 中枢: %zu, 耗时: %lds\n",
           obs->普通K线序列.长度, obs->笔序列.长度,
           obs->线段序列.长度, obs->中枢序列.长度, (long)elapsed);

    /* 导出笔序列 */
    FILE* f笔 = fopen("c_笔.txt", "w");
    for (size_t i = 0; i < obs->笔序列.长度; i++) {
        虚线* b = 动态数组_获取(&obs->笔序列, i);
        fprintf(f笔, "%d %d %s %.6f %.6f %ld %ld %.6f %.6f\n",
                b->序号, b->级别,
                虚线_方向(b) == 相对方向_向上 ? "上" : "下",
                b->高, b->低,
                (long)b->文->时间戳, (long)b->武->时间戳,
                b->文->分型特征值, b->武->分型特征值);
    }
    fclose(f笔);

    /* 导出线段序列 */
    FILE* f段 = fopen("c_线段.txt", "w");
    for (size_t i = 0; i < obs->线段序列.长度; i++) {
        虚线* s = 动态数组_获取(&obs->线段序列, i);
        fprintf(f段, "%d %d %s %s %.6f %.6f %zu %ld %ld %.6f %.6f",
                s->序号, s->级别,
                虚线_方向(s) == 相对方向_向上 ? "上" : "下",
                线段_四象(s),
                s->高, s->低,
                s->基础序列.长度,
                (long)s->文->时间戳, (long)s->武->时间戳,
                s->文->分型特征值, s->武->分型特征值);
        for (size_t j = 0; j < s->基础序列.长度; j++) {
            虚线* bs = 动态数组_获取(&s->基础序列, j);
            fprintf(f段, " %d", bs->序号);
        }
        fprintf(f段, "\n");
    }
    fclose(f段);

    /* 导出中枢序列 */
    FILE* f枢 = fopen("c_中枢.txt", "w");
    for (size_t i = 0; i < obs->中枢序列.长度; i++) {
        中枢* z = 动态数组_获取(&obs->中枢序列, i);
        fprintf(f枢, "%d %d %s %.6f %.6f %zu",
                z->序号, z->级别, z->标识,
                中枢_高(z), 中枢_低(z), z->元素.长度);
        for (size_t j = 0; j < z->元素.长度; j++) {
            虚线* e = 动态数组_获取(&z->元素, j);
            fprintf(f枢, " %s_%d", e->标识, e->序号);
        }
        fprintf(f枢, "\n");
    }
    fclose(f枢);

    解引用(obs);
    解引用(配置);
    // 打印内存摘要();
    // 打印笔线段摘要();

    return 0;
}
