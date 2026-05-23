/*
 * MIT License
 *
 * Copyright (c) 2026 YuYuKunKun
 *
 * chan.c — 缠论 C99 移植实现
 */

#define _POSIX_C_SOURCE 200112L
#define _FILE_OFFSET_BITS 64
#include "chan.h"

#include <sys/stat.h>

/* Arena 模式：对象从观察者的内存池分配，观察者销毁时整池释放。
   全局内存池 在 获取全局池 时初始化。
   分析过程中弹出/替换的对象直接 解引用——refcount 归零时执行
   对象销毁（清理内部资源），但 skip free（池拥有内存）。 */

/* ================================================================
 *  全局内存池
 * ================================================================ */

void *全局内存池 = NULL;

/* 每个类型一个空闲链：已销毁 + 弱引用归零 的对象压入，供 分配() O(1) 复用 */
static void *类型空闲链[CHAN_TYPE_COUNT] = {NULL};
static void 尝试回收(void *obj);  /* 前置声明 */

/* ================================================================
 *  内部：获取块的数据区域起始地址
 * ================================================================ */

static inline void *块_数据区(内存池块 *block) {
    return (char *) block + 内存池_头部大小;
}

/* ================================================================
 *  内部：向上对齐
 * ================================================================ */

static inline size_t 向上对齐(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

/* ================================================================
 *  内部：分配新块并链接到池中
 * ================================================================ */

static 内存池块 *分配新块(内存池 *self, size_t 数据容量) {
    size_t 分配总大小 = 内存池_头部大小 + 数据容量;
    内存池块 *block = malloc(分配总大小);
    if (!block) {
        return NULL;
    }

    block->下一块 = NULL;
    block->容量 = 数据容量;
    block->已用 = 0;

    /* 链入尾部 */
    if (!self->首块) {
        self->首块 = block;
        self->当前块 = block;
    } else {
        内存池块 *tail = self->首块;
        while (tail->下一块) {
            tail = tail->下一块;
        }
        tail->下一块 = block;
    }

    self->总容量 += 数据容量;
    return block;
}

/* ================================================================
 *  API 实现
 * ================================================================ */

void 内存池_初始化(内存池 *self, size_t 初始块大小) {
    if (初始块大小 == 0) {
        初始块大小 = 1024 * 1024;    /* 默认 1 MB */
    }

    self->首块 = NULL;
    self->当前块 = NULL;
    self->块大小 = 初始块大小;
    self->总容量 = 0;
    self->总分配 = 0;
    self->分配次数 = 0;
    self->清理头 = NULL;
    pthread_mutex_init(&self->锁, NULL);

    分配新块(self, 初始块大小);
}

void *内存池_分配(内存池 *self, size_t 大小) {
    if (大小 == 0) {
        return NULL;
    }

    pthread_mutex_lock(&self->锁);

    size_t 对齐后大小 = 向上对齐(大小, 内存池_对齐);
    内存池块 *block = self->当前块;

    /* 当前块空间不足时分配新块 */
    if (!block || block->已用 + 对齐后大小 > block->容量) {
        size_t 新块大小 = self->块大小;

        /* 单次请求大于默认块大小 → 分配 oversized 专用块 */
        if (对齐后大小 > 新块大小) {
            新块大小 = 对齐后大小;
        } else {
            /* 扩容：下一个默认块 2x */
            self->块大小 = (self->块大小 < (SIZE_MAX / 2))
                              ? self->块大小 * 2
                              : self->块大小;
        }

        block = 分配新块(self, 新块大小);
        if (!block) {
            pthread_mutex_unlock(&self->锁);
            return NULL;
        }

        self->当前块 = block;
    }

    void *ptr = (char *) 块_数据区(block) + block->已用;
    memset(ptr, 0, 对齐后大小); /* calloc 语义：零初始化 */

    block->已用 += 对齐后大小;
    self->总分配 += 对齐后大小;
    self->分配次数++;

    pthread_mutex_unlock(&self->锁);
    return ptr;
}

void *内存池_分配拷贝(内存池 *self, const void *源, size_t 大小) {
    void *dst = 内存池_分配(self, 大小);
    if (dst) {
        memcpy(dst, 源, 大小);
    }
    return dst;
}

void 内存池_重置(内存池 *self) {
    内存池块 *block = self->首块;
    while (block) {
        block->已用 = 0;
        block = block->下一块;
    }
    self->当前块 = self->首块;
    self->总分配 = 0;
    self->分配次数 = 0;
    self->清理头 = NULL;
    /* 块大小不重置——保留扩容后的配置以适配数据规模 */
}

void 内存池_释放(内存池 *self) {
    内存池块 *block = self->首块;
    while (block) {
        内存池块 *next = block->下一块;
        free(block);
        block = next;
    }
    self->首块 = NULL;
    self->当前块 = NULL;
    self->总容量 = 0;
    self->总分配 = 0;
    self->分配次数 = 0;
    self->清理头 = NULL;
    pthread_mutex_destroy(&self->锁);
}

size_t 内存池_使用量(const 内存池 *self) {
    return self->总分配;
}

size_t 内存池_总容量(const 内存池 *self) {
    return self->总容量;
}

size_t 内存池_分配次数(const 内存池 *self) {
    return self->分配次数;
}

size_t 内存池_块数量(const 内存池 *self) {
    size_t n = 0;
    内存池块 *block = self->首块;
    while (block) {
        n++;
        block = block->下一块;
    }
    return n;
}

/* ================================================================
 * 内存管理
 * ================================================================ */

bool CHAN_DEBUG_MEM = false;

static size_t 分配计数[CHAN_TYPE_COUNT] = {0};
static size_t 释放计数[CHAN_TYPE_COUNT] = {0};

static const char *类型名称[] = {
    "K线", "缠论K线", "分型", "缺口", "虚线", "中枢",
    "线段特征", "特征分型", "平滑异同移动平均线", "相对强弱指数",
    "随机指标", "缠论配置", "观察者", "K线合成器", "立体分析器"
};

void 打印内存摘要(void) {
    fprintf(stderr, "\n=== 内存摘要 ===\n");
    fprintf(stderr, "%-20s %8s %8s %8s\n", "类型", "分配", "释放", "泄漏");
    size_t total_leak = 0;
    for (int i = 0; i < CHAN_TYPE_COUNT; i++) {
        size_t alloc = __atomic_load_n(&分配计数[i], __ATOMIC_RELAXED);
        size_t freed = __atomic_load_n(&释放计数[i], __ATOMIC_RELAXED);
        if (alloc > 0 || freed > 0) {
            size_t leak = alloc - freed;
            total_leak += leak;
            fprintf(stderr, "%-20s %8zu %8zu %8zu%s\n",
                    类型名称[i], alloc, freed, leak,
                    leak > 0 ? " <---" : "");
        }
    }
    fprintf(stderr, "%-20s %8s %8s %8zu\n", "合计", "", "", total_leak);
    fflush(stderr);
}

void chan_memory_diagnostics(void) {
    打印内存摘要();
}

int chan_cnt_alloc(int type) {
    return (type >= 0 && type < CHAN_TYPE_COUNT) ? (int) __atomic_load_n(&分配计数[type], __ATOMIC_RELAXED) : -1;
}

int chan_cnt_free(int type) {
    return (type >= 0 && type < CHAN_TYPE_COUNT) ? (int) __atomic_load_n(&释放计数[type], __ATOMIC_RELAXED) : -1;
}


static pthread_mutex_t 全局池初始化锁 = PTHREAD_MUTEX_INITIALIZER;
static bool 全局池已初始化 = false;

static 内存池 *获取全局池(void) {
    static 内存池 全局池;
    if (!__atomic_load_n(&全局池已初始化, __ATOMIC_ACQUIRE)) {
        pthread_mutex_lock(&全局池初始化锁);
        if (!全局池已初始化) {
            内存池_初始化(&全局池, 0); /* 默认 1MB 块 */
            __atomic_store_n(&全局池已初始化, true, __ATOMIC_RELEASE);
        }
        pthread_mutex_unlock(&全局池初始化锁);
    }
    return &全局池;
}

bool 释放全局内存池(void) {
    内存池 *pool = (内存池 *) __atomic_load_n(&全局内存池, __ATOMIC_ACQUIRE);
    if (!pool) {
        pool = 获取全局池();
        if (!pool) {
            return false;
        }
    }

    /* 第一遍：移除所有池对象的创建引用，触发对象销毁级联
     * 每个对象从 分配() 获得 引用计数=1（创建引用）。
     * 解引用 将引用计数归零 → 对象销毁 → 释放XXX → 清除内部交叉引用 → 弱引用归零。 */
    void *cursor = __atomic_load_n(&pool->清理头, __ATOMIC_ACQUIRE);
    while (cursor) {
        if (!已销毁(cursor)) {
            解引用(cursor);
        }
        cursor = ((对象头结构 *) cursor)->下一清理对象;
    }

    /* 第二遍：验证级联完成 — 所有对象应已销毁且弱引用归零 */
    cursor = __atomic_load_n(&pool->清理头, __ATOMIC_ACQUIRE);
    int 存活数 = 0;
    int 残留弱引用数 = 0;
    while (cursor) {
        if (!已销毁(cursor)) {
            存活数++;
            int wrc = __atomic_load_n(&((对象头结构 *) cursor)->弱引用计数, __ATOMIC_ACQUIRE);
            if (wrc > 0) {
                残留弱引用数++;
            }
        }
        cursor = ((对象头结构 *) cursor)->下一清理对象;
    }

    if (存活数 > 0) {
        fprintf(stderr, "释放全局内存池: %d 对象未被级联销毁 (其中 %d 有残留弱引用)\n",
                存活数, 残留弱引用数);
    }

    /* 第三遍：销毁残余对象（不应有） */
    cursor = __atomic_load_n(&pool->清理头, __ATOMIC_ACQUIRE);
    while (cursor) {
        if (!已销毁(cursor)) {
            对象销毁(cursor);
        }
        cursor = ((对象头结构 *) cursor)->下一清理对象;
    }

    内存池_释放(pool);
    __atomic_store_n(&全局内存池, NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&全局池已初始化, false, __ATOMIC_RELEASE);
    return true;
}

void *分配(size_t 大小, 对象类型 类型) {
    bool 已复用 = false;
    void *ptr = NULL;
    内存池 *pool = NULL;

    /* 1. 优先从类型空闲链取（O(1)） */
    ptr = 类型空闲链[类型];
    if (ptr) {
        类型空闲链[类型] = ((对象头结构 *) ptr)->空闲链下一项;
        已复用 = true;
        pool = ((对象头结构 *) ptr)->所属内存池;
        goto init;
    }

    /* 2. 空闲链无货，走 Arena Bump */
    if (!__atomic_load_n(&全局内存池, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&全局内存池, 获取全局池(), __ATOMIC_RELEASE);
    }
    pool = (内存池 *) __atomic_load_n(&全局内存池, __ATOMIC_ACQUIRE);

    if (pool) {
        ptr = 内存池_分配(pool, 大小);
    } else {
        ptr = calloc(1, 大小);
    }

    if (!ptr) {
        chan_oom_handler(大小);
        return NULL;
    }

init:
    if (已复用) {
        /* 清零覆盖旧数据，保留清理链表指针（空闲链已在上面弹出） */
        void *cleanup_next = ((对象头结构 *) ptr)->下一清理对象;
        memset(ptr, 0, 大小);
        ((对象头结构 *) ptr)->下一清理对象 = cleanup_next;
        fprintf(stderr, "♻ 池复用: %s @ %p\n", 类型名称[类型], ptr);
    }
    引用计数(ptr) = 1;
    对象类型_取(ptr) = 类型;
    ((对象头结构 *) ptr)->所属内存池 = pool;
    ((对象头结构 *) ptr)->弱引用计数 = 0;
    if (pool && !已复用) {
        /* 新分配对象压入清理链表（复用对象已在链中） */
        对象头结构 *old_head;
        do {
            old_head = (对象头结构 *) __atomic_load_n(&pool->清理头, __ATOMIC_ACQUIRE);
            ((对象头结构 *) ptr)->下一清理对象 = old_head;
        } while (!__atomic_compare_exchange_n(&pool->清理头,
                      &((对象头结构 *) ptr)->下一清理对象, ptr,
                      false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    }
    __atomic_fetch_add(&分配计数[类型], 1, __ATOMIC_RELAXED);
    return ptr;
}

static void default_oom_handler(size_t sz) {
    fprintf(stderr, "分配: 内存不足 (请求 %zu 字节)\n", sz);
    abort();
}

void (*chan_oom_handler)(size_t) = default_oom_handler;

void 引用(void *对象) {
    if (!对象) {
        return;
    }
    __atomic_fetch_add(&引用计数(对象), 1, __ATOMIC_RELAXED);
}

void 解引用(void *对象) {
    if (!对象) {
        return;
    }
    if (__atomic_fetch_sub(&引用计数(对象), 1, __ATOMIC_ACQ_REL) <= 1) {
        if (CHAN_DEBUG_MEM) {
            fprintf(stderr, "解引用: 释放对象 %p\n", 对象);
        }
        对象销毁(对象);
        /* 内存池分配的对象：struct 内存归池所有, 不 free */
        if (!((对象头结构 *) 对象)->所属内存池) {
            free(对象);
        }
    }
}

void 对象销毁(void *对象) {
    /* 原子 test-and-set：确保单线程销毁，exchange 返回旧值，true 表示已销毁 */
    if (__atomic_exchange_n(&((对象头结构 *) 对象)->已销毁, true, __ATOMIC_ACQ_REL)) {
        return;
    }
    对象类型 t = 对象类型_取(对象);
    __atomic_fetch_add(&释放计数[t], 1, __ATOMIC_RELAXED);
    switch (t) {
        case CHAN_TYPE_K线:
            释放K线((K线 *) 对象);
            break;
        case CHAN_TYPE_缠论K线:
            释放缠论K线((缠论K线 *) 对象);
            break;
        case CHAN_TYPE_分型:
            释放分型((分型 *) 对象);
            break;
        case CHAN_TYPE_缺口:
            释放缺口((缺口 *) 对象);
            break;
        case CHAN_TYPE_虚线:
            释放虚线((虚线 *) 对象);
            break;
        case CHAN_TYPE_中枢:
            释放中枢((中枢 *) 对象);
            break;
        case CHAN_TYPE_线段特征:
            释放线段特征((线段特征 *) 对象);
            break;
        case CHAN_TYPE_特征分型:
            释放特征分型((特征分型 *) 对象);
            break;
        case CHAN_TYPE_平滑异同移动平均线:
            释放平滑异同移动平均线((平滑异同移动平均线 *) 对象);
            break;
        case CHAN_TYPE_相对强弱指数:
            释放相对强弱指数((相对强弱指数 *) 对象);
            break;
        case CHAN_TYPE_随机指标:
            释放随机指标((随机指标 *) 对象);
            break;
        case CHAN_TYPE_缠论配置:
            释放缠论配置((缠论配置 *) 对象);
            break;
        case CHAN_TYPE_观察者:
            释放观察者((观察者 *) 对象);
            break;
        case CHAN_TYPE_K线合成器:
            释放K线合成器((K线合成器 *) 对象);
            break;
        case CHAN_TYPE_立体分析器:
            释放立体分析器((立体分析器 *) 对象);
            break;
        default:
            break;
    }
    尝试回收(对象);
}

/* ================================================================
 * 动态数组
 * ================================================================ */

void 动态数组_初始化(动态数组 *arr, size_t 初始容量) {
    if (初始容量 == 0) {
        初始容量 = 4;
    }
    arr->数据 = malloc(初始容量 * sizeof(void *));
    if (!arr->数据) {
        perror("动态数组_初始化");
        chan_oom_handler(初始容量 * sizeof(void *));
        return;
    }
    arr->长度 = 0;
    arr->容量 = 初始容量;
}

void 动态数组_追加(动态数组 *arr, void *基础序列) {
    if (arr->长度 >= arr->容量) {
        if (arr->容量 > SIZE_MAX / 2) {
            perror("动态数组_追加");
            chan_oom_handler(SIZE_MAX);
            return;
        }
        size_t 新容量 = arr->容量 * 2;
        void **新数据 = realloc(arr->数据, 新容量 * sizeof(void *));
        if (!新数据) {
            perror("动态数组_追加");
            chan_oom_handler(新容量 * sizeof(void *));
            return;
        }
        arr->数据 = 新数据;
        arr->容量 = 新容量;
    }
    arr->数据[arr->长度++] = 基础序列;
}

void *动态数组_弹出(动态数组 *arr) {
    if (arr->长度 == 0) {
        return NULL;
    }
    return arr->数据[--arr->长度];
}

void *动态数组_获取(动态数组 *arr, size_t 索引) {
    if (索引 >= arr->长度) {
        return NULL;
    }
    return arr->数据[索引];
}

void 动态数组_设置(动态数组 *arr, size_t 索引, void *基础序列) {
    if (索引 < arr->长度) {
        arr->数据[索引] = 基础序列;
    }
}

size_t 动态数组_查找(动态数组 *arr, void *基础序列) {
    for (size_t i = 0; i < arr->长度; i++) {
        if (arr->数据[i] == 基础序列) {
            return i;
        }
    }
    return (size_t) - 1;
}

bool 动态数组_截取(动态数组 *序列, void *始, void *终, 动态数组 *结果) {
    size_t 始_idx = 动态数组_查找(序列, 始);
    size_t 终_idx = 动态数组_查找(序列, 终);
    if (始_idx == (size_t)-1 || 终_idx == (size_t)-1 || 始_idx > 终_idx) {
        return false;
    }
    动态数组_初始化(结果, 终_idx - 始_idx + 1);
    for (size_t i = 始_idx; i <= 终_idx; i++) {
        弱引用_数组追加(结果, 动态数组_获取(序列, i));
    }
    return true;
}

void 动态数组_释放(动态数组 *arr, bool 释放元素) {
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

/* ================================================================
 *  弱引用操作 — 全局池是唯一所有者，所有对象间引用均为弱引用
 * ================================================================ */

/* 条件回收：已销毁 + 弱引用归零 → 压入类型空闲链 */
static void 尝试回收(void *obj) {
    if (!obj) return;
    if (!__atomic_load_n(&((对象头结构 *)obj)->已销毁, __ATOMIC_ACQUIRE)) return;
    if (__atomic_load_n(&((对象头结构 *)obj)->弱引用计数, __ATOMIC_ACQUIRE) != 0) return;
    对象类型 t = 对象类型_取(obj);
    ((对象头结构 *)obj)->空闲链下一项 = 类型空闲链[t];
    类型空闲链[t] = obj;
}

/* 独立指针字段弱引用设置：
 *   旧目标弱引用计数--，新目标弱引用计数++
 *   用法：弱引用_设置(fractal, 左, new_target)
 *   弱引用计数操作均使用 __ATOMIC_RELAXED：仅用于验证，无数序依赖 */
#define 弱引用_设置(ptr, field, target) do { \
        void* _弱设_t = (void*)(target); \
        if ((ptr)->field && !__atomic_load_n(&((对象头结构*)((ptr)->field))->已销毁, __ATOMIC_ACQUIRE)) \
            __atomic_fetch_sub(&((对象头结构*)((ptr)->field))->弱引用计数, 1, __ATOMIC_RELAXED); \
        (ptr)->field = _弱设_t; \
        if (_弱设_t) \
            __atomic_fetch_add(&((对象头结构*)(_弱设_t))->弱引用计数, 1, __ATOMIC_RELAXED); \
    } while(0)

/* 手动弱引用操作（用于手动平衡场景） */
#define 弱引用_手动增加(ptr) do { __atomic_fetch_add(&((对象头结构*)(ptr))->弱引用计数, 1, __ATOMIC_RELAXED); } while(0)
#define 弱引用_手动减少(ptr) do { \
    __atomic_fetch_sub(&((对象头结构*)(ptr))->弱引用计数, 1, __ATOMIC_RELAXED); \
    尝试回收(ptr); \
} while(0)

/* 动态数组弱引用操作 — 所有 ++/-- 使用原子操作 */
void 弱引用_数组追加(动态数组 *arr, void *基础序列) {
    动态数组_追加(arr, 基础序列);
    if (基础序列) {
        __atomic_fetch_add(&((对象头结构 *) 基础序列)->弱引用计数, 1, __ATOMIC_RELAXED);
    }
}

void 弱引用_数组设置(动态数组 *arr, size_t i, void *新元素) {
    void *旧 = 动态数组_获取(arr, i);
    if (旧) {
        __atomic_fetch_sub(&((对象头结构 *) 旧)->弱引用计数, 1, __ATOMIC_RELAXED);
        尝试回收(旧);
    }
    动态数组_设置(arr, i, 新元素);
    if (新元素) {
        __atomic_fetch_add(&((对象头结构 *) 新元素)->弱引用计数, 1, __ATOMIC_RELAXED);
    }
}

void *弱引用_数组弹出(动态数组 *arr) {
    void *尾 = 动态数组_弹出(arr);
    if (尾) {
        __atomic_fetch_sub(&((对象头结构 *) 尾)->弱引用计数, 1, __ATOMIC_RELAXED);
        尝试回收(尾);
    }
    return 尾;
}

void 弱引用_数组清除(动态数组 *arr) {
    for (size_t i = 0; i < arr->长度; i++) {
        void *e = arr->数据[i];
        if (e && !__atomic_load_n(&((对象头结构 *) e)->已销毁, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_sub(&((对象头结构 *) e)->弱引用计数, 1, __ATOMIC_RELAXED);
        }
    }
    free(arr->数据);
    arr->数据 = NULL;
    arr->长度 = 0;
    arr->容量 = 0;
}

/* 弱引用_数组清空: 递减弱引用计数并清空数组，但保留已分配缓冲区 */
void 弱引用_数组清空(动态数组 *arr) {
    for (size_t i = 0; i < arr->长度; i++) {
        void *e = arr->数据[i];
        if (e && !__atomic_load_n(&((对象头结构 *) e)->已销毁, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_sub(&((对象头结构 *) e)->弱引用计数, 1, __ATOMIC_RELAXED);
        }
    }
    arr->长度 = 0;
}

/* ================================================================
 * 工具函数
 * ================================================================ */

time_t 转化为时间戳_数字(time_t ts) {
    return ts;
}

time_t 转化为时间戳(const char *ts_str) {
    struct tm tm_buf = {0};
    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
    if (sscanf(ts_str, "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s) == 6) {
        tm_buf.tm_year = y - 1900;
        tm_buf.tm_mon = m - 1;
        tm_buf.tm_mday = d;
        tm_buf.tm_hour = h;
        tm_buf.tm_min = min;
        tm_buf.tm_sec = s;
        tm_buf.tm_isdst = -1;
        return mktime(&tm_buf);
    }
    return 0;
}

相对方向 相对方向_翻转(相对方向 dir) {
    switch (dir) {
        case 相对方向_向上:
            return 相对方向_向下;
        case 相对方向_向下:
            return 相对方向_向上;
        case 相对方向_向下缺口:
            return 相对方向_向上缺口;
        case 相对方向_向上缺口:
            return 相对方向_向下缺口;
        case 相对方向_衔接向上:
            return 相对方向_衔接向下;
        case 相对方向_衔接向下:
            return 相对方向_衔接向上;
        case 相对方向_顺:
            return 相对方向_逆;
        case 相对方向_逆:
            return 相对方向_顺;
        default:
            return dir;
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
    if (前高 == 后高 && 前低 == 后低) {
        return 相对方向_同;
    }
    if (前高 > 后高 && 前低 > 后低) {
        if (前低 == 后高) {
            return 相对方向_衔接向下;
        }
        if (前低 > 后高) {
            return 相对方向_向下缺口;
        }
        return 相对方向_向下;
    }
    if (前高 < 后高 && 前低 < 后低) {
        if (前高 == 后低) {
            return 相对方向_衔接向上;
        }
        if (前高 < 后低) {
            return 相对方向_向上缺口;
        }
        return 相对方向_向上;
    }
    if (前高 >= 后高 && 前低 <= 后低) {
        return 相对方向_顺;
    }
    if (前高 <= 后高 && 前低 >= 后低) {
        return 相对方向_逆;
    }
    fprintf(stderr, "相对方向_分析: 无法识别 (%f,%f) vs (%f,%f)\n", 前高, 前低, 后高, 后低);
    return 相对方向_同;
}

/* ================================================================
 * 买卖点类型 辅助函数
 * ================================================================ */

bool 买卖点类型_是买点(买卖点类型 t) {
    switch (t) {
        case 买卖点类型_一买:
        case 买卖点类型_二买:
        case 买卖点类型_三买:
        case 买卖点类型_T1买:
        case 买卖点类型_T1P买:
        case 买卖点类型_T2买:
        case 买卖点类型_T2S买:
        case 买卖点类型_T3A买:
        case 买卖点类型_T3B买:
            return true;
        default:
            return false;
    }
}

bool 买卖点类型_是卖点(买卖点类型 t) {
    switch (t) {
        case 买卖点类型_一卖:
        case 买卖点类型_二卖:
        case 买卖点类型_三卖:
        case 买卖点类型_T1卖:
        case 买卖点类型_T1P卖:
        case 买卖点类型_T2卖:
        case 买卖点类型_T2S卖:
        case 买卖点类型_T3A卖:
        case 买卖点类型_T3B卖:
            return true;
        default:
            return false;
    }
}

/* ================================================================
 * 缠论配置
 * ================================================================ */

缠论配置 *缠论配置_新建(void) {
    缠论配置 *c = 分配(sizeof(缠论配置), CHAN_TYPE_缠论配置);
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

缠论配置 *缠论配置_不推送(void) {
    缠论配置 *c = 缠论配置_新建();
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

缠论配置 *缠论配置_复制(const 缠论配置 *src) {
    缠论配置 *dst = 分配(sizeof(缠论配置), CHAN_TYPE_缠论配置);
    size_t header_size = sizeof(对象头结构);
    memcpy((char *)dst + header_size,
           (const char *)src + header_size,
           sizeof(缠论配置) - header_size);
    return dst;
}

void 释放缠论配置(缠论配置 *obj) {
    (void) obj;
}

/* ================================================================
 * 缺口
 * ================================================================ */

缺口 *缺口_新建(double 高, double 低) {
    assert(高 > 低);
    缺口 *q = 分配(sizeof(缺口), CHAN_TYPE_缺口);
    q->高 = 高;
    q->低 = 低;
    return q;
}

void 释放缺口(缺口 *obj) {
    (void) obj;
}

/* ================================================================
 * 指标取值
 * ================================================================ */

double 指标_K线取值(K线 *k线, const char *计算方式) {
    if (strcmp(计算方式, "开") == 0) {
        return k线->开盘价;
    }
    if (strcmp(计算方式, "高") == 0) {
        return k线->高;
    }
    if (strcmp(计算方式, "低") == 0) {
        return k线->低;
    }
    if (strcmp(计算方式, "收") == 0) {
        return k线->收盘价;
    }
    if (strcmp(计算方式, "高低均值") == 0) {
        return (k线->高 + k线->低) / 2.0;
    }
    if (strcmp(计算方式, "高低收均值") == 0) {
        return (k线->高 + k线->低 + k线->收盘价) / 3.0;
    }
    if (strcmp(计算方式, "开高低收均值") == 0) {
        return (k线->高 + k线->低 + k线->开盘价 + k线->收盘价) / 4.0;
    }
    return k线->收盘价;
}

/* ================================================================
 * 平滑异同移动平均线 (MACD)
 * ================================================================ */

static double macd_平滑系数(int 周期) {
    return 2.0 / (周期 + 1.0);
}

平滑异同移动平均线 *平滑异同移动平均线_首次计算(double 收盘价, time_t 时间,
        int 快线周期, int 慢线周期, int 信号周期) {
    平滑异同移动平均线 *m = 分配(sizeof(平滑异同移动平均线), CHAN_TYPE_平滑异同移动平均线);
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

平滑异同移动平均线 *平滑异同移动平均线_首次计算_K线(K线 *k线, const char *计算方式,
        int 快线, int 慢线, int 信号) {
    return 平滑异同移动平均线_首次计算(指标_K线取值(k线, 计算方式), k线->时间戳, 快线, 慢线, 信号);
}

平滑异同移动平均线 *平滑异同移动平均线_增量计算(平滑异同移动平均线 *前, double 收盘价, time_t 时间) {
    平滑异同移动平均线 *m = 分配(sizeof(平滑异同移动平均线), CHAN_TYPE_平滑异同移动平均线);
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

平滑异同移动平均线 *平滑异同移动平均线_增量计算_K线(平滑异同移动平均线 *前, K线 *k线, const char *计算方式) {
    return 平滑异同移动平均线_增量计算(前, 指标_K线取值(k线, 计算方式), k线->时间戳);
}

void 释放平滑异同移动平均线(平滑异同移动平均线 *obj) {
    (void) obj;
}

/* ================================================================
 * 相对强弱指数 (RSI)
 * ================================================================ */

相对强弱指数 *相对强弱指数_首次计算(double 收盘价, time_t 时间, int 周期,
        double 超买, double 超卖, int sma周期, bool 有sma) {
    相对强弱指数 *r = 分配(sizeof(相对强弱指数), CHAN_TYPE_相对强弱指数);
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

相对强弱指数 *相对强弱指数_首次计算_K线(K线 *k线, const char *计算方式,
        int 周期, double 超买, double 超卖, int sma) {
    return 相对强弱指数_首次计算(指标_K线取值(k线, 计算方式), k线->时间戳, 周期, 超买, 超卖, sma, sma > 0);
}

相对强弱指数 *相对强弱指数_增量计算(相对强弱指数 *前, double 收盘价, time_t 时间) {
    相对强弱指数 *r = 分配(sizeof(相对强弱指数), CHAN_TYPE_相对强弱指数);
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
        double *新队列 = malloc(新长 * sizeof(double));
        if (!新队列) {
            解引用(r);
            return NULL;
        }
        if (前->RSI历史队列) {
            memcpy(新队列, 前->RSI历史队列, 前->RSI历史队列_长度 * sizeof(double));
        }
        新队列[新长 - 1] = r->RSI;
        if (新长 > (size_t) 前->RSI_SMA周期) {
            /* 移除最早的元素 (pop(0)) */
            for (size_t i = 1; i < 新长; i++) {
                新队列[i - 1] = 新队列[i];
            }
            新长--;
        }
        r->RSI历史队列 = 新队列;
        r->RSI历史队列_长度 = 新长;
        if (新长 > 0) {
            double sum = 0;
            for (size_t i = 0; i < 新长; i++) {
                sum += 新队列[i];
            }
            r->RSI_SMA = sum / 新长;
            r->有RSI_SMA = true;
        }
    }
    return r;
}

相对强弱指数 *相对强弱指数_增量计算_K线(相对强弱指数 *前, K线 *k线, const char *计算方式) {
    return 相对强弱指数_增量计算(前, 指标_K线取值(k线, 计算方式), k线->时间戳);
}

void 释放相对强弱指数(相对强弱指数 *obj) {
    free(obj->RSI历史队列);
}

/* ================================================================
 * 随机指标 (KDJ)
 * ================================================================ */

随机指标 *随机指标_首次计算(double 高, double 低, double 收, time_t 时间,
                                        int N, int M1, int M2, double 超买, double 超卖) {
    随机指标 *k = 分配(sizeof(随机指标), CHAN_TYPE_随机指标);
    k->时间戳 = 时间;
    k->最高价 = 高;
    k->最低价 = 低;
    k->收盘价 = 收;
    k->N = N;
    k->M1 = M1;
    k->M2 = M2;
    k->超买阈值 = 超买;
    k->超卖阈值 = 超卖;
    k->有RSV = k->有K = k->有D = k->有J = false;
    k->RSV = k->K = k->D = k->J = 0;
    k->历史最高价队列 = malloc(sizeof(double));
    if (!k->历史最高价队列) {
        解引用(k);
        return NULL;
    }
    k->历史最高价队列[0] = 高;
    k->历史最高价队列_长度 = 1;
    k->历史最低价队列 = malloc(sizeof(double));
    if (!k->历史最低价队列) {
        解引用(k);
        return NULL;
    }
    k->历史最低价队列[0] = 低;
    k->历史最低价队列_长度 = 1;
    k->有前RSV = k->有前K = k->有前D = false;
    return k;
}

随机指标 *随机指标_首次计算_K线(K线 *k线, const char *计算方式,
        int N, int M1, int M2, double 超买, double 超卖) {
    (void) 计算方式;
    return 随机指标_首次计算(k线->高, k线->低, k线->收盘价, k线->时间戳, N, M1, M2, 超买, 超卖);
}

随机指标 *随机指标_增量计算(随机指标 *前, double 高, double 低, double 收, time_t 时间) {
    随机指标 *k = 分配(sizeof(随机指标), CHAN_TYPE_随机指标);
    k->时间戳 = 时间;
    k->最高价 = 高;
    k->最低价 = 低;
    k->收盘价 = 收;
    k->N = 前->N;
    k->M1 = 前->M1;
    k->M2 = 前->M2;
    k->超买阈值 = 前->超买阈值;
    k->超卖阈值 = 前->超卖阈值;

    /* 复制并更新历史队列 */
    size_t 新长高 = 前->历史最高价队列_长度 + 1;
    k->历史最高价队列 = malloc(新长高 * sizeof(double));
    if (!k->历史最高价队列) {
        解引用(k);
        return NULL;
    }
    memcpy(k->历史最高价队列, 前->历史最高价队列, 前->历史最高价队列_长度 * sizeof(double));
    k->历史最高价队列[新长高 - 1] = 高;
    if (新长高 > (size_t) 前->N) {
        for (size_t i = 1; i < 新长高; i++) {
            k->历史最高价队列[i - 1] = k->历史最高价队列[i];
        }
        新长高--;
    }
    k->历史最高价队列_长度 = 新长高;

    size_t 新长低 = 前->历史最低价队列_长度 + 1;
    k->历史最低价队列 = malloc(新长低 * sizeof(double));
    if (!k->历史最低价队列) {
        解引用(k);
        return NULL;
    }
    memcpy(k->历史最低价队列, 前->历史最低价队列, 前->历史最低价队列_长度 * sizeof(double));
    k->历史最低价队列[新长低 - 1] = 低;
    if (新长低 > (size_t) 前->N) {
        for (size_t i = 1; i < 新长低; i++) {
            k->历史最低价队列[i - 1] = k->历史最低价队列[i];
        }
        新长低--;
    }
    k->历史最低价队列_长度 = 新长低;

    /* 计算RSV */
    if (新长高 == (size_t) 前->N && 新长低 == (size_t) 前->N) {
        double hmax = k->历史最高价队列[0];
        double lmin = k->历史最低价队列[0];
        for (size_t i = 1; i < 新长高; i++) if (k->历史最高价队列[i] > hmax) {
                hmax = k->历史最高价队列[i];
            }
        for (size_t i = 1; i < 新长低; i++) if (k->历史最低价队列[i] < lmin) {
                lmin = k->历史最低价队列[i];
            }
        if (hmax != lmin) {
            k->RSV = (收 - lmin) / (hmax - lmin) * 100.0;
        }
        else {
            k->RSV = 50.0;
        }
        k->有RSV = true;
    } else {
        k->有RSV = false;
    }

    /* K值 */
    if (k->有RSV) {
        if (!前->有K) {
            k->K = k->RSV;
        }
        else {
            k->K = (前->K * (前->M1 - 1) + k->RSV) / 前->M1;
        }
        k->有K = true;
    } else {
        k->K = 前->K;
        k->有K = 前->有K;
    }

    /* D值 */
    if (k->有K) {
        if (!前->有D) {
            k->D = k->K;
        }
        else {
            k->D = (前->D * (前->M2 - 1) + k->K) / 前->M2;
        }
        k->有D = true;
    } else {
        k->D = 前->D;
        k->有D = 前->有D;
    }

    /* J值 */
    if (k->有K && k->有D) {
        k->J = 3.0 * k->K - 2.0 * k->D;
        k->有J = true;
    }

    k->有前RSV = k->有RSV;
    k->前一个RSV = k->RSV;
    k->有前K = k->有K;
    k->前一个K = k->K;
    k->有前D = k->有D;
    k->前一个D = k->D;

    return k;
}

随机指标 *随机指标_增量计算_K线(随机指标 *前, K线 *k线, const char *计算方式) {
    (void) 计算方式;
    return 随机指标_增量计算(前, k线->高, k线->低, k线->收盘价, k线->时间戳);
}

void 释放随机指标(随机指标 *obj) {
    free(obj->历史最高价队列);
    free(obj->历史最低价队列);
}

/* ================================================================
 * K线
 * ================================================================ */

K线 *K线_新建(const char *标识, int 序号, int 周期, time_t 时间戳,
                  double 开盘价, double 高, double 低, double 收盘价, double 成交量) {
    K线 *k = 分配(sizeof(K线), CHAN_TYPE_K线);
    strncpy(k->标识, 标识, 63);
    k->标识[63] = '\0';
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

K线 *K线_创建普K(const char *标识, time_t 时间戳, double 开盘价, double 高,
                      double 低, double 收盘价, double 成交量, int 序号, int 周期) {
    return K线_新建(标识, 序号, 周期, 时间戳, 开盘价, 高, 低, 收盘价, 成交量);
}

相对方向 K线_方向(K线 *k线) {
    return k线->开盘价 < k线->收盘价 ? 相对方向_向上 : 相对方向_向下;
}

K线 *K线_读取大端字节数组(const uint8_t *字节组, int 周期, const char *标识) {
    /* 解包 >6d (6个 big-endian double) */
    double vals[6];
    for (int i = 0; i < 6; i++) {
        uint64_t raw = 0;
        for (int j = 0; j < 8; j++) {
            raw = (raw << 8) | 字节组[i * 8 + j];
        }
        uint64_t sign_bit = raw >> 63;
        int64_t exponent = ((raw >> 52) & 0x7FF) - 1023;
        uint64_t mantissa = raw & 0xFFFFFFFFFFFFFULL;
        if (exponent == -1023) {
            vals[i] = 0.0;
        } else {
            double value = 1.0 + (double) mantissa / (double) (1ULL << 52);
            vals[i] = ldexp(value, (int) exponent);
            if (sign_bit) {
                vals[i] = -vals[i];
            }
        }
    }
    return K线_新建(标识, 0, 周期, (time_t) vals[0],
                       vals[1], vals[2], vals[3], vals[4], vals[5]);
}

void K线_保存到DAT文件(const char *路径, K线 **序列, size_t 长度) {
    FILE *f = fopen(路径, "wb");
    if (!f) {
        perror(路径);
        return;
    }
    for (size_t i = 0; i < 长度; i++) {
        double vals[6] = {
            (double) 序列[i]->时间戳,
            序列[i]->开盘价, 序列[i]->高, 序列[i]->低,
            序列[i]->收盘价, 序列[i]->成交量
        };
        for (int j = 0; j < 6; j++) {
            uint64_t raw;
            memcpy(&raw, &vals[j], 8);
            /* 转为大端 */
            uint8_t be[8];
            for (int k = 7; k >= 0; k--) {
                be[k] = raw & 0xFF;
                raw >>= 8;
            }
            fwrite(be, 1, 8, f);
        }
    }
    fclose(f);
}

void K线_获取MACD(K线 **序列, size_t 长度, K线 *始, K线 *终,
                     double *阳, double *阴, double *合, double *总) {
    *阳 = 0;
    *阴 = 0;
    bool 开始 = false;
    for (size_t i = 0; i < 长度; i++) {
        if (序列[i] == 始) {
            开始 = true;
        }
        if (开始 && 序列[i]->macd) {
            double h = 序列[i]->macd->MACD柱;
            if (h >= 0) {
                *阳 += h;
            }
            else {
                *阴 += h;
            }
        }
        if (序列[i] == 终) {
            break;
        }
    }
    *合 = *阳 + *阴;
    *总 = *阳 + fabs(*阴);
}

void 释放K线(K线 *obj) {
    弱引用_设置(obj, macd, NULL);
    弱引用_设置(obj, rsi, NULL);
    弱引用_设置(obj, kdj, NULL);
}

/* ================================================================
 * 缠论K线
 * ================================================================ */

缠论K线 *缠论K线_创建缠K(time_t 时间戳, double 高, double 低, 相对方向 方向,
                                  分型结构 结构, int 原始序号, K线 *普k, 缠论K线 *之前) {
    assert(高 >= 低);
    缠论K线 *c = 分配(sizeof(缠论K线), CHAN_TYPE_缠论K线);
    c->时间戳 = 时间戳;
    c->高 = 高;
    c->低 = 低;
    c->方向 = 方向;
    c->分型 = 结构;
    c->原始起始序号 = 原始序号;
    c->原始结束序号 = 原始序号;
    c->标的K线 = 普k;
    strncpy(c->标识, 普k->标识, 63);
    c->标识[63] = '\0';
    c->周期 = 普k->周期;
    c->分型特征值 = 高;

    if (之前) {
        c->序号 = 之前->序号 + 1;
        if (相对方向_是否包含(相对方向_分析(之前->高, 之前->低, c->高, c->低))) {
            fprintf(stderr, "缠论K线_创建缠K: 包含关系不应出现\n");
            /* 池拥有所有权，无需解引用 */
            return NULL;
        }
    } else {
        c->序号 = 0;
    }
    return c;
}

缠论K线 *缠论K线_镜像(缠论K线 *self) {
    缠论K线 *c = 分配(sizeof(缠论K线), CHAN_TYPE_缠论K线);
    memcpy(c, self, sizeof(缠论K线));
    引用计数(c) = 1;
    return c;
}

bool 缠论K线_与MACD柱子匹配(缠论K线 *ck) {
    if (!ck->标的K线 || !ck->标的K线->macd) {
        return false;
    }
    double bar = ck->标的K线->macd->MACD柱;
    if (ck->分型 == 分型结构_底 || ck->分型 == 分型结构_下) {
        return bar < 0;
    }
    if (ck->分型 == 分型结构_顶 || ck->分型 == 分型结构_上) {
        return bar > 0;
    }
    return false;
}

bool 缠论K线_与RSI匹配(缠论K线 *ck) {
    if (!ck->标的K线 || !ck->标的K线->rsi) {
        return false;
    }
    double rsi = ck->标的K线->rsi->RSI;
    double sma = ck->标的K线->rsi->RSI_SMA;
    if (ck->分型 == 分型结构_底 || ck->分型 == 分型结构_下) {
        return rsi < sma;
    }
    if (ck->分型 == 分型结构_顶 || ck->分型 == 分型结构_上) {
        return rsi > sma;
    }
    return false;
}

bool 缠论K线_与KDJ匹配(缠论K线 *ck) {
    if (!ck->标的K线 || !ck->标的K线->kdj) {
        return false;
    }
    if (!ck->标的K线->kdj->有K || !ck->标的K线->kdj->有D) {
        return false;
    }
    double kval = ck->标的K线->kdj->K;
    double dval = ck->标的K线->kdj->D;
    if (ck->分型 == 分型结构_底 || ck->分型 == 分型结构_下) {
        return kval < dval;
    }
    if (ck->分型 == 分型结构_顶 || ck->分型 == 分型结构_上) {
        return kval > dval;
    }
    return false;
}

time_t 缠论K线_时间戳对齐(缠论K线 **基线, size_t 基线长, 缠论K线 *k线) {
    if (基线长 == 0) {
        return k线->时间戳;
    }
    for (size_t i_ = 基线长; i_ > 0; i_--) {
        size_t i = i_ - 1;
        if (基线[0]->周期 < k线->周期) {
            if (k线->时间戳 <= 基线[i]->时间戳 && 基线[i]->时间戳 <= k线->时间戳 + k线->周期) {
                if (k线->分型特征值 == 基线[i]->分型特征值) {
                    return 基线[i]->时间戳;
                }
            }
        } else {
            if (基线[i]->时间戳 <= k线->时间戳 && k线->时间戳 <= 基线[i]->时间戳 + 基线[i]->周期) {
                if (k线->分型特征值 == 基线[i]->分型特征值) {
                    return 基线[i]->时间戳;
                }
            }
        }
    }
    return k线->时间戳;
}

缠论K线 *缠论K线_兼并(缠论K线 *之前缠K, 缠论K线 *当前缠K, K线 *当前普K,
                              缠论配置 *配置, const char **模式) {
    相对方向 关系 = 相对方向_分析(当前缠K->高, 当前缠K->低, 当前普K->高, 当前普K->低);
    if (!相对方向_是否包含(关系)) {
        缠论K线 *新 = 缠论K线_创建缠K(当前普K->时间戳, 当前普K->高, 当前普K->低,
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
        *模式 = NULL;
        return NULL;
    }
    if (当前普K->序号 - 1 != 当前缠K->原始结束序号 && 当前普K->序号 != 当前缠K->原始结束序号) {
        fprintf(stderr, "缠论K线_兼并: 不可追加不连续元素\n");
        *模式 = NULL;
        return NULL;
    }

    double (*取值函数)(double, double) = fmax;
    if (之前缠K) {
        if (相对方向_是否向下(相对方向_分析(之前缠K->高, 之前缠K->低, 当前缠K->高, 当前缠K->低))) {
            取值函数 = fmin;
        }
    }

    if (关系 != 相对方向_顺) {
        当前缠K->时间戳 = 当前普K->时间戳;
        当前缠K->标的K线 = 当前普K;
    }
    当前缠K->高 = 取值函数(当前缠K->高, 当前普K->高);
    当前缠K->低 = 取值函数(当前缠K->低, 当前普K->低);
    当前缠K->原始结束序号 = 当前普K->序号;
    当前缠K->方向 = K线_方向(当前普K);

    if (之前缠K) {
        当前缠K->序号 = 之前缠K->序号 + 1;
    }

    if (配置->缠K合并替换) {
        *模式 = "替换";
        return 缠论K线_镜像(当前缠K);
    }
    *模式 = NULL;
    return NULL;
}

void 缠论K线_分析(K线 *当前K线, 动态数组 *缠K序列, 动态数组 *普K序列,
                       缠论配置 *配置, const char **状态, 分型 **形态) {
    *状态 = NULL;
    *形态 = NULL;
    当前K线->标识[0] = '\0';
    strncpy(当前K线->标识, 配置->标识, 63);

    if (普K序列->长度 == 0) {
        if (配置->计算指标) {
            弱引用_设置(当前K线, macd, 平滑异同移动平均线_首次计算_K线(当前K线, 配置->指标计算方式,
                             配置->平滑异同移动平均线_快线周期, 配置->平滑异同移动平均线_慢线周期,
                             配置->平滑异同移动平均线_信号周期));
            弱引用_设置(当前K线, rsi, 相对强弱指数_首次计算_K线(当前K线, 配置->指标计算方式,
                             配置->相对强弱指数_周期, 配置->相对强弱指数_超买阈值,
                             配置->相对强弱指数_超卖阈值, 配置->相对强弱指数_移动平均线周期));
            弱引用_设置(当前K线, kdj, 随机指标_首次计算_K线(当前K线, 配置->指标计算方式,
                             配置->随机指标_RSV周期, 配置->随机指标_K值平滑周期,
                             配置->随机指标_D值平滑周期, 配置->随机指标_超买阈值,
                             配置->随机指标_超卖阈值));
        }
        弱引用_数组追加(普K序列, 当前K线);
    } else {
        K线 *之前普K = 动态数组_获取(普K序列, 普K序列->长度 - 1);
        if (之前普K->时间戳 == 当前K线->时间戳) {
            当前K线->序号 = 之前普K->序号;

            弱引用_数组设置(普K序列, 普K序列->长度 - 1, 当前K线);
            if (配置->计算指标 && 普K序列->长度 >= 2) {
                K线 *前前 = 动态数组_获取(普K序列, 普K序列->长度 - 2);
                if (前前) {
                    弱引用_设置(当前K线, macd, 平滑异同移动平均线_增量计算_K线(前前->macd, 当前K线, 配置->指标计算方式));
                    弱引用_设置(当前K线, rsi, 相对强弱指数_增量计算_K线(前前->rsi, 当前K线, 配置->指标计算方式));
                    弱引用_设置(当前K线, kdj, 随机指标_增量计算_K线(前前->kdj, 当前K线, 配置->指标计算方式));
                }
            }
        } else {
            if (之前普K->时间戳 > 当前K线->时间戳) {
                fprintf(stderr, "缠论K线_分析: 时序错误\n");
                return;
            }
            当前K线->序号 = 之前普K->序号 + 1;
            if (配置->计算指标) {
                弱引用_设置(当前K线, macd, 平滑异同移动平均线_增量计算_K线(之前普K->macd, 当前K线, 配置->指标计算方式));
                弱引用_设置(当前K线, rsi, 相对强弱指数_增量计算_K线(之前普K->rsi, 当前K线, 配置->指标计算方式));
                弱引用_设置(当前K线, kdj, 随机指标_增量计算_K线(之前普K->kdj, 当前K线, 配置->指标计算方式));
            }
            弱引用_数组追加(普K序列, 当前K线);
        }
    }

    缠论K线 *之前缠K = NULL;
    if (缠K序列->长度 > 1) {
        之前缠K = 动态数组_获取(缠K序列, 缠K序列->长度 - 2);
    }

    if (缠K序列->长度 > 0) {
        缠论K线 *当前缠K = 动态数组_获取(缠K序列, 缠K序列->长度 - 1);
        const char *模式 = NULL;
        缠论K线 *新缠K = 缠论K线_兼并(之前缠K, 当前缠K, 当前K线, 配置, &模式);
        if (新缠K) {
            if (strcmp(模式, "添加") == 0) {
                弱引用_数组追加(缠K序列, 新缠K);
                *状态 = "创建";
            } else if (strcmp(模式, "替换") == 0) {
                弱引用_数组设置(缠K序列, 缠K序列->长度 - 1, 新缠K);
                *状态 = "替换";
            }
        } else {
            *状态 = "兼并";
        }
    } else {
        缠论K线 *新缠K = 缠论K线_创建缠K(当前K线->时间戳, 当前K线->高, 当前K线->低,
                              K线_方向(当前K线), 分型结构_散, 当前K线->序号, 当前K线, NULL);
        弱引用_数组追加(缠K序列, 新缠K);
        *状态 = "新建";
    }

    if (缠K序列->长度 < 3) {
        return;
    }

    缠论K线 *左 = 动态数组_获取(缠K序列, 缠K序列->长度 - 3);
    缠论K线 *中 = 动态数组_获取(缠K序列, 缠K序列->长度 - 2);
    缠论K线 *右 = 动态数组_获取(缠K序列, 缠K序列->长度 - 1);

    缺口 _左边 = {.高 = 左->高, .低 = 左->低};
    缺口 _中间 = {.高 = 中->高, .低 = 中->低};
    缺口 _右边 = {.高 = 右->高, .低 = 右->低};

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
        default:
            break;
    }

    *形态 = 分型_新建(左, 中, 右);
    if (结构 == 分型结构_上 || 结构 == 分型结构_下) {
        释放分型(*形态);
        *形态 = 分型_新建(中, 右, NULL);
    }
}

void 释放缠论K线(缠论K线 *obj) {
    obj->标的K线 = NULL;
}

/* ================================================================
 * 分型
 * ================================================================ */

分型结构 分型结构_分析(缺口 *左, 缺口 *中, 缺口 *右,
                                 bool 可以逆序包含, bool 忽视顺序包含) {
    if (!左 || !中 || !右) {
        return 分型结构_散;
    }

    相对方向 左中关系 = 相对方向_分析(左->高, 左->低, 中->高, 中->低);
    相对方向 中右关系 = 相对方向_分析(中->高, 中->低, 右->高, 右->低);

    switch (左中关系) {
        case 相对方向_顺:
            if (忽视顺序包含) {
                break;
            }
            fprintf(stderr, "分型结构_分析: 顺序包含 左中\n");
            return 分型结构_散;
        default:
            break;
    }
    switch (中右关系) {
        case 相对方向_顺:
            if (忽视顺序包含) {
                break;
            }
            fprintf(stderr, "分型结构_分析: 顺序包含 中右\n");
            return 分型结构_散;
        default:
            break;
    }

    bool 左中向上 = 相对方向_是否向上(左中关系) && !相对方向_是否包含(左中关系);
    bool 左中向下 = 相对方向_是否向下(左中关系) && !相对方向_是否包含(左中关系);
    bool 中右向上 = 相对方向_是否向上(中右关系) && !相对方向_是否包含(中右关系);
    bool 中右向下 = 相对方向_是否向下(中右关系) && !相对方向_是否包含(中右关系);

    if (左中向上 && 中右向上) {
        return 分型结构_上;
    }
    if (左中向上 && 中右向下) {
        return 分型结构_顶;
    }
    if (左中向上 && 中右关系 == 相对方向_逆 && 可以逆序包含) {
        return 分型结构_上;
    }

    if (左中向下 && 中右向上) {
        return 分型结构_底;
    }
    if (左中向下 && 中右向下) {
        return 分型结构_下;
    }
    if (左中向下 && 中右关系 == 相对方向_逆 && 可以逆序包含) {
        return 分型结构_下;
    }

    if (左中关系 == 相对方向_逆 && 中右向上 && 可以逆序包含) {
        return 分型结构_底;
    }
    if (左中关系 == 相对方向_逆 && 中右向下 && 可以逆序包含) {
        return 分型结构_顶;
    }
    if (左中关系 == 相对方向_逆 && 中右关系 == 相对方向_逆 && 可以逆序包含) {
        return 分型结构_散;
    }

    fprintf(stderr, "分型结构_分析: 未匹配 可以逆序=%d 左中=%d 中右=%d\n",
            可以逆序包含, 左中关系, 中右关系);
    return 分型结构_散;
}

分型 *分型_新建(缠论K线 *左, 缠论K线 *中, 缠论K线 *右) {
    if (左 && 右) {
        assert(左->时间戳 < 中->时间戳 && 中->时间戳 < 右->时间戳);
    }
    分型 *f = 分配(sizeof(分型), CHAN_TYPE_分型);
    弱引用_设置(f, 左, 左);
    弱引用_设置(f, 中, 中);
    弱引用_设置(f, 右, 右);
    f->结构 = 中->分型;
    f->时间戳 = 中->时间戳;
    f->分型特征值 = 中->分型特征值;
    return f;
}

bool 分型_判断分型(分型 *左, 分型 *右, const char *模式) {
    (void) 模式;
    if (左 == 右) {
        return true;
    }
    if (左 && 右 && 左->中 == 右->中) {
        return true;
    }
    return false;
}

bool 分型_与MACD柱子分型匹配(分型 *f) {
    if (!f->右 || !f->左) {
        return false;
    }
    if (!f->左->标的K线 || !f->左->标的K线->macd
            || !f->中->标的K线 || !f->中->标的K线->macd
            || !f->右->标的K线 || !f->右->标的K线->macd) {
        return false;
    }
    double 左_柱 = f->左->标的K线->macd->MACD柱;
    double 中_柱 = f->中->标的K线->macd->MACD柱;
    double 右_柱 = f->右->标的K线->macd->MACD柱;
    if (f->结构 == 分型结构_底) {
        return 左_柱 > 中_柱 && 中_柱 < 右_柱;
    }
    if (f->结构 == 分型结构_顶) {
        return 左_柱 < 中_柱 && 中_柱 > 右_柱;
    }
    return false;
}

分型 *分型_从缠K序列中获取分型(缠论K线 **序列, size_t 长度, 缠论K线 *中) {
    for (size_t i = 0; i < 长度; i++) {
        if (序列[i] == 中) {
            if (i > 0 && i + 1 < 长度) {
                return 分型_新建(序列[i - 1], 中, 序列[i + 1]);
            }
            else if (i > 0) {
                return 分型_新建(序列[i - 1], 中, NULL);
            }
            break;
        }
    }
    return NULL;
}

void 分型_向序列中添加(动态数组 *分型序列, 分型 *当前分型) {
    if (分型序列->长度 == 0 && (当前分型->结构 != 分型结构_顶 && 当前分型->结构 != 分型结构_底)) {
        fprintf(stderr, "分型_向序列中添加: 首次添加不为顶底\n");
        return;
    }
    if (分型序列->长度 > 0) {
        分型 *末 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (末->结构 == 当前分型->结构) {
            fprintf(stderr, "分型_向序列中添加: 分型相同无法添加\n");
            return;
        }
        if (末->右 == NULL) {
            fprintf(stderr, "分型_向序列中添加: 分型异常\n");
        }
    }
    弱引用_数组追加(分型序列, 当前分型);
}

void 释放分型(分型 *obj) {
    弱引用_设置(obj, 左, NULL);
    弱引用_设置(obj, 中, NULL);
    弱引用_设置(obj, 右, NULL);
}

bool 分型_关系组(分型 *f, 相对方向 *左中, 相对方向 *中右, 相对方向 *左右) {
    if (!f->左 || !f->右) {
        return false;
    }
    *左中 = 相对方向_分析(f->左->高, f->左->低, f->中->高, f->中->低);
    *中右 = 相对方向_分析(f->中->高, f->中->低, f->右->高, f->右->低);
    *左右 = 相对方向_分析(f->左->高, f->左->低, f->右->高, f->右->低);
    return true;
}

const char *分型_强度(分型 *f) {
    if (f->结构 != 分型结构_底 && f->结构 != 分型结构_顶) {
        return "未知";
    }
    if (!f->右 || !f->左) {
        return "未知";
    }

    相对方向 左中, 中右, 左右;
    if (分型_关系组(f, &左中, &中右, &左右)) {
        if (f->结构 == 分型结构_底) {
            if (相对方向_是否向下(左右)) {
                return "弱";
            }
            if (相对方向_是否向上(左右)) {
                return "强";
            }
            return "中";
        } else {
            if (相对方向_是否向上(左右)) {
                return "弱";
            }
            if (相对方向_是否向下(左右)) {
                return "强";
            }
            return "中";
        }
    }

    if (f->结构 == 分型结构_底) {
        if (f->右->标的K线->收盘价 > f->左->标的K线->高) {
            return "强";
        }
        if (f->右->标的K线->收盘价 > f->中->标的K线->高) {
            return "中";
        }
        return "弱";
    } else {
        if (f->右->标的K线->收盘价 < f->左->标的K线->低) {
            return "强";
        }
        if (f->右->标的K线->收盘价 < f->中->标的K线->低) {
            return "中";
        }
        return "弱";
    }
}

/* ================================================================
 * 虚线
 * ================================================================ */

虚线 *虚线_新建(int 序号, const char *标识, 分型 *文, 分型 *武, int 级别, bool 有效性) {
    虚线 *x = 分配(sizeof(虚线), CHAN_TYPE_虚线);
    x->序号 = 序号;
    strncpy(x->标识, 标识, 63);
    x->标识[63] = '\0';
    x->级别 = 级别;
    弱引用_设置(x, 文, 文);
    弱引用_设置(x, 武, 武);
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

虚线 *虚线_创建笔(分型 *文, 分型 *武, bool 有效性) {
    return 虚线_新建(0, "笔", 文, 武, 1, 有效性);
}

虚线 *虚线_创建线段(动态数组 *虚线序列) {
    虚线 *首 = 动态数组_获取(虚线序列, 0);
    虚线 *末 = 动态数组_获取(虚线序列, 虚线序列->长度 - 1);
    char buf[128];
    if (strcmp(首->标识, "笔") != 0) {
        snprintf(buf, 127, "线段<%s>", 首->标识);
    }
    else {
        strcpy(buf, "线段");
    }

    虚线 *段 = 虚线_新建(0, buf, 首->文, 末->武, 首->级别 + 1, true);
    /* 初始化特征序列为 [NULL, NULL, NULL] */
    弱引用_数组追加(&段->特征序列, NULL);
    弱引用_数组追加(&段->特征序列, NULL);
    弱引用_数组追加(&段->特征序列, NULL);

    for (size_t i = 0; i < 虚线序列->长度; i++) {
        弱引用_数组追加(&段->基础序列, 动态数组_获取(虚线序列, i));
    }
    return 段;
}

相对方向 虚线_方向(虚线 *self) {
    if (self->文->结构 == 分型结构_顶 && (self->武->结构 == 分型结构_底 || self->武->结构 == 分型结构_下)) {
        return 相对方向_向下;
    }
    if (self->文->结构 == 分型结构_底 && (self->武->结构 == 分型结构_顶 || self->武->结构 == 分型结构_上)) {
        return 相对方向_向上;
    }
    fprintf(stderr, "虚线_方向: 无法识别\n");
    return 相对方向_向上;
}

double 虚线_高(虚线 *self) {
    if (虚线_方向(self) == 相对方向_向上) {
        return self->武->中->高;
    }
    return self->文->中->高;
}

double 虚线_低(虚线 *self) {
    if (虚线_方向(self) == 相对方向_向下) {
        return self->武->中->低;
    }
    return self->文->中->低;
}

bool 虚线_之前是(虚线 *self, 虚线 *之前) {
    if (strcmp(self->标识, 之前->标识) != 0) {
        return false;
    }
    return 分型_判断分型(之前->武, self->文, "中");
}

bool 虚线_之后是(虚线 *self, 虚线 *之后) {
    if (strcmp(self->标识, 之后->标识) != 0) {
        return false;
    }
    return 分型_判断分型(self->武, 之后->文, "中");
}

void 虚线_获取普K序列(虚线 *self, 观察者 *obs, K线 ***out, size_t *out_len) {
    size_t si = 动态数组_查找(&obs->普通K线序列, self->文->中->标的K线);
    size_t ei = 动态数组_查找(&obs->普通K线序列, self->武->中->标的K线);
    *out_len = (si != (size_t) - 1 && ei != (size_t) - 1 && ei >= si) ? (ei - si + 1) : 0;
    *out = (*out_len > 0) ? (K线 **) (obs->普通K线序列.数据 + si) : NULL;
}

void 虚线_获取缠K序列(虚线 *self, 观察者 *obs, 缠论K线 ***out, size_t *out_len) {
    size_t si = 动态数组_查找(&obs->缠论K线序列, self->文->中);
    size_t ei = 动态数组_查找(&obs->缠论K线序列, self->武->中);
    *out_len = (si != (size_t) - 1 && ei != (size_t) - 1 && ei >= si) ? (ei - si + 1) : 0;
    *out = (*out_len > 0) ? (缠论K线 **) (obs->缠论K线序列.数据 + si) : NULL;
}

void 释放虚线(虚线 *obj) {
    弱引用_数组清除(&obj->基础序列);
    弱引用_数组清除(&obj->特征序列);

    /* 内部中枢预释放：清除所有弱引用并释放序列内存 */
#define 中枢序列_预释放(seq) 弱引用_数组清除(&(seq))
    中枢序列_预释放(obj->实_中枢序列);
    中枢序列_预释放(obj->虚_中枢序列);
    中枢序列_预释放(obj->合_中枢序列);
#undef 中枢序列_预释放

    /* 所有弱引用指针字段 */
    弱引用_设置(obj, 文, NULL);
    弱引用_设置(obj, 武, NULL);
    弱引用_设置(obj, 确认K线, NULL);
    弱引用_设置(obj, 前一缺口, NULL);
    弱引用_设置(obj, 前一结束位置, NULL);
}

/* ================================================================
 * 格式化输出辅助函数（与 Python str() 输出保持一致）
 * ================================================================ */

static const char *_方向到Python名称(相对方向 d) {
    switch (d) {
        case 相对方向_向上:
            return "相对方向.向上";
        case 相对方向_向下:
            return "相对方向.向下";
        case 相对方向_向上缺口:
            return "相对方向.向上缺口";
        case 相对方向_向下缺口:
            return "相对方向.向下缺口";
        case 相对方向_衔接向上:
            return "相对方向.衔接向上";
        case 相对方向_衔接向下:
            return "相对方向.衔接向下";
        case 相对方向_顺:
            return "相对方向.顺";
        case 相对方向_逆:
            return "相对方向.逆";
        case 相对方向_同:
            return "相对方向.同";
        default:
            return "相对方向.未知";
    }
}

static const char *_分型结构到Python名称(分型结构 s) {
    switch (s) {
        case 分型结构_顶:
            return "顶";
        case 分型结构_底:
            return "底";
        case 分型结构_上:
            return "上";
        case 分型结构_下:
            return "下";
        case 分型结构_散:
            return "散";
        default:
            return "未知";
    }
}

static void _utf8_安全截断(char *buf) {
    size_t len = strlen(buf);
    if (len == 0) {
        return;
    }
    size_t i = len;
    int 后续字节数 = 0;
    while (i > 0) {
        i--;
        unsigned char c = (unsigned char)buf[i];
        if ((c & 0xC0) == 0x80) {
            后续字节数++;
            if (后续字节数 > 3) {
                break;
            }
        } else {
            int 预期;
            if ((c & 0x80) == 0) {
                预期 = 0;
            }
            else if ((c & 0xE0) == 0xC0) {
                预期 = 1;
            }
            else if ((c & 0xF0) == 0xE0) {
                预期 = 2;
            }
            else if ((c & 0xF8) == 0xF0) {
                预期 = 3;
            }
            else {
                buf[i] = '\0';
                return;
            }
            if (后续字节数 < 预期) {
                buf[i] = '\0';
            }
            return;
        }
    }
}

static void _缺口到字符串(缺口 *g, char *buf, size_t buf_size) {
    if (!g) {
        snprintf(buf, buf_size, "None");
        return;
    }
    snprintf(buf, buf_size, "缺口区间<%g <=> %g>", g->低, g->高);
}

static void _分型到字符串(分型 *f, char *buf, size_t buf_size) {
    if (!f) {
        snprintf(buf, buf_size, "None");
        return;
    }
    snprintf(buf, buf_size, "%s<%ld, %g, None: %s, None: %s>",
             _分型结构到Python名称(f->结构),
             (long) f->时间戳,
             f->分型特征值,
             f->左 ? "False" : "True",
             f->右 ? "False" : "True");
}

static void _虚线到字符串(虚线 *d, char *buf, size_t buf_size) {
    if (!d) {
        snprintf(buf, buf_size, "None");
        return;
    }
    char 文str[256];
    char 武str[256];
    _分型到字符串(d->文, 文str, sizeof(文str));
    _分型到字符串(d->武, 武str, sizeof(武str));

    if (strcmp(d->标识, "笔") == 0) {
        snprintf(buf, buf_size,
                 "笔(%d, %s, %s, %s, 周期: %d, 数量: %d)",
                 d->序号,
                 _方向到Python名称(虚线_方向(d)),
                 文str, 武str,
                 d->文->中->周期,
                 d->武->中->序号 - d->文->中->序号 + 1);
    } else {
        char 缺口str[128];
        _缺口到字符串(线段_获取缺口(d), 缺口str, sizeof(缺口str));

        char 确认K线str[512];
        if (d->确认K线) {
            snprintf(确认K线str, sizeof(确认K线str),
                     "%s<%d, %s, %d, %s, %ld, %g, %g>",
                     d->确认K线->标识,
                     d->确认K线->序号,
                     _分型结构到Python名称(d->确认K线->分型),
                     d->确认K线->周期,
                     _方向到Python名称(d->确认K线->方向),
                     (long) d->确认K线->时间戳,
                     d->确认K线->高, d->确认K线->低);
        } else {
            snprintf(确认K线str, sizeof(确认K线str), "None");
        }

        snprintf(buf, buf_size,
                 "%s<%d, %s, %s, %s, %s, 数量: %zu, 缺口: %s, %s>",
                 d->标识, d->序号,
                 线段_四象(d),
                 _方向到Python名称(虚线_方向(d)),
                 文str, 武str,
                 d->基础序列.长度,
                 缺口str,
                 确认K线str);
    }
}

static void _虚线数组_到字符串(动态数组 *arr, char *buf, size_t buf_size) {
    if (buf_size == 0) {
        return;
    }
    if (arr->长度 == 0) {
        snprintf(buf, buf_size, "[]");
        return;
    }
    size_t pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[");
    for (size_t i = 0; i < arr->长度; i++) {
        if (pos >= buf_size) {
            break;
        }
        虚线 *d = (虚线 *) 动态数组_获取(arr, i);
        char dstr[1536];
        _虚线到字符串(d, dstr, sizeof(dstr));
        pos += snprintf(buf + pos, buf_size - pos, "%s%s",
                        i > 0 ? ", " : "", dstr);
    }
    if (pos < buf_size) {
        snprintf(buf + pos, buf_size - pos, "]");
    } else {
        buf[buf_size - 1] = '\0';
    }
    _utf8_安全截断(buf);
}

static void _中枢_到字符串(中枢 *h, char *buf, size_t buf_size) {
    char 文str[256], 武str[256];
    _分型到字符串(中枢_文(h), 文str, sizeof(文str));
    _分型到字符串(中枢_武(h), 武str, sizeof(武str));

    char 基础str[16384];
    _虚线数组_到字符串(&h->基础序列, 基础str, sizeof(基础str));

    snprintf(buf, buf_size, "%s(%g, %g, 元素数量: %zu, %s, %s ===>>> %s)",
             h->标识,
             中枢_高(h), 中枢_低(h),
             h->基础序列.长度,
             基础str,
             文str, 武str);
}

void 中枢_到字符(中枢 *h, char *buf, size_t buf_size) {
    _中枢_到字符串(h, buf, buf_size);
}

static void _中枢数组_到字符串(动态数组 *arr, char *buf, size_t buf_size) {
    if (buf_size == 0) {
        return;
    }
    if (arr->长度 == 0) {
        snprintf(buf, buf_size, "[]");
        return;
    }
    size_t pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "[");
    for (size_t i = 0; i < arr->长度; i++) {
        if (pos >= buf_size) {
            break;
        }
        中枢 *h = (中枢 *) 动态数组_获取(arr, i);
        char hstr[20480];
        _中枢_到字符串(h, hstr, sizeof(hstr));
        pos += snprintf(buf + pos, buf_size - pos, "%s%s",
                        i > 0 ? ", " : "", hstr);
    }
    if (pos < buf_size) {
        snprintf(buf + pos, buf_size - pos, "]");
    } else {
        buf[buf_size - 1] = '\0';
    }
    _utf8_安全截断(buf);
}

void 虚线_获取数据文本(虚线 *d, char *buf, size_t buf_size) {
    if (strcmp(d->标识, "笔") == 0) {
        snprintf(buf, buf_size,
                 "%s, %d, %d, 文:(%ld,%g), 武:(%ld,%g), %s",
                 d->标识, d->序号, d->级别,
                 (long) d->文->时间戳, d->文->分型特征值,
                 (long) d->武->时间戳, d->武->分型特征值,
                 d->有效性 ? "True" : "False");
    } else {
        bool 左, 中, 右;
        线段_特征序列状态(d, &左, &中, &右);

        char 缺口str[128];
        _缺口到字符串(d->前一缺口, 缺口str, sizeof(缺口str));

        char 结束位置str[1536];
        _虚线到字符串(d->前一结束位置, 结束位置str, sizeof(结束位置str));

        char 实str[32768], 虚str[32768], 合str[32768];
        _中枢数组_到字符串(&d->实_中枢序列, 实str, sizeof(实str));
        _中枢数组_到字符串(&d->虚_中枢序列, 虚str, sizeof(虚str));
        _中枢数组_到字符串(&d->合_中枢序列, 合str, sizeof(合str));

        动态数组 前, 后, 第三;
        动态数组_初始化(&前, 2);
        动态数组_初始化(&后, 2);
        动态数组_初始化(&第三, 2);
        虚线 *贯穿伤 = NULL;
        线段_分割序列(d, NULL, &前, &后, &第三, &贯穿伤);

        char 前str[8192], 后str[8192], 三str[8192], 贯穿伤str[2048];
        _虚线数组_到字符串(&前, 前str, sizeof(前str));
        _虚线数组_到字符串(&后, 后str, sizeof(后str));
        _虚线数组_到字符串(&第三, 三str, sizeof(三str));
        _虚线到字符串(贯穿伤, 贯穿伤str, sizeof(贯穿伤str));

        snprintf(buf, buf_size,
                 "%s, %d, %d, 文:(%ld,%g), 武:(%ld,%g), %s, %zu, (%s, %s, %s), (前: %s, 后: %s, 三: %s, 伤: %s), 实: %s, 虚: %s, 合: %s, %s, %s, %s, %s",
                 d->标识, d->序号, d->级别,
                 (long) d->文->时间戳, d->文->分型特征值,
                 (long) d->武->时间戳, d->武->分型特征值,
                 d->有效性 ? "True" : "False",
                 d->基础序列.长度,
                 左 ? "True" : "False", 中 ? "True" : "False", 右 ? "True" : "False",
                 前str, 后str, 三str, 贯穿伤str,
                 实str, 虚str, 合str,
                 d->模式,
                 缺口str, 结束位置str,
                 d->短路修正 ? "True" : "False");
    }
    _utf8_安全截断(buf);
}

/* ================================================================
 * 笔
 * ================================================================ */

int 笔_获取缠K数量(动态数组 *缠K序列, 动态数组 *笔序列, 缠论配置 *配置) {
    int 实际数量 = (int) 缠K序列->长度;
    if (实际数量 >= 配置->笔内元素数量) {
        return 实际数量;
    }

    if (配置->笔弱化 && 实际数量 >= 3) {
        缠论K线 *高 = 笔_实际高点(缠K序列, 配置->笔内相同终点取舍);
        缠论K线 *低 = 笔_实际低点(缠K序列, 配置->笔内相同终点取舍);
        int 原始数量 = 1 + abs(低->标的K线->序号 - 高->标的K线->序号);
        if (原始数量 >= 配置->笔内元素数量) {
            return 配置->笔内元素数量;
        }

        if (笔序列->长度 > 0) {
            虚线 *筆 = 笔_根据缠K找笔(笔序列, 高, 1);
            if (!筆) {
                筆 = 笔_根据缠K找笔(笔序列, 低, 1);
            }
            if (筆) {
                if (虚线_方向(筆) == 相对方向_向上 && 低->低 < 虚线_低(筆))
                    if (原始数量 >= 配置->笔弱化_原始数量) {
                        return 配置->笔内元素数量;
                    }
                if (虚线_方向(筆) == 相对方向_向下 && 低->低 > 虚线_高(筆))
                    if (原始数量 >= 配置->笔弱化_原始数量) {
                        return 配置->笔内元素数量;
                    }
            }
        }
    }
    return 实际数量;
}

static int 缠K_按时间比较(const void *a, const void *b) {
    time_t ta = (*(缠论K线 **) a)->时间戳, tb = (*(缠论K线 **) b)->时间戳;
    return (ta > tb) - (ta < tb);
}

缠论K线 *笔_实际高点(动态数组 *缠K序列, bool 笔内相同终点取舍) {
    if (缠K序列->长度 == 0) {
        return NULL;
    }
    /* 找最大->高 */
    缠论K线 *max_k = 动态数组_获取(缠K序列, 0);
    for (size_t i = 1; i < 缠K序列->长度; i++) {
        缠论K线 *c = 动态数组_获取(缠K序列, i);
        if (c->高 > max_k->高) {
            max_k = c;
        }
    }
    /* 收集所有等于max高的 */
    缠论K线 **eq = malloc(缠K序列->长度 * sizeof(缠论K线 *));
    size_t eqn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线 *c = 动态数组_获取(缠K序列, i);
        if (c->高 == max_k->高) {
            eq[eqn++] = c;
        }
    }
    qsort(eq, eqn, sizeof(缠论K线 *), 缠K_按时间比较);
    缠论K线 *result = 笔内相同终点取舍 ? eq[eqn - 1] : eq[0];
    free(eq);
    return result;
}

缠论K线 *笔_实际低点(动态数组 *缠K序列, bool 笔内相同终点取舍) {
    if (缠K序列->长度 == 0) {
        return NULL;
    }
    缠论K线 *min_k = 动态数组_获取(缠K序列, 0);
    for (size_t i = 1; i < 缠K序列->长度; i++) {
        缠论K线 *c = 动态数组_获取(缠K序列, i);
        if (c->低 < min_k->低) {
            min_k = c;
        }
    }
    缠论K线 **eq = malloc(缠K序列->长度 * sizeof(缠论K线 *));
    size_t eqn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线 *c = 动态数组_获取(缠K序列, i);
        if (c->低 == min_k->低) {
            eq[eqn++] = c;
        }
    }
    qsort(eq, eqn, sizeof(缠论K线 *), 缠K_按时间比较);
    缠论K线 *result = 笔内相同终点取舍 ? eq[eqn - 1] : eq[0];
    free(eq);
    return result;
}

缠论K线 *笔_次高(动态数组 *缠K序列, bool 笔内相同终点取舍) {
    缠论K线 *actual = 笔_实际高点(缠K序列, 笔内相同终点取舍);
    /* 排除最->高 */
    缠论K线 **filtered = malloc(缠K序列->长度 * sizeof(缠论K线 *));
    size_t fn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线 *c = 动态数组_获取(缠K序列, i);
        if (c->高 != actual->高) {
            filtered[fn++] = c;
        }
    }
    /* 找次->高 */
    if (fn == 0) {
        free(filtered);
        return actual;
    }
    缠论K线 *max2 = filtered[0];
    for (size_t i = 1; i < fn; i++)
        if (filtered[i]->高 > max2->高) {
            max2 = filtered[i];
        }
    /* 收集次->高 */
    size_t eqn = 0;
    for (size_t i = 0; i < fn; i++)
        if (filtered[i]->高 == max2->高) {
            filtered[eqn++] = filtered[i];
        }
    qsort(filtered, eqn, sizeof(缠论K线 *), 缠K_按时间比较);
    缠论K线 *result = 笔内相同终点取舍 ? filtered[eqn - 1] : filtered[0];
    free(filtered);
    return result;
}

缠论K线 *笔_次低(动态数组 *缠K序列, bool 笔内相同终点取舍) {
    缠论K线 *actual = 笔_实际低点(缠K序列, 笔内相同终点取舍);
    缠论K线 **filtered = malloc(缠K序列->长度 * sizeof(缠论K线 *));
    size_t fn = 0;
    for (size_t i = 0; i < 缠K序列->长度; i++) {
        缠论K线 *c = 动态数组_获取(缠K序列, i);
        if (c->低 != actual->低) {
            filtered[fn++] = c;
        }
    }
    if (fn == 0) {
        free(filtered);
        return actual;
    }
    缠论K线 *min2 = filtered[0];
    for (size_t i = 1; i < fn; i++)
        if (filtered[i]->低 < min2->低) {
            min2 = filtered[i];
        }
    size_t eqn = 0;
    for (size_t i = 0; i < fn; i++)
        if (filtered[i]->低 == min2->低) {
            filtered[eqn++] = filtered[i];
        }
    qsort(filtered, eqn, sizeof(缠论K线 *), 缠K_按时间比较);
    缠论K线 *result = 笔内相同终点取舍 ? filtered[eqn - 1] : filtered[0];
    free(filtered);
    return result;
}

bool 笔_相对关系(虚线 *筆, 缠论配置 *配置) {
    相对方向 rel;
    if (配置->笔内起始分型包含整笔) {
        缠论K线 *ws[] = {筆->文->左, 筆->文->中, 筆->文->右};
        缠论K线 *wmax = ws[0];
        for (int i = 0; i < 3; i++) if (ws[i] && ws[i]->高 > wmax->高) {
                wmax = ws[i];
            }
        缠论K线 *wmin = ws[0];
        for (int i = 0; i < 3; i++) if (ws[i] && ws[i]->低 < wmin->低) {
                wmin = ws[i];
            }
        double wh = wmax ? wmax->高 : 0, wl = wmin ? wmin->低 : 0;

        缠论K线 *us[3] = {筆->武->左, 筆->武->中, 配置->笔内起始分型包含整笔_包括右 ? 筆->武->右 : NULL};
        缠论K线 *umax = us[0];
        for (int i = 0; i < 3; i++) if (us[i] && us[i]->高 > umax->高) {
                umax = us[i];
            }
        缠论K线 *umin = us[0];
        for (int i = 0; i < 3; i++) if (us[i] && us[i]->低 < umin->低) {
                umin = us[i];
            }
        double uh = umax ? umax->高 : 0, ul = umin ? umin->低 : 0;

        rel = 相对方向_分析(wh, wl, uh, ul);
    } else {
        rel = 相对方向_分析(筆->文->中->高, 筆->文->中->低, 筆->武->中->高, 筆->武->中->低);
        if (配置->笔内原始K线包含整笔 &&
                相对方向_是否包含(相对方向_分析(筆->文->中->标的K线->高, 筆->文->中->标的K线->低,
                                          筆->武->中->标的K线->高, 筆->武->中->标的K线->低))) {
            return false;
        }
    }
    if (虚线_方向(筆) == 相对方向_向下) {
        return 相对方向_是否向下(rel);
    }
    return 相对方向_是否向上(rel);
}

static void 笔_弹出旧笔(动态数组 *分型序列, 动态数组 *笔序列) {
    分型 *旧分型 = 弱引用_数组弹出(分型序列);
    (void) 旧分型;
    if (笔序列->长度 > 0) {
        虚线 *旧笔 = 弱引用_数组弹出(笔序列);
        assert(旧笔->武 == 旧分型);
        旧笔->有效性 = false;
    }
}

static void 笔_添加新笔(动态数组 *分型序列, 动态数组 *笔序列, 分型 *待添加分型, 虚线 *待添加新笔) {
    if (分型序列->长度 == 0 && (待添加分型->结构 != 分型结构_顶 && 待添加分型->结构 != 分型结构_底)) {
        fprintf(stderr, "笔_添加新笔: 首次添加不为顶底\n");
        释放虚线(待添加新笔);
        return;
    }
    if (分型序列->长度 > 0) {
        分型 *末 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (末->结构 == 待添加分型->结构) {
            fprintf(stderr, "笔_添加新笔: 分型相同\n");
            释放虚线(待添加新笔);
            return;
        }
    }
    弱引用_数组追加(分型序列, 待添加分型);
    if (笔序列->长度 > 0) {
        虚线 *末笔 = 动态数组_获取(笔序列, 笔序列->长度 - 1);
        if (!虚线_之后是(末笔, 待添加新笔)) {
            fprintf(stderr, "笔_添加新笔: 不连续\n");
            释放虚线(待添加新笔);
            分型 *回滚 = 动态数组_弹出(分型序列);
            弱引用_手动减少(回滚);

            return;
        }
        待添加新笔->序号 = 末笔->序号 + 1;
        if (待添加新笔->武->左 == NULL && 待添加新笔->武->右 == NULL) {
            待添加新笔->有效性 = false;
        }
    }
    弱引用_数组追加(笔序列, 待添加新笔);
}

/*
 * 笔_分析 — 显式栈版本（不依赖 C 调用栈）
 *
 * 原 Python 使用深度递归（max 64 层），C99 中以显式栈模拟。
 * 栈帧按 LIFO 顺序压入：后压入者先执行。
 */
#define 笔分析_MAX栈深 128

typedef struct {
    分型 *待分析;
    int 层级;
} 笔分析帧;

void 笔_分析(分型 *初始分型, 动态数组 *分型序列, 动态数组 *笔序列,
                动态数组 *缠K序列, 动态数组 *普K序列,
                缠论配置 *配置) {
    笔分析帧 栈[笔分析_MAX栈深];
    int 栈顶 = 0;
    栈[栈顶++] = (笔分析帧) {
        初始分型, 0
    };

    while (栈顶 > 0) {
        笔分析帧 帧 = 栈[--栈顶];
        分型 *当前分型 = 帧.待分析;
        int 层次 = 帧.层级;

        if (!当前分型) {
            continue;
        }
        if (层次 > 64) {
            fprintf(stderr, "笔_分析: 深度超出 %d\n", 层次);
            // continue;
        }
        if (当前分型->结构 != 分型结构_顶 && 当前分型->结构 != 分型结构_底) {
            continue;
        }

        if (分型序列->长度 == 0) {
            if (当前分型->结构 == 分型结构_顶 || 当前分型->结构 == 分型结构_底) {
                分型_向序列中添加(分型序列, 当前分型);
            }
            continue;
        }

        分型 *之前分型 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (之前分型->中->时间戳 == 当前分型->中->时间戳 ||
                之前分型->结构 == 分型结构_上 || 之前分型->结构 == 分型结构_下) {
            笔_弹出旧笔(分型序列, 笔序列);
            if (分型序列->长度 == 0) {
                if (当前分型->右 != NULL) {
                    分型_向序列中添加(分型序列, 当前分型);
                }
                continue;
            }
        }

        之前分型 = 动态数组_获取(分型序列, 分型序列->长度 - 1);
        if (之前分型->中->时间戳 > 当前分型->中->时间戳 &&
                之前分型->中->序号 - 当前分型->中->序号 > 1) {
            continue;
        }

        /* --- 弱化：弹出旧笔后重新分析 --- */
        if (配置->笔弱化 && 笔序列->长度 > 0) {
            虚线 *前一笔 = 动态数组_获取(笔序列, 笔序列->长度 - 1);
            if (前一笔->武->中->序号 - 前一笔->文->中->序号 + 1 == 3) {
                if ((虚线_方向(前一笔) == 相对方向_向上 && 虚线_低(前一笔) > 当前分型->分型特征值 && 当前分型->结构 == 分型结构_底) ||
                        (虚线_方向(前一笔) == 相对方向_向下 && 虚线_高(前一笔) < 当前分型->分型特征值 && 当前分型->结构 == 分型结构_顶)) {
                    笔_弹出旧笔(分型序列, 笔序列);
                    if (栈顶 < 笔分析_MAX栈深)
                        栈[栈顶++] = (笔分析帧) {
                        当前分型, 层次 + 1
                    };
                    continue;
                }
            }
        }

        /* --- 不同结构：尝试成笔 --- */
        if (之前分型->结构 != 当前分型->结构) {
            size_t si = 动态数组_查找(缠K序列, 之前分型->中);
            size_t ei = 动态数组_查找(缠K序列, 当前分型->中);
            size_t 基础长 = 0;
            if (si != (size_t) - 1 && ei != (size_t) - 1 && ei >= si) {
                基础长 = ei - si + 1;
            }

            虚线 *当前笔 = 虚线_创建笔(之前分型, 当前分型, true);
            int 缠K数 = 0;
            if (基础长 > 0) {
                动态数组 tmp = {.数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长};
                缠K数 = 笔_获取缠K数量(&tmp, 笔序列, 配置);
            }

            if (缠K数 >= 配置->笔内元素数量) {
                缠论K线 *文官 = NULL;
                if (基础长 > 0) {
                    动态数组 tmp = {.数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长};
                    文官 = (之前分型->结构 == 分型结构_顶 && 当前分型->结构 == 分型结构_底)
                             ? 笔_实际高点(&tmp, false)
                             : 笔_实际低点(&tmp, false);
                }

                if (文官 && 文官 != 之前分型->中) {
                    分型 *临时分型 = 分型_从缠K序列中获取分型(
                                               (缠论K线 **) 缠K序列->数据, 缠K序列->长度, 文官);
                    if (临时分型) {
                        /* 压入：先压 当前分型，后压 临时分型 → 临时分型先执行 */
                        if (栈顶 + 1 < 笔分析_MAX栈深) {
                            栈[栈顶++] = (笔分析帧) {
                                当前分型, 层次 + 1
                            };
                            栈[栈顶++] = (笔分析帧) {
                                临时分型, 层次 + 1
                            };
                        } else {
                            释放分型(临时分型);
                        }
                        释放虚线(当前笔);
                        continue;
                    }
                }

                /* 武将判断 */
                缠论K线 *武将 = NULL;
                if (基础长 > 0) {
                    动态数组 tmp = {.数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长};
                    武将 = (之前分型->结构 == 分型结构_顶 && 当前分型->结构 == 分型结构_底)
                             ? 笔_实际低点(&tmp, 配置->笔内相同终点取舍)
                             : 笔_实际高点(&tmp, 配置->笔内相同终点取舍);
                }

                if (笔_相对关系(当前笔, 配置) && 当前分型->中 == 武将) {
                    笔_添加新笔(分型序列, 笔序列, 当前分型, 当前笔);
                    continue;
                }

                if (配置->笔次级成笔 && 基础长 > 0) {
                    动态数组 tmp = {.数据 = 缠K序列->数据 + si, .长度 = 基础长, .容量 = 基础长};
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
                    分型 *临时分型 = 分型_从缠K序列中获取分型(
                                               (缠论K线 **) 缠K序列->数据, 缠K序列->长度, 当前分型->右);
                    if (临时分型 && 栈顶 < 笔分析_MAX栈深)
                        栈[栈顶++] = (笔分析帧) {
                        临时分型, 层次 + 1
                    };
                    else if (临时分型) {
                        释放分型(临时分型);
                    }
                }
            }
            释放虚线(当前笔);
        } else {
            /* --- 相同结构：新分型更极端时弹出并重建 --- */
            double fv = 当前分型->分型特征值;
            if ((之前分型->结构 == 分型结构_顶 && 之前分型->分型特征值 < fv) ||
                    (之前分型->结构 == 分型结构_底 && 之前分型->分型特征值 > fv)) {
                笔_弹出旧笔(分型序列, 笔序列);

                size_t si = 动态数组_查找(缠K序列, 之前分型->中);
                size_t ei = 动态数组_查找(缠K序列, 当前分型->中);
                if (si != (size_t) - 1 && ei != (size_t) - 1 && ei >= si) {
                    动态数组 tmp = {.数据 = 缠K序列->数据 + si, .长度 = ei - si + 1, .容量 = ei - si + 1};
                    缠论K线 *武将 = (之前分型->结构 == 分型结构_顶)
                                         ? 笔_实际低点(&tmp, 配置->笔内相同终点取舍)
                                         : 笔_实际高点(&tmp, 配置->笔内相同终点取舍);
                    分型 *临时分型 = 分型_从缠K序列中获取分型(
                                               (缠论K线 **) 缠K序列->数据, 缠K序列->长度, 武将);

                    if (分型序列->长度 > 0 && 临时分型) {
                        /* 修复错过笔：Python 按正序递归处理错过笔。
                           LIFO 栈需逆序压入，使正序执行：
                           执行序: 临时分型 → 错过笔1 → 错过笔2 → ... → 当前分型 */
                        int 错过起始 = 栈顶;
                        /* 先压入 当前分型（最后执行） */
                        if (栈顶 < 笔分析_MAX栈深) {
                            栈[栈顶++] = (笔分析帧) {
                                当前分型, 层次 + 1
                            };
                        } else {
                            释放分型(临时分型);
                            分型_向序列中添加(分型序列, 当前分型);
                            continue;
                        }
                        /* 收集期间错过的笔 */
                        size_t si2 = 动态数组_查找(缠K序列, 武将);
                        if (si2 != (size_t) - 1) {
                            for (size_t j = si2; j < 缠K序列->长度; j++) {
                                缠论K线 *ck = 动态数组_获取(缠K序列, j);
                                if (ck->分型 == 分型结构_底 || ck->分型 == 分型结构_顶) {
                                    分型 *tf = 分型_从缠K序列中获取分型(
                                                     (缠论K线 **) 缠K序列->数据, 缠K序列->长度, ck);
                                    if (tf && 栈顶 < 笔分析_MAX栈深) {
                                        栈[栈顶++] = (笔分析帧) {
                                            tf, 层次 + 1
                                        };
                                    } else if (tf) {
                                        释放分型(tf);
                                    }
                                }
                            }
                        }
                        /* 逆序错过笔段，使正序执行 */
                        for (int a = 错过起始 + 1, b = 栈顶 - 1; a < b; a++, b--) {
                            笔分析帧 t = 栈[a];
                            栈[a] = 栈[b];
                            栈[b] = t;
                        }
                        /* 压入 临时分型（最先执行） */
                        if (栈顶 < 笔分析_MAX栈深)
                            栈[栈顶++] = (笔分析帧) {
                            临时分型, 层次 + 1
                        };
                        else {
                            释放分型(临时分型);
                        }
                    } else {
                        if (临时分型) {
                            释放分型(临时分型);
                        }
                        分型_向序列中添加(分型序列, 当前分型);
                    }
                }
            }
        }
    }
}
#undef 笔分析_MAX栈深

虚线 *笔_以文会友(动态数组 *笔序列, 分型 *文) {
    for (size_t i = 0; i < 笔序列->长度; i++) {
        虚线 *b = 动态数组_获取(笔序列, i);
        if (b->文 == 文) {
            return b;
        }
    }
    return NULL;
}

虚线 *笔_以武会友(动态数组 *笔序列, 分型 *武) {
    for (size_t i = 笔序列->长度; i > 0; i--) {
        虚线 *b = 动态数组_获取(笔序列, i - 1);
        if (b->武 == 武) {
            return b;
        }
    }
    return NULL;
}

虚线 *笔_根据缠K找笔(动态数组 *笔序列, 缠论K线 *缠K, int 偏移) {
    for (size_t i = 笔序列->长度; i > 0; i--) {
        虚线 *b = 动态数组_获取(笔序列, i - 1);
        if (b->文->中->序号 - 偏移 <= 缠K->序号 && 缠K->序号 <= b->武->中->序号) {
            return b;
        }
    }
    return NULL;
}

/* ================================================================
 * 线段特征
 * ================================================================ */

线段特征 *线段特征_新建(const char *标识, 动态数组 *基础序列, 相对方向 线段方向) {
    线段特征 *t = 分配(sizeof(线段特征), CHAN_TYPE_线段特征);
    t->序号 = 0;
    strncpy(t->标识, 标识, 127);
    t->标识[127] = '\0';
    t->线段方向 = 线段方向;
    动态数组_初始化(&t->基础序列, 4);
    for (size_t i = 0; i < 基础序列->长度; i++) {
        虚线 *b = 动态数组_获取(基础序列, i);
        弱引用_数组追加(&t->基础序列, b);
    }
    return t;
}

分型 *线段特征_文(线段特征 *self) {
    if (self->基础序列.长度 == 0) {
        return NULL;
    }
    double (*func)(double, double) = (self->线段方向 == 相对方向_向上) ? fmax : fmin;
    虚线 *best = 动态数组_获取(&self->基础序列, 0);
    for (size_t i = 1; i < self->基础序列.长度; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (func(x->文->中->分型特征值, best->文->中->分型特征值) == x->文->中->分型特征值) {
            best = x;
        }
    }
    return best->文;
}

分型 *线段特征_武(线段特征 *self) {
    if (self->基础序列.长度 == 0) {
        return NULL;
    }
    double (*func)(double, double) = (self->线段方向 == 相对方向_向上) ? fmax : fmin;
    虚线 *best = 动态数组_获取(&self->基础序列, 0);
    for (size_t i = 1; i < self->基础序列.长度; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (func(x->武->中->分型特征值, best->武->中->分型特征值) == x->武->中->分型特征值) {
            best = x;
        }
    }
    return best->武;
}

double 线段特征_高(线段特征 *self) {
    分型 *w = 线段特征_文(self);
    分型 *u = 线段特征_武(self);
    double hw = w ? w->中->分型特征值 : 0;
    double hu = u ? u->中->分型特征值 : 0;
    return fmax(hw, hu);
}

double 线段特征_低(线段特征 *self) {
    分型 *w = 线段特征_文(self);
    分型 *u = 线段特征_武(self);
    double lw = w ? w->中->分型特征值 : 0;
    double lu = u ? u->中->分型特征值 : 0;
    return fmin(lw, lu);
}

相对方向 线段特征_方向(线段特征 *self) {
    return 相对方向_翻转(self->线段方向);
}

void 线段特征_添加(线段特征 *self, 虚线 *待添加) {
    if (虚线_方向(待添加) == self->线段方向) {
        fprintf(stderr, "线段特征_添加: 方向不匹配\n");
        return;
    }
    弱引用_数组追加(&self->基础序列, 待添加);
}

void 线段特征_删除(线段特征 *self, 虚线 *待删除) {
    size_t idx = 动态数组_查找(&self->基础序列, 待删除);
    if (idx != (size_t) - 1) {
        for (size_t i = idx; i < self->基础序列.长度 - 1; i++) {
            self->基础序列.数据[i] = self->基础序列.数据[i + 1];
        }
        self->基础序列.长度--;
    }
}

void 线段特征_静态分析(动态数组 *虚线序列, 相对方向 线段方向, const char *四象,
                               bool 是否忽视, 动态数组 *结果) {
    /* 确定需要合并的方向序列 */
    bool is_老 = (strcmp(四象, "老阳") == 0 || strcmp(四象, "老阴") == 0);
    bool 严格包含 = is_老 && !是否忽视;

    for (size_t idx = 0; idx < 虚线序列->长度; idx++) {
        虚线 *当前虚线 = 动态数组_获取(虚线序列, idx);

        /* 情况1：方向相同（可能触发分型替换） */
        if (虚线_方向(当前虚线) == 线段方向) {
            if (结果->长度 < 3) {
                continue;
            }
            线段特征 *左 = 动态数组_获取(结果, 结果->长度 - 3);
            线段特征 *中 = 动态数组_获取(结果, 结果->长度 - 2);
            线段特征 *右 = 动态数组_获取(结果, 结果->长度 - 1);

            /* 使用分型分析判断结构 */
            缺口 fl = {.高 = 线段特征_高(左), .低 = 线段特征_低(左)};
            缺口 fm = {.高 = 线段特征_高(中), .低 = 线段特征_低(中)};
            缺口 fr = {.高 = 线段特征_高(右), .低 = 线段特征_低(右)};

            分型结构 结构 = 分型结构_分析(&fl, &fm, &fr, true, true);

            if ((线段方向 == 相对方向_向上 && 结构 == 分型结构_顶 && 虚线_高(当前虚线) > 线段特征_高(中)) ||
                    (线段方向 == 相对方向_向下 && 结构 == 分型结构_底 && 虚线_低(当前虚线) < 线段特征_低(中))) {
                /* 找序号最小和最大的虚线 */
                虚线 *小号 = 中->基础序列.数据[0];
                虚线 *大号 = 右->基础序列.数据[0];
                for (size_t j = 1; j < 中->基础序列.长度; j++) {
                    if (((虚线 *) 中->基础序列.数据[j])->序号 < 小号->序号) {
                        小号 = 中->基础序列.数据[j];
                    }
                }
                for (size_t j = 1; j < 右->基础序列.长度; j++) {
                    if (((虚线 *) 右->基础序列.数据[j])->序号 > 大号->序号) {
                        大号 = 右->基础序列.数据[j];
                    }
                }
                虚线 *fake = 虚线_创建笔(小号->文, 大号->武, false);
                弱引用_数组弹出(结果); /* pop & free old right */
                动态数组 tmp2 = {.数据 = malloc(sizeof(void *)), .长度 = 0, .容量 = 1};
                if (!tmp2.数据) {
                    perror("线段特征_静态分析");
                    chan_oom_handler(sizeof(void *));
                    return;
                }
                动态数组_追加(&tmp2, fake);
                线段特征 *new_ft = 线段特征_新建("特征", &tmp2, 线段方向);
                free(tmp2.数据);
                弱引用_数组弹出(结果);
                弱引用_数组设置(结果, 结果->长度 - 1, new_ft);
            }
            continue;
        }

        /* 情况2：方向不同 */
        if (结果->长度 == 0) {
            动态数组 tmp = {.数据 = malloc(sizeof(void *)), .长度 = 0, .容量 = 1};
            if (!tmp.数据) {
                perror("线段特征_静态分析");
                chan_oom_handler(sizeof(void *));
                return;
            }
            动态数组_追加(&tmp, 当前虚线);
            线段特征 *ft = 线段特征_新建("特征", &tmp, 线段方向);
            free(tmp.数据);
            弱引用_数组追加(结果, ft);
            continue;
        }

        线段特征 *之前 = 动态数组_获取(结果, 结果->长度 - 1);
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
            动态数组 tmp = {.数据 = malloc(sizeof(void *)), .长度 = 0, .容量 = 1};
            if (!tmp.数据) {
                perror("线段特征_静态分析");
                chan_oom_handler(sizeof(void *));
                return;
            }
            动态数组_追加(&tmp, 当前虚线);
            线段特征 *ft = 线段特征_新建("特征", &tmp, 线段方向);
            free(tmp.数据);
            弱引用_数组追加(结果, ft);
        }
    }
}

void 线段特征_获取分型序列(动态数组 *特征序列, 动态数组 *结果) {
    if (特征序列->长度 < 3) {
        return;
    }
    for (size_t i = 1; i < 特征序列->长度 - 1; i++) {
        线段特征 *左 = 动态数组_获取(特征序列, i - 1);
        线段特征 *中 = 动态数组_获取(特征序列, i);
        线段特征 *右 = 动态数组_获取(特征序列, i + 1);
        缺口 fl = {.高 = 线段特征_高(左), .低 = 线段特征_低(左)};
        缺口 fm = {.高 = 线段特征_高(中), .低 = 线段特征_低(中)};
        缺口 fr = {.高 = 线段特征_高(右), .低 = 线段特征_低(右)};
        分型结构 s = 分型结构_分析(&fl, &fm, &fr, true, true);
        特征分型 *tf = 特征分型_新建(左, 中, 右, s);
        弱引用_数组追加(结果, tf);
    }
}

void 释放线段特征(线段特征 *obj) {
    弱引用_数组清除(&obj->基础序列);
}

/* ================================================================
 * 特征分型
 * ================================================================ */

特征分型 *特征分型_新建(线段特征 *左, 线段特征 *中, 线段特征 *右, 分型结构 结构) {
    特征分型 *t = 分配(sizeof(特征分型), CHAN_TYPE_特征分型);
    弱引用_设置(t, 左, 左);
    弱引用_设置(t, 中, 中);
    弱引用_设置(t, 右, 右);
    t->结构 = 结构;
    return t;
}

void 释放特征分型(特征分型 *obj) {
    弱引用_设置(obj, 左, NULL);
    弱引用_设置(obj, 中, NULL);
    弱引用_设置(obj, 右, NULL);
}

/* ================================================================
 * 线段 — 所有递归转为 while 循环 + 显式深度计数器
 * ================================================================ */

/* ---------- 内部助手 ---------- */

bool 线段_添加虚线(虚线 *段, 虚线 *筆) {
    if (段->基础序列.长度 > 0) {
        虚线 *末 = 动态数组_获取(&段->基础序列, 段->基础序列.长度 - 1);
        if (!分型_判断分型(末->武, 筆->文, "中")) {
            return false;
        }
        if (strcmp(末->标识, 筆->标识) != 0) {
            return false;
        }
    }
    弱引用_数组追加(&段->基础序列, 筆);
    return true;
}

void 线段_武斗(虚线 *段, 分型 *武, int 行号) {
    (void) 行号;
    if (段->武 == 武) {
        return;
    }
    if (段->武->分型特征值 == 武->分型特征值 && 段->武->时间戳 != 武->时间戳) {
        fprintf(stderr, "%s.武斗[%d], 发现特征值相等但时间戳不同\n",
                段->标识, 行号);
    }
    assert(段->文->结构 != 武->结构);
    if (武->右 != NULL) {
        缺口 _w左 = {.高 = 武->左->高, .低 = 武->左->低};
        缺口 _w中 = {.高 = 武->中->高, .低 = 武->中->低};
        缺口 _w右 = {.高 = 武->右->高, .低 = 武->右->低};
        if (分型结构_分析(&_w左, &_w中, &_w右, false, false) != 武->结构) {
            fprintf(stderr, "线段_武斗[%d]: 武分型结构无效\n", 行号);
            return;
        }
    }
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
    弱引用_设置(段, 武, 武);
    if (虚线_方向(段) == 相对方向_向上) {
        段->高 = 武->中->高;
    } else {
        段->低 = 武->中->低;
    }
}

bool 线段_特征分型终结(虚线 *段) {
    动态数组 特征序列;
    动态数组_初始化(&特征序列, 8);
    线段特征_静态分析(&段->基础序列, 虚线_方向(段), 线段_四象(段), false, &特征序列);
    bool result = false;
    if (特征序列.长度 >= 3) {
        线段特征 *左 = 动态数组_获取(&特征序列, 特征序列.长度 - 3);
        线段特征 *中 = 动态数组_获取(&特征序列, 特征序列.长度 - 2);
        线段特征 *右 = 动态数组_获取(&特征序列, 特征序列.长度 - 1);
        /* 用缺口进行结构分析 */
        缺口 fl = {.高 = 线段特征_高(左), .低 = 线段特征_低(左)};
        缺口 fm = {.高 = 线段特征_高(中), .低 = 线段特征_低(中)};
        缺口 fr = {.高 = 线段特征_高(右), .低 = 线段特征_低(右)};
        分型结构 s = 分型结构_分析(&fl, &fm, &fr, true, true);
        if (虚线_方向(段) == 相对方向_向上 && s == 分型结构_顶) {
            result = true;
        }
        if (虚线_方向(段) == 相对方向_向下 && s == 分型结构_底) {
            result = true;
        }
    }
    /* 释放特征序列 */
    弱引用_数组清除(&特征序列);
    return result;
}

void 线段_特征序列状态(虚线 *段, bool *左, bool *中, bool *右) {
    *左 = 动态数组_获取(&段->特征序列, 0) != NULL;
    *中 = 动态数组_获取(&段->特征序列, 1) != NULL;
    *右 = 动态数组_获取(&段->特征序列, 2) != NULL;
}

缺口 *线段_获取缺口(虚线 *段) {
    if (strcmp(段->模式, "文武") != 0) {
        return NULL;
    }
    线段特征 *左 = 动态数组_获取(&段->特征序列, 0);
    线段特征 *中 = 动态数组_获取(&段->特征序列, 1);
    if (!左 || !中) {
        return NULL;
    }
    相对方向 rel = 相对方向_分析(线段特征_高(左), 线段特征_低(左), 线段特征_高(中), 线段特征_低(中));
    if (相对方向_是否缺口(rel)) {
        double hh = fmax(线段特征_文(左)->中->分型特征值, 线段特征_文(中)->中->分型特征值);
        double ll = fmin(线段特征_文(左)->中->分型特征值, 线段特征_文(中)->中->分型特征值);
        return 缺口_新建(hh, ll);
    }
    return NULL;
}

const char *线段_四象(虚线 *段) {
    if (段->前一缺口) {
        return (虚线_方向(段) == 相对方向_向上) ? "老阳" : "老阴";
    }
    return (虚线_方向(段) == 相对方向_向上) ? "小阳" : "少阴";
}

void 线段_设置特征序列(虚线 *段, 线段特征 **序列, int 行号) {
    (void) 行号;
    if (strcmp(段->模式, "文武") != 0) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        弱引用_数组设置(&段->特征序列, i, 序列[i]);
    }
    if (序列[2]) {
        if (动态数组_查找(&段->基础序列, 序列[2]->基础序列.数据[序列[2]->基础序列.长度 - 1]) == (size_t) - 1) {
            fprintf(stderr, "线段_设置特征序列: 右[-1]不在基础序列\n");
            return;
        }
        /* 截断基础序列到右[-1] */
        虚线 *last = 序列[2]->基础序列.数据[序列[2]->基础序列.长度 - 1];
        动态数组 new_base;
        动态数组_初始化(&new_base, 4);
        for (size_t i_ = 0; i_ < 段->基础序列.长度; i_++) {
            虚线 *e = 动态数组_获取(&段->基础序列, i_);
            动态数组_追加(&new_base, e);
            if (e == last) {
                break;
            }
        }
        if (new_base.长度 >= 6 && new_base.长度 % 2 == 0) {
            for (size_t i_ = 0; i_ < new_base.长度; i_++) {
                弱引用_手动增加(动态数组_获取(&new_base, i_));
            }
            for (size_t i_ = 0; i_ < 段->基础序列.长度; i_++) {
                弱引用_手动减少(动态数组_获取(&段->基础序列, i_));
            }
            动态数组_释放(&段->基础序列, false);
            段->基础序列 = new_base;
        } else {
            动态数组_释放(&new_base, false);
            弱引用_数组设置(&段->特征序列, 2, NULL);
        }
    }
}

void 线段_刷新特征序列(虚线 *段, 缠论配置 *配置) {
    if (strcmp(段->模式, "文武") != 0) {
        return;
    }

    /* 构建用于分析的基础序列（考虑前一结束位置） */
    动态数组 分析用序列;
    动态数组_初始化(&分析用序列, 段->基础序列.长度);
    size_t start = 0;
    if (段->前一结束位置) {
        size_t idx = 动态数组_查找(&段->基础序列, 段->前一结束位置);
        if (idx != (size_t) - 1 && idx > 0) {
            start = idx - 1;
        }
    }
    for (size_t i = start; i < 段->基础序列.长度; i++) {
        动态数组_追加(&分析用序列, 动态数组_获取(&段->基础序列, i));
    }

    动态数组 特征序列结果;
    动态数组_初始化(&特征序列结果, 8);
    线段特征_静态分析(&分析用序列, 虚线_方向(段), 线段_四象(段),
                              配置->线段_特征序列忽视老阴老阳, &特征序列结果);
    线段特征 *seq[3] = {NULL, NULL, NULL};
    if (特征序列结果.长度 >= 3) {
        动态数组 分型序列;
        动态数组_初始化(&分型序列, 4);
        线段特征_获取分型序列(&特征序列结果, &分型序列);
        if (分型序列.长度 > 0) {
            特征分型 *last_tf = 动态数组_获取(&分型序列, 分型序列.长度 - 1);
            if ((虚线_方向(段) == 相对方向_向上 && last_tf->结构 == 分型结构_顶) ||
                    (虚线_方向(段) == 相对方向_向下 && last_tf->结构 == 分型结构_底)) {
                /* Only set seq[2] if the resulting truncation would be valid (>=6, even) */
                虚线 *last_stroke = last_tf->右->基础序列.数据[last_tf->右->基础序列.长度 - 1];
                size_t last_idx = 动态数组_查找(&段->基础序列, last_stroke);
                if (last_idx != (size_t) - 1 && (last_idx + 1) >= 6 && (last_idx + 1) % 2 == 0) {
                    seq[0] = last_tf->左;
                    seq[1] = last_tf->中;
                    seq[2] = last_tf->右;
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
        弱引用_数组清除(&分型序列);
    } else {
        for (size_t i = 0; i < 特征序列结果.长度 && i < 3; i++) {
            seq[i] = 动态数组_获取(&特征序列结果, i);
        }
    }

    线段_设置特征序列(段, seq, __LINE__);

    /* 清除未被选中的特征的内部元素引用，防止 phantom refs */
    for (size_t _i = 0; _i < 特征序列结果.长度; _i++) {
        线段特征 *_ft = 动态数组_获取(&特征序列结果, _i);
        bool _kept = false;
        for (int _j = 0; _j < 3; _j++)
            if (seq[_j] == _ft) {
                _kept = true;
                break;
            }
        if (!_kept) {
            弱引用_数组清除(&_ft->基础序列);
        }
    }
    弱引用_数组清除(&特征序列结果);
    动态数组_释放(&分析用序列, false); /* 仅释放数组内存，不操作元素弱引用 */
}

void 线段_分割序列(虚线 *段, 中枢 *所属中枢,
                         动态数组 *前, 动态数组 *后, 动态数组 *第三买卖线, 虚线 **贯穿伤) {
    *贯穿伤 = NULL;
    前->长度 = 0;
    后->长度 = 0;
    第三买卖线->长度 = 0;

    if (strcmp(段->模式, "文武") != 0) {
        for (size_t i = 0; i < 段->基础序列.长度; i++) {
            动态数组_追加(前, 动态数组_获取(&段->基础序列, i));
        }
        return;
    }
    assert(段->基础序列.长度 > 0);

    bool 在之后 = false;
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        虚线 *b = 动态数组_获取(&段->基础序列, i);
        if (!在之后 && (i == 0 || ((虚线 *) 动态数组_获取(&段->基础序列, i - 1))->武 != 段->武)) {
            动态数组_追加(前, b);
        }
        if (在之后) {
            动态数组_追加(后, b);
        }
        if (b->文 == 段->武) {
            动态数组_追加(后, b);
            在之后 = true;
        }
    }

    const char *状态 = NULL;
    if (所属中枢) {
        弱引用_设置(所属中枢, 本级_第三买卖线, NULL);
        分型 *尾部 = 段->武;
        if (后->长度 > 0) {
            虚线 *last_b = 动态数组_获取(后, 后->长度 - 1);
            尾部 = last_b->武;
        }
        if (中枢_高(所属中枢) >= 尾部->分型特征值 && 尾部->分型特征值 >= 中枢_低(所属中枢)) {
            状态 = "中枢之中";
        }
        else if (中枢_高(所属中枢) < 尾部->分型特征值) {
            状态 = "中枢之上";
        }
        else if (中枢_低(所属中枢) > 尾部->分型特征值) {
            状态 = "中枢之下";
        }
    }

    if (状态 && strcmp(状态, "中枢之上") == 0) {
        for (size_t i_ = 段->基础序列.长度; i_ > 0; i_--) {
            size_t i = i_ - 1;
            虚线 *b = 动态数组_获取(&段->基础序列, i);
            if (虚线_方向(b) == 相对方向_向下) {
                相对方向 rel = 相对方向_分析(中枢_高(所属中枢), 中枢_低(所属中枢), 虚线_高(b), 虚线_低(b));
                if (rel == 相对方向_向上缺口) {
                    动态数组_追加(第三买卖线, b);
                }
                else {
                    break;
                }
            }
        }
    }
    if (状态 && strcmp(状态, "中枢之下") == 0) {
        for (size_t i_ = 段->基础序列.长度; i_ > 0; i_--) {
            size_t i = i_ - 1;
            虚线 *b = 动态数组_获取(&段->基础序列, i);
            if (虚线_方向(b) == 相对方向_向上) {
                相对方向 rel = 相对方向_分析(中枢_高(所属中枢), 中枢_低(所属中枢), 虚线_高(b), 虚线_低(b));
                if (rel == 相对方向_向下缺口) {
                    动态数组_追加(第三买卖线, b);
                }
                else {
                    break;
                }
            }
        }
    }

    if (第三买卖线->长度 > 0 && 所属中枢) {
        /* reverse */
        for (size_t i = 0; i < 第三买卖线->长度 / 2; i++) {
            void *tmp = 第三买卖线->数据[i];
            第三买卖线->数据[i] = 第三买卖线->数据[第三买卖线->长度 - 1 - i];
            第三买卖线->数据[第三买卖线->长度 - 1 - i] = tmp;
        }
        弱引用_设置(所属中枢, 本级_第三买卖线, 动态数组_获取(第三买卖线, 0));
    }

    if (后->长度 > 0) {
        虚线 *first_in_后 = 动态数组_获取(后, 0);
        if (虚线_方向(段) == 相对方向_向上) {
            if (first_in_后->武->分型特征值 < 段->文->分型特征值) {
                *贯穿伤 = first_in_后;
            }
        } else {
            if (first_in_后->武->分型特征值 > 段->文->分型特征值) {
                *贯穿伤 = first_in_后;
            }
        }
    }
}

void 线段_刷新(虚线 *段, 缠论配置 *配置) {
    if (strcmp(段->模式, "文武") != 0) {
        return;
    }
    if (段->基础序列.长度 == 0) {
        fprintf(stderr, "线段_刷新: 基础序列为空\n");
        return;
    }

    线段_刷新特征序列(段, 配置);

    /* 统计有效特征序列 */
    线段特征 **eff = malloc(3 * sizeof(线段特征 *));
    if (!eff) {
        perror("线段_武斗");
        chan_oom_handler(3 * sizeof(线段特征 *));
        return;
    }
    int effn = 0;
    for (int i = 0; i < 3; i++) {
        线段特征 *ft = 动态数组_获取(&段->特征序列, i);
        if (ft) {
            eff[effn++] = ft;
        }
    }

    if (effn == 3) {
        线段_武斗(段, 线段特征_文((线段特征 *) 动态数组_获取(&段->特征序列, 1)), __LINE__);
    } else if (effn >= 1) {
        线段特征 *最近 = eff[effn - 1];
        虚线 *last_in_ft = 最近->基础序列.数据[最近->基础序列.长度 - 1];
        虚线 *特征后一笔 = NULL;
        if (动态数组_查找(&段->基础序列, last_in_ft) != (size_t) - 1) {
            特征后一笔 = last_in_ft;
        } else {
            特征后一笔 = 笔_以武会友(&段->基础序列, last_in_ft->武);
        }
        if (特征后一笔) {
            size_t idx = 动态数组_查找(&段->基础序列, 特征后一笔);
            if (idx != (size_t) - 1 && idx < 段->基础序列.长度 - 1) {
                虚线 *下一笔 = 动态数组_获取(&段->基础序列, idx + 1);
                if ((虚线_方向(段) == 相对方向_向上 && 虚线_高(段) <= 虚线_高(下一笔)) ||
                        (虚线_方向(段) == 相对方向_向下 && 虚线_低(段) >= 虚线_低(下一笔))) {
                    线段_武斗(段, 下一笔->武, __LINE__);
                }
            }
        }
    }
    free(eff);

    线段_获取内部中枢序列(段, 配置, NULL, NULL, NULL);
}

void 线段_序列重置(虚线 *段, 动态数组 *序列) {
    动态数组 new_base;
    动态数组_初始化(&new_base, 4);
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        虚线 *e = 动态数组_获取(&段->基础序列, i);
        if (动态数组_查找(序列, e) == (size_t) - 1) {
            bool found = false;
            for (size_t j = 0; j < 序列->长度; j++) {
                虚线 *s = 动态数组_获取(序列, j);
                if (s->序号 == e->序号 && s->有效性) {
                    e = s;
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }
        }
        if (new_base.长度 > 0 && !虚线_之后是(动态数组_获取(&new_base, new_base.长度 - 1), e)) {
            break;
        }
        动态数组_追加(&new_base, e);
    }
    /* 引用 new_base 基础序列，再解引用旧基础序列元素，防止引用计数归零 */
    for (size_t i = 0; i < new_base.长度; i++) {
        弱引用_手动增加(动态数组_获取(&new_base, i));
    }
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        弱引用_手动减少(动态数组_获取(&段->基础序列, i));
    }
    动态数组_释放(&段->基础序列, false);
    段->基础序列 = new_base;
    弱引用_数组设置(&段->特征序列, 2, NULL);
}

虚线 *线段_查找贯穿伤(虚线 *段) {
    for (size_t i = 3; i < 段->基础序列.长度; i++) {
        虚线 *b = 动态数组_获取(&段->基础序列, i);
        if (虚线_方向(段) == 相对方向_向上) {
            if (b->武->分型特征值 < 段->文->分型特征值) {
                return b;
            }
        } else {
            if (b->武->分型特征值 > 段->文->分型特征值) {
                return b;
            }
        }
    }
    return NULL;
}

void 线段_获取内部中枢序列(虚线 *段, 缠论配置 *配置,
                                     动态数组 *虚_out, 动态数组 *实_out, 动态数组 *合_out) {
    char buf[128];
    snprintf(buf, 127, "%s_%d_合_", 段->标识, 段->序号);
    中枢_分析(&段->基础序列, &段->合_中枢序列, true, buf);

    if (strcmp(段->模式, "文武") != 0) {
        if (合_out) {
            *合_out = 段->合_中枢序列;
        }
        return;
    }
    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 2);
    动态数组_初始化(&后, 2);
    动态数组_初始化(&第三, 2);
    虚线 *贯穿伤 = NULL;
    线段_分割序列(段, NULL, &前, &后, &第三, &贯穿伤);

    snprintf(buf, 127, "%s_%d_实_", 段->标识, 段->序号);
    中枢_分析(&前, &段->实_中枢序列, true, buf);
    snprintf(buf, 127, "%s_%d_虚_", 段->标识, 段->序号);
    中枢_分析(&后, &段->虚_中枢序列, true, buf);


    if (虚_out) {
        *虚_out = 段->虚_中枢序列;
    }
    if (实_out) {
        *实_out = 段->实_中枢序列;
    }
    if (合_out) {
        *合_out = 段->合_中枢序列;
    }

    动态数组_释放(&前, false);
    动态数组_释放(&后, false);
    动态数组_释放(&第三, false);
}

bool 线段_基础判断(虚线 *左, 虚线 *中, 虚线 *右, 相对方向 *关系序列, size_t 关系数) {
    if (!虚线_之后是(左, 中)) {
        return false;
    }
    if (!虚线_之后是(中, 右)) {
        return false;
    }
    if (!相对方向_是否包含(相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(中), 虚线_低(中)))) {
        return false;
    }
    if (!相对方向_是否包含(相对方向_分析(虚线_高(中), 虚线_低(中), 虚线_高(右), 虚线_低(右)))) {
        return false;
    }
    相对方向 rel = 相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(右), 虚线_低(右));
    bool found = false;
    for (size_t i = 0; i < 关系数; i++)
        if (rel == 关系序列[i]) {
            found = true;
            break;
        }
    if (!found) {
        return false;
    }
    if (虚线_方向(左) == 相对方向_向下 && !相对方向_是否向下(rel)) {
        return false;
    }
    if (虚线_方向(左) == 相对方向_向上 && !相对方向_是否向上(rel)) {
        return false;
    }
    return true;
}

/* ---------- 线段._添加线段 ---------- */
static void 线段_添加线段(动态数组 *线段序列, 虚线 *待添加, 缠论配置 *配置) {
    if (线段序列->长度 > 0) {
        虚线 *prev = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        if (!虚线_之后是(prev, 待添加)) {
            fprintf(stderr, "线段_添加线段: 不连续\n");
            return;
        }
    }
    待添加->模式[0] = '\0';
    strcpy(待添加->模式, "文武");

    if (线段序列->长度 == 0) {
        弱引用_数组追加(线段序列, 待添加);
        return;
    }

    虚线 *prev = 动态数组_获取(线段序列, 线段序列->长度 - 1);

    if (!动态数组_获取(&prev->特征序列, 2) && !prev->短路修正) {
        assert(动态数组_获取(&prev->特征序列, 2) != NULL);
    }

    待添加->序号 = prev->序号 + 1;
    弱引用_设置(待添加, 前一缺口, 线段_获取缺口(prev));
    弱引用_设置(待添加, 前一结束位置, prev->基础序列.数据[prev->基础序列.长度 - 1]);

    if (strcmp(线段_四象(prev), "老阴") == 0 || strcmp(线段_四象(prev), "老阳") == 0) {
        弱引用_设置(待添加, 前一缺口, NULL);
    }

    弱引用_数组追加(线段序列, 待添加);
}

/* ---------- 线段._弹出线段 ---------- */
static 虚线 *线段_弹出线段(动态数组 *线段序列, 虚线 *待弹出, 缠论配置 *配置) {
    (void) 配置;
    if (线段序列->长度 == 0) {
        return NULL;
    }
    if (动态数组_获取(线段序列, 线段序列->长度 - 1) != 待弹出) {
        fprintf(stderr, "线段_弹出线段: 不在列表中\n");
        return NULL;
    }
    虚线 *弹出 = 弱引用_数组弹出(线段序列);
    弹出->有效性 = false;
    弱引用_设置(弹出, 前一结束位置, NULL);
    return 弹出;
}

/* ---------- 四种修正 ---------- */

static bool 线段_缺口突破(动态数组 *线段序列, 缠论配置 *配置) {
    虚线 *cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    if (cur->基础序列.长度 == 0) {
        return false;
    }
    虚线 *last_stroke = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
    const char *sx = 线段_四象(cur);
    bool 同向 = (虚线_方向(last_stroke) == 虚线_方向(cur));
    if (同向) {
        return false;
    }
    if (strcmp(sx, "老阳") != 0 && strcmp(sx, "老阴") != 0) {
        return false;
    }
    if (动态数组_获取(&cur->特征序列, 2) != NULL) {
        return false;
    }
    if (!((strcmp(sx, "老阳") == 0 && 虚线_低(last_stroke) < 虚线_低(cur)) ||
            (strcmp(sx, "老阴") == 0 && 虚线_高(last_stroke) > 虚线_高(cur)))) {
        return false;
    }

    /* 执行修正 */
    动态数组 saved;
    动态数组_初始化(&saved, cur->基础序列.长度);
    for (size_t i = 0; i < cur->基础序列.长度; i++) {
        动态数组_追加(&saved, cur->基础序列.数据[i]);
    }
    线段_弹出线段(线段序列, cur, 配置);
    虚线 *cur2 = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    assert(动态数组_获取(&cur2->特征序列, 2) != NULL);
    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4);
    动态数组_初始化(&后, 4);
    动态数组_初始化(&第三, 4);
    虚线 *gs = NULL;
    线段_分割序列(cur2, NULL, &前, &后, &第三, &gs);
    for (size_t i = 0; i < saved.长度; i++) {
        动态数组_追加(&前, saved.数据[i]);
    }
    for (size_t i = 0; i < 前.长度; i++) {
        弱引用_手动增加(动态数组_获取(&前, i));
    }
    for (size_t i = 0; i < cur2->基础序列.长度; i++) {
        弱引用_手动减少(动态数组_获取(&cur2->基础序列, i));
    }
    动态数组_释放(&cur2->基础序列, false);
    cur2->基础序列 = 前;
    线段_刷新(cur2, 配置);
    动态数组_释放(&saved, false);
    动态数组_释放(&后, false);
    动态数组_释放(&第三, false);
    return true;
}

static bool 线段_非缺口下穿刺(动态数组 *线段序列, 缠论配置 *配置) {
    虚线 *cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    const char *sx = 线段_四象(cur);
    if (!(配置->线段_非缺口下穿刺 &&
            (strcmp(sx, "小阳") == 0 || strcmp(sx, "少阴") == 0) &&
            动态数组_获取(&cur->特征序列, 2) == NULL)) {
        return false;
    }

    虚线 *gs = 线段_查找贯穿伤(cur);
    if (!gs) {
        return false;
    }

    size_t idx = 动态数组_查找(&cur->基础序列, gs);
    if (idx == (size_t) - 1) {
        return false;
    }
    size_t base_len = cur->基础序列.长度 - idx;
    if (!(base_len == 4 && 线段序列->长度 >= 2)) {
        return false;
    }

    虚线 *左 = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 3);
    虚线 *右 = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
    if (相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(右), 虚线_低(右)) != 虚线_方向(cur)) {
        return false;
    }

    /* 执行修正 */
    fprintf(stderr, "[警告] 线段_非缺口下穿刺\n");
    动态数组 saved;
    动态数组_初始化(&saved, cur->基础序列.长度);
    for (size_t i = 0; i < cur->基础序列.长度; i++) {
        动态数组_追加(&saved, cur->基础序列.数据[i]);
    }
    线段_弹出线段(线段序列, cur, 配置);
    虚线 *cur2 = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    弱引用_数组设置(&cur2->特征序列, 2, NULL);
    size_t start_idx = 动态数组_查找(&saved, cur2->基础序列.数据[cur2->基础序列.长度 - 1]);
    for (size_t i = start_idx + 1; i < saved.长度; i++) {
        线段_添加虚线(cur2, saved.数据[i]);
    }
    线段_刷新(cur2, 配置);

    if (动态数组_获取(&cur2->特征序列, 2)) {
        动态数组 new_seg;
        动态数组_初始化(&new_seg, 3);
        动态数组_追加(&new_seg, saved.数据[saved.长度 - 3]);
        动态数组_追加(&new_seg, saved.数据[saved.长度 - 2]);
        动态数组_追加(&new_seg, saved.数据[saved.长度 - 1]);
        虚线 *seg = 虚线_创建线段(&new_seg);
        线段_添加线段(线段序列, seg, 配置);
        /* seg->特征序列 already initialized with 3 NULLs by 虚线_创建线段 */
        动态数组 tmp = {.数据 = malloc(sizeof(void *)), .长度 = 0, .容量 = 1};
        if (!tmp.数据) {
            perror("线段_非缺口下穿刺");
            chan_oom_handler(sizeof(void *));
            return false;
        }
        动态数组_追加(&tmp, saved.数据[new_seg.长度 - 2]);
        线段特征 *ft = 线段特征_新建("特征", &tmp, 虚线_方向(seg));
        free(tmp.数据);
        弱引用_数组设置(&seg->特征序列, 0, ft);
        动态数组_释放(&new_seg, false);
    }
    动态数组_释放(&saved, false);
    return true;
}

static bool 线段_缺口后紧急修正(动态数组 *线段序列, 缠论配置 *配置) {
    虚线 *cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    const char *sx = 线段_四象(cur);
    if (!(配置->线段_缺口后紧急修正 && !配置->线段_特征序列忽视老阴老阳 &&
            (strcmp(sx, "小阳") == 0 || strcmp(sx, "少阴") == 0) &&
            动态数组_获取(&cur->特征序列, 2) == NULL)) {
        return false;
    }
    if (!(线段序列->长度 >= 2 &&
            (strcmp(线段_四象(动态数组_获取(线段序列, 线段序列->长度 - 2)), "老阴") == 0 ||
             strcmp(线段_四象(动态数组_获取(线段序列, 线段序列->长度 - 2)), "老阳") == 0))) {
        return false;
    }

    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4);
    动态数组_初始化(&后, 4);
    动态数组_初始化(&第三, 4);
    虚线 *gs = NULL;
    线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
    if (后.长度 < 3) {
        动态数组_释放(&前, false);
        动态数组_释放(&后, false);
        动态数组_释放(&第三, false);
        return false;
    }

    bool 需要修正 = false;
    if (虚线_方向(cur) == 相对方向_向上) {
        if (相对方向_分析(虚线_高((虚线 *) 后.数据[0]), 虚线_低((虚线 *) 后.数据[0]),
                                虚线_高((虚线 *) 后.数据[2]), 虚线_低((虚线 *) 后.数据[2])) == 相对方向_向下) {
            需要修正 = true;
        }
    } else {
        if (相对方向_分析(虚线_高((虚线 *) 后.数据[0]), 虚线_低((虚线 *) 后.数据[0]),
                                虚线_高((虚线 *) 后.数据[2]), 虚线_低((虚线 *) 后.数据[2])) == 相对方向_向上) {
            需要修正 = true;
        }
    }
    if (!需要修正) {
        动态数组_释放(&前, false);
        动态数组_释放(&后, false);
        动态数组_释放(&第三, false);
        return false;
    }

    cur->短路修正 = true;
    虚线 *seg = 虚线_创建线段(&后);
    线段_添加线段(线段序列, seg, 配置);
    动态数组_释放(&前, false);
    动态数组_释放(&后, false);
    动态数组_释放(&第三, false);
    return true;
}

static bool 线段_修正(动态数组 *线段序列, 缠论配置 *配置) {
    虚线 *cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
    if (!(配置->线段_修正 && cur->基础序列.长度 >= 9)) {
        return false;
    }

    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4);
    动态数组_初始化(&后, 4);
    动态数组_初始化(&第三, 4);
    虚线 *gs = NULL;
    线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
    if (后.长度 < 6) {
        动态数组_释放(&前, false);
        动态数组_释放(&后, false);
        动态数组_释放(&第三, false);
        return false;
    }

    虚线 *a = 动态数组_获取(&后, 后.长度 - 3);
    虚线 *b = 动态数组_获取(&后, 后.长度 - 1);
    if (虚线_方向(cur) != 相对方向_分析(虚线_高(a), 虚线_低(a), 虚线_高(b), 虚线_低(b))) {
        动态数组_释放(&前, false);
        动态数组_释放(&后, false);
        动态数组_释放(&第三, false);
        return false;
    }

    cur->短路修正 = true;
    /* 创建第一个新段 */
    动态数组 seg1_base;
    动态数组_初始化(&seg1_base, 后.长度 - 3);
    for (size_t i = 0; i < 后.长度 - 3; i++) {
        动态数组_追加(&seg1_base, 后.数据[i]);
    }
    虚线 *seg1 = 虚线_创建线段(&seg1_base);
    seg1->短路修正 = true;
    线段_添加线段(线段序列, seg1, 配置);
    if (strcmp(线段_四象(cur), "老阴") == 0 || strcmp(线段_四象(cur), "老阳") == 0) {
        弱引用_设置(seg1, 前一缺口, NULL);
        seg1->前一缺口 = NULL;
    }
    动态数组_释放(&seg1_base, false);

    /* 创建第二个新段 */
    动态数组 seg2_base;
    动态数组_初始化(&seg2_base, 3);
    for (size_t i = 后.长度 - 3; i < 后.长度; i++) {
        动态数组_追加(&seg2_base, 后.数据[i]);
    }
    虚线 *seg2 = 虚线_创建线段(&seg2_base);
    线段_添加线段(线段序列, seg2, 配置);
    动态数组_释放(&seg2_base, false);

    动态数组_释放(&前, false);
    动态数组_释放(&后, false);
    动态数组_释放(&第三, false);
    return true;
}

/* ================================================================
 * 线段_分析 — 将原 Python 的尾递归转为 while 循环
 * ================================================================ */

void 线段_分析(动态数组 *笔序列, 动态数组 *线段序列, 缠论配置 *配置) {
    相对方向 关系序列[] = {相对方向_向上, 相对方向_向下};
    int 深度 = 0;

    while (深度 < 512) {
        /* ---- 1. 初始化第一个线段 ---- */
        if (线段序列->长度 == 0) {
            for (size_t i = 1; i + 1 < 笔序列->长度; i++) {
                虚线 *左 = 动态数组_获取(笔序列, i - 1);
                虚线 *中 = 动态数组_获取(笔序列, i);
                虚线 *右 = 动态数组_获取(笔序列, i + 1);
                if (!线段_基础判断(左, 中, 右, 关系序列, 2)) {
                    continue;
                }
                动态数组 segs;
                动态数组_初始化(&segs, 3);
                动态数组_追加(&segs, 左);
                动态数组_追加(&segs, 中);
                动态数组_追加(&segs, 右);
                虚线 *seg = 虚线_创建线段(&segs);
                线段_添加线段(线段序列, seg, 配置);
                /* seg->特征序列 already initialized with 3 NULLs by 虚线_创建线段 */
                动态数组 tmp = {.数据 = malloc(sizeof(void *)), .长度 = 0, .容量 = 1};
                if (!tmp.数据) {
                    perror("线段_扩展分析");
                    chan_oom_handler(sizeof(void *));
                    return;
                }
                动态数组_追加(&tmp, 中);
                线段特征 *ft = 线段特征_新建("特征", &tmp, 虚线_方向(seg));
                free(tmp.数据);
                弱引用_数组设置(&seg->特征序列, 0, ft);
                动态数组_释放(&segs, false);
                break;
            }
            if (线段序列->长度 == 0) {
                return;
            }
        }

        /* ---- 2. 清理无效尾部 ---- */
        while (线段序列->长度 > 0) {
            虚线 *last = 动态数组_获取(线段序列, 线段序列->长度 - 1);
            if (last->前一结束位置 && 动态数组_查找(笔序列, last->前一结束位置) == (size_t) - 1) {
                线段_弹出线段(线段序列, last, 配置);
            }
            else {
                break;
            }
        }
        if (线段序列->长度 == 0) {
            深度++;
            continue;
        }

        /* ---- 3. 确保当前线段有效 ---- */
        虚线 *cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        线段_序列重置(cur, 笔序列);
        if (cur->基础序列.长度 < 3) {
            线段_弹出线段(线段序列, cur, 配置);
            if (线段序列->长度 == 0) {
                深度++;
                continue;
            }
        }

        cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);

        /* ---- 4. 特征序列完整时创建新段 ---- */
step4:
        if (动态数组_获取(&cur->特征序列, 2) != NULL) {
            动态数组 前, 后, 第三;
            动态数组_初始化(&前, 4);
            动态数组_初始化(&后, 4);
            动态数组_初始化(&第三, 4);
            虚线 *gs = NULL;
            线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
            虚线 *seg = 虚线_创建线段(&后);
            线段_添加线段(线段序列, seg, 配置);
            const char *sx_cur = 线段_四象(cur);
            if (strcmp(sx_cur, "老阴") == 0 || strcmp(sx_cur, "老阳") == 0) {
                弱引用_设置(seg, 前一缺口, NULL);
                seg->前一缺口 = NULL;
            }
            动态数组_释放(&前, false);
            动态数组_释放(&后, false);
            动态数组_释放(&第三, false);
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
        if (cur->基础序列.长度 == 0) {
            fprintf(stderr, "线段_分析: 基础序列为空\n");
            return;
        }
        虚线 *last_stroke = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
        size_t si = 动态数组_查找(笔序列, last_stroke);
        if (si == (size_t) - 1) {
            for (size_t j = 0; j < 笔序列->长度; j++) {
                虚线 *s = 动态数组_获取(笔序列, j);
                if (s->序号 == last_stroke->序号) {
                    si = j;
                    break;
                }
            }
        }
        if (si == (size_t) - 1) {
            fprintf(stderr, "线段_分析: 索引异常\n");
            return;
        }
        size_t start = si + 1;

        for (size_t idx = start; idx < 笔序列->长度; idx++) {
            虚线 *stroke = 动态数组_获取(笔序列, idx);
            cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
            const char *sx = 线段_四象(cur);

            if (!线段_添加虚线(cur, stroke)) {
                break;
            }
            线段_刷新(cur, 配置);

            if (线段_缺口突破(线段序列, 配置)) {
                continue;
            }
            if (线段_非缺口下穿刺(线段序列, 配置)) {
                continue;
            }
            if (线段_缺口后紧急修正(线段序列, 配置)) {
                continue;
            }
            if (线段_修正(线段序列, 配置)) {
                continue;
            }

            cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
            if (动态数组_获取(&cur->特征序列, 2) == NULL) {
                continue;
            }

            动态数组 前, 后, 第三;
            动态数组_初始化(&前, 4);
            动态数组_初始化(&后, 4);
            动态数组_初始化(&第三, 4);
            虚线 *gs = NULL;
            线段_分割序列(cur, NULL, &前, &后, &第三, &gs);
            虚线 *seg = 虚线_创建线段(&后);
            线段_添加线段(线段序列, seg, 配置);
            if (strcmp(sx, "老阴") == 0 || strcmp(sx, "老阳") == 0) {
                弱引用_设置(seg, 前一缺口, NULL);
                seg->前一缺口 = NULL;
            }

            if (seg->基础序列.长度 > 0) {
                虚线 *seg_last = 动态数组_获取(&seg->基础序列, seg->基础序列.长度 - 1);
                if (seg_last != stroke) {
                    if (!虚线_之后是(seg_last, stroke)) {
                        动态数组_释放(&前, false);
                        动态数组_释放(&后, false);
                        动态数组_释放(&第三, false);
                        深度++;
                        goto restart;
                    }
                    线段_添加虚线(seg, stroke);
                }
            }
            线段_刷新(seg, 配置);
            动态数组_释放(&前, false);
            动态数组_释放(&后, false);
            动态数组_释放(&第三, false);
        }
        return;
restart:;
    }
    fprintf(stderr, "线段_分析: 深度超出 %d\n", 深度);
}

/* ================================================================
 * 线段 助手函数
 * ================================================================ */

void 线段_武终(虚线 *段, int 行号) {
    if (strcmp(段->模式, "文武") != 0 && 段->基础序列.长度 > 0) {
        线段_武斗(段, ((虚线 *) 动态数组_获取(&段->基础序列, 段->基础序列.长度 - 1))->武, 行号);
    }
}

void 线段_验证序列(虚线 *段, 动态数组 *序列) {
    动态数组 new_base;
    动态数组_初始化(&new_base, 4);
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        虚线 *e = 动态数组_获取(&段->基础序列, i);
        if (动态数组_查找(序列, e) == (size_t) - 1) {
            break;
        }
        if (new_base.长度 > 0 && !虚线_之后是(动态数组_获取(&new_base, new_base.长度 - 1), e)) {
            break;
        }
        动态数组_追加(&new_base, e);
    }
    for (size_t i = 0; i < new_base.长度; i++) {
        弱引用_手动增加(动态数组_获取(&new_base, i));
    }
    for (size_t i = 0; i < 段->基础序列.长度; i++) {
        弱引用_手动减少(动态数组_获取(&段->基础序列, i));
    }
    动态数组_释放(&段->基础序列, false);
    段->基础序列 = new_base;
    if (段->基础序列.长度 % 2 == 0 && 段->基础序列.长度 > 0) {
        弱引用_手动减少(动态数组_获取(&段->基础序列, 段->基础序列.长度 - 1));
        段->基础序列.长度--;
    }
}

/* ---------- 扩展线段 助手 ---------- */
static void 线段_添加扩展线段(动态数组 *线段序列, 虚线 *seg) {
    seg->模式[0] = '\0';
    strcpy(seg->模式, "高低");
    char buf[128];
    if (seg->基础序列.长度 > 0) {
        虚线 *first = 动态数组_获取(&seg->基础序列, 0);
        if (strcmp(first->标识, "笔") != 0) {
            snprintf(buf, 127, "扩展%s", seg->标识);
        }
        else {
            strcpy(buf, "扩展线段");
        }
    } else {
        strcpy(buf, "扩展线段");
    }
    strncpy(seg->标识, buf, 63);
    seg->标识[63] = '\0';

    if (线段序列->长度 > 0) {
        虚线 *prev = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        seg->序号 = prev->序号 + 1;
    }
    弱引用_数组追加(线段序列, seg);
}

/* ================================================================
 * 线段_扩展分析 — while 循环代替尾递归
 * ================================================================ */

void 线段_扩展分析(动态数组 *虚线序列, 动态数组 *线段序列, 缠论配置 *配置) {
    int 深度 = 0;
    while (深度 < 128) {
        if (虚线序列->长度 < 3) {
            return;
        }

        if (线段序列->长度 == 0) {
            for (size_t i = 1; i + 1 < 虚线序列->长度; i++) {
                虚线 *左 = 动态数组_获取(虚线序列, i - 1);
                虚线 *中 = 动态数组_获取(虚线序列, i);
                虚线 *右 = 动态数组_获取(虚线序列, i + 1);
                相对方向 rel = 相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(右), 虚线_低(右));
                if (rel == 相对方向_向下 || rel == 相对方向_向上 ||
                        rel == 相对方向_顺 || rel == 相对方向_逆 || rel == 相对方向_同) {
                    动态数组 segs;
                    动态数组_初始化(&segs, 3);
                    动态数组_追加(&segs, 左);
                    动态数组_追加(&segs, 中);
                    动态数组_追加(&segs, 右);
                    虚线 *seg = 虚线_创建线段(&segs);
                    线段_添加扩展线段(线段序列, seg);
                    动态数组_释放(&segs, false);
                    break;
                }
            }
            if (线段序列->长度 == 0) {
                return;
            }
        }

        虚线 *cur = 动态数组_获取(线段序列, 线段序列->长度 - 1);
        线段_验证序列(cur, 虚线序列);
        if (cur->基础序列.长度 < 3) {
            cur->有效性 = false;
            弱引用_手动减少(cur);
            线段序列->长度--;
            深度++;
            continue;
        }

        if (!配置->扩展线段_当下分析) {
            虚线 *左 = cur->基础序列.数据[0];
            虚线 *右 = cur->基础序列.数据[2];
            if (!相对方向_是否缺口(相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(右), 虚线_低(右)))) {
                while (cur->基础序列.长度 > 3) {
                    弱引用_数组弹出(&cur->基础序列);
                }
                线段_武终(cur, __LINE__);
            } else {
                cur->有效性 = false;
                弱引用_手动减少(cur);
                线段序列->长度--;
                深度++;
                continue;
            }
        }

        线段_武终(cur, __LINE__);
        虚线 *last = 动态数组_获取(&cur->基础序列, cur->基础序列.长度 - 1);
        if (last->序号 + 3 > ((虚线 *) 动态数组_获取(虚线序列, 虚线序列->长度 - 1))->序号) {
            return;
        }

        size_t idx = 动态数组_查找(虚线序列, last);
        if (idx == (size_t) - 1 || idx + 1 >= 虚线序列->长度) {
            return;
        }

        for (size_t i = idx + 2; i + 1 < 虚线序列->长度; i++) {
            虚线 *左 = 动态数组_获取(虚线序列, i - 1);
            虚线 *中 = 动态数组_获取(虚线序列, i);
            虚线 *右 = 动态数组_获取(虚线序列, i + 1);
            相对方向 rel = 相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(右), 虚线_低(右));
            if (相对方向_是否缺口(rel)) {
                线段_添加虚线(cur, 左);
                线段_添加虚线(cur, 中);
                线段_武终(cur, __LINE__);
                continue;
            }
            if (动态数组_查找(&cur->基础序列, 左) != (size_t) - 1) {
                continue;
            }
            动态数组 segs;
            动态数组_初始化(&segs, 3);
            动态数组_追加(&segs, 左);
            动态数组_追加(&segs, 中);
            动态数组_追加(&segs, 右);
            虚线 *seg = 虚线_创建线段(&segs);
            线段_添加扩展线段(线段序列, seg);
            动态数组_释放(&segs, false);
            深度++;
            goto restart;
        }
        return;
restart:;
    }
}

/* ================================================================
 * 中枢
 * ================================================================ */

中枢 *中枢_新建(int 序号, const char *标识, int 级别, 动态数组 *基础序列) {
    中枢 *z = 分配(sizeof(中枢), CHAN_TYPE_中枢);
    z->序号 = 序号;
    strncpy(z->标识, 标识, 127);
    z->标识[127] = '\0';
    z->级别 = 级别;
    动态数组_初始化(&z->基础序列, 4);
    for (size_t i = 0; i < 基础序列->长度 && i < 3; i++) {
        弱引用_数组追加(&z->基础序列, 动态数组_获取(基础序列, i));
    }
    z->第三买卖线 = NULL;
    z->本级_第三买卖线 = NULL;
    return z;
}

中枢 *中枢_创建(虚线 *左, 虚线 *中, 虚线 *右, int 级别, const char *标识) {
    assert(中枢_基础检查(左, 中, 右));
    动态数组 tmp = {.数据 = (void *[]){左, 中, 右}, .长度 = 3, .容量 = 3};
    char buf[128];
    snprintf(buf, 127, "%s中枢<%s>", 标识, 中->标识);
    return 中枢_新建(0, buf, 级别, &tmp);
}

中枢 *中枢_从序列中获取中枢(动态数组 *虚线序列, 相对方向 起始方向, const char *标识) {
    if (虚线序列->长度 < 3) {
        return NULL;
    }
    for (size_t i = 1; i + 1 < 虚线序列->长度; i++) {
        虚线 *左 = 动态数组_获取(虚线序列, i - 1);
        虚线 *中 = 动态数组_获取(虚线序列, i);
        虚线 *右 = 动态数组_获取(虚线序列, i + 1);
        if (中枢_基础检查(左, 中, 右) && 虚线_方向(左) == 起始方向) {
            return 中枢_创建(左, 中, 右, 0, 标识);
        }
    }
    return NULL;
}

double 中枢_高(中枢 *self) {
    double h = INFINITY;
    for (size_t i = 0; i < self->基础序列.长度 && i < 3; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (虚线_高(x) < h) {
            h = 虚线_高(x);
        }
    }
    return h;
}

double 中枢_低(中枢 *self) {
    double l = -INFINITY;
    for (size_t i = 0; i < self->基础序列.长度 && i < 3; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (虚线_低(x) > l) {
            l = 虚线_低(x);
        }
    }
    return l;
}

double 中枢_高高(中枢 *self) {
    double h = -INFINITY;
    for (size_t i = 0; i < self->基础序列.长度; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (虚线_高(x) > h) {
            h = 虚线_高(x);
        }
    }
    return h;
}

double 中枢_低低(中枢 *self) {
    double l = INFINITY;
    for (size_t i = 0; i < self->基础序列.长度; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (虚线_低(x) < l) {
            l = 虚线_低(x);
        }
    }
    return l;
}

分型 *中枢_文(中枢 *self) {
    return ((虚线 *) 动态数组_获取(&self->基础序列, 0))->文;
}

分型 *中枢_武(中枢 *self) {
    return ((虚线 *) 动态数组_获取(&self->基础序列, self->基础序列.长度 - 1))->武;
}

相对方向 中枢_方向(中枢 *self) {
    return 相对方向_翻转(虚线_方向(动态数组_获取(&self->基础序列, 0)));
}

虚线 *中枢_离开段(中枢 *self) {
    return 动态数组_获取(&self->基础序列, self->基础序列.长度 - 1);
}

bool 中枢_完整性(中枢 *self, const char *虚实) {
    if (self->基础序列.长度 == 0) {
        return false;
    }
    虚线 *first = 动态数组_获取(&self->基础序列, 0);
    if (strcmp(first->标识, "笔") == 0) {
        return self->第三买卖线 != NULL;
    }
    虚线 *last = 动态数组_获取(&self->基础序列, self->基础序列.长度 - 1);
    动态数组 *内序列 = strcmp(虚实, "实") == 0 ? &last->实_中枢序列 : &last->合_中枢序列;
    for (size_t i = 0; i < 内序列->长度; i++) {
        中枢 *inner = 动态数组_获取(内序列, i);
        if (相对方向_是否缺口(相对方向_分析(中枢_高(self), 中枢_低(self), 中枢_高(inner), 中枢_低(inner)))) {
            return true;
        }
    }
    return false;
}

void 中枢_获取序列(中枢 *self, 动态数组 *结果) {
    for (size_t i = 0; i < self->基础序列.长度; i++) {
        弱引用_数组追加(结果, self->基础序列.数据[i]);
    }
    if (self->第三买卖线) {
        弱引用_数组追加(结果, self->第三买卖线);
    }
}

void 中枢_获取扩展中枢(中枢 *self, 动态数组 *扩展中枢, 缠论配置 *配置) {
    if (self->基础序列.长度 >= 9) {
        动态数组 ext_segs;
        动态数组_初始化(&ext_segs, 4);
        线段_扩展分析(&self->基础序列, &ext_segs, 配置);
        char buf[256];
        snprintf(buf, 255, "%s_扩展中枢_", self->标识);
        中枢_分析(&ext_segs, 扩展中枢, false, buf);
        动态数组_释放(&ext_segs, false);
    }
}

bool 中枢_校验合法性(中枢 *self, 动态数组 *序列, 动态数组 *中枢序列) {
    (void) 中枢序列;
    /* 过滤无效元素 */
    size_t 有效长度 = self->基础序列.长度;
    for (size_t i = 0; i < self->基础序列.长度; i++) {
        if (动态数组_查找(序列, self->基础序列.数据[i]) == (size_t) - 1) {
            有效长度 = i;
            break;
        }
    }
    if (有效长度 < 3) {
        弱引用_设置(self, 第三买卖线, NULL);
        弱引用_设置(self, 本级_第三买卖线, NULL);
        return false;
    }
    /* 截断无效元素并递减弱引用计数 */
    while (self->基础序列.长度 > 有效长度) {
        弱引用_数组弹出(&self->基础序列);
    }

    /* 过滤缺口后的元素 */
    有效长度 = self->基础序列.长度;
    double z高 = 中枢_高(self), z低 = 中枢_低(self);
    for (size_t i = 0; i < self->基础序列.长度; i++) {
        虚线 *x = 动态数组_获取(&self->基础序列, i);
        if (相对方向_是否缺口(相对方向_分析(z高, z低, 虚线_高(x), 虚线_低(x)))) {
            有效长度 = i;
            break;
        }
    }
    while (self->基础序列.长度 > 有效长度) {
        弱引用_数组弹出(&self->基础序列);
    }
    if (self->基础序列.长度 < 3) {
        return false;
    }

    /* 检查连续性 */
    for (size_t i = 1; i < self->基础序列.长度; i++) {
        if (!虚线_之后是(动态数组_获取(&self->基础序列, i - 1), 动态数组_获取(&self->基础序列, i))) {
            return false;
        }
    }

    /* 检查重叠 — 取前3个元素的交集 */
    if (!相对方向_是否缺口(相对方向_分析(
                                       虚线_高((虚线 *) self->基础序列.数据[0]), 虚线_低((虚线 *) self->基础序列.数据[0]),
                                       虚线_高((虚线 *) self->基础序列.数据[2]), 虚线_低((虚线 *) self->基础序列.数据[2])))) {
        double 重叠高 = 中枢_高(self), 重叠低 = 中枢_低(self);
        if (重叠低 > 重叠高) {
            return false;
        }
    }

    /* 检查第三买卖线 */
    if (self->第三买卖线) {
        if (动态数组_查找(序列, self->第三买卖线) != (size_t) - 1) {
            if (!虚线_之后是(动态数组_获取(&self->基础序列, self->基础序列.长度 - 1), self->第三买卖线)) {
                弱引用_设置(self, 第三买卖线, NULL);
            } else {
                if (!相对方向_是否缺口(相对方向_分析(z高, z低,
                                               虚线_高(self->第三买卖线), 虚线_低(self->第三买卖线)))) {
                    中枢_添加虚线(self, self->第三买卖线);
                }
            }
        } else {
            弱引用_设置(self, 第三买卖线, NULL);
        }
    }
    return true;
}

void 中枢_设置第三买卖线(中枢 *self, 虚线 *线) {
    弱引用_设置(self, 第三买卖线, 线);
}

void 中枢_添加虚线(中枢 *self, 虚线 *实线) {
    弱引用_数组追加(&self->基础序列, 实线);
    弱引用_设置(self, 本级_第三买卖线, NULL);
    弱引用_设置(self, 第三买卖线, NULL);
}

const char *中枢_当前状态(中枢 *self) {
    虚线 *last = 动态数组_获取(&self->基础序列, self->基础序列.长度 - 1);
    分型 *尾部 = last->武;
    if (strcmp(last->标识, "笔") != 0) {
        尾部 = ((虚线 *) 动态数组_获取(&last->基础序列, last->基础序列.长度 - 1))->武;
    }
    相对方向 rel = 相对方向_分析(中枢_高(self), 中枢_低(self), 尾部->中->高, 尾部->中->低);
    if (rel == 相对方向_向上缺口) {
        return "中枢之上";
    }
    if (rel == 相对方向_向下缺口) {
        return "中枢之下";
    }
    return "中枢之中";
}

bool 中枢_基础检查(虚线 *左, 虚线 *中, 虚线 *右) {
    if (!虚线_之后是(左, 中)) {
        return false;
    }
    if (!虚线_之后是(中, 右)) {
        return false;
    }
    相对方向 rel = 相对方向_分析(虚线_高(左), 虚线_低(左), 虚线_高(右), 虚线_低(右));
    return rel == 相对方向_向下 || rel == 相对方向_向上 ||
           rel == 相对方向_顺 || rel == 相对方向_逆 || rel == 相对方向_同;
}

void 中枢_向中枢序列尾部添加(动态数组 *中枢序列, 中枢 *待添加中枢) {
    if (中枢序列->长度 > 0) {
        中枢 *最后一个 = 动态数组_获取(中枢序列, 中枢序列->长度 - 1);
        待添加中枢->序号 = 最后一个->序号 + 1;
        /* 序号单调性校验：直接访问字段，避免中枢_获取序列 的临时数组造成弱引用计数泄漏 */
        虚线 *last_of_prev = 最后一个->第三买卖线
                               ? 最后一个->第三买卖线
                               : 动态数组_获取(&最后一个->基础序列, 最后一个->基础序列.长度 - 1);
        虚线 *last_of_new = 待添加中枢->第三买卖线
                              ? 待添加中枢->第三买卖线
                              : 动态数组_获取(&待添加中枢->基础序列, 待添加中枢->基础序列.长度 - 1);

        if (last_of_prev->序号 > last_of_new->序号) {
            /* 单调性违反 — 仅静默丢弃，不抛异常（C 层无异常机制） */
            return;
        }
    }
    弱引用_数组追加(中枢序列, 待添加中枢);
}

中枢 *中枢_从中枢序列尾部弹出(动态数组 *中枢序列, 中枢 *待弹出中枢) {
    if (中枢序列->长度 == 0) {
        return NULL;
    }
    if (动态数组_获取(中枢序列, 中枢序列->长度 - 1) == (void *) 待弹出中枢) {
        弱引用_数组弹出(中枢序列);
        return 待弹出中枢;
    }
    return NULL;
}

/* ================================================================
 * 中枢_分析 — while 循环代替尾递归
 * ================================================================ */

void 中枢_分析(动态数组 *虚线序列, 动态数组 *中枢序列,
                   bool 跳过首部, const char *标识) {
    if (虚线序列->长度 < 3) {
        return;
    }
    int 深度 = 0;

    while (深度 < 128) {
        if (中枢序列->长度 == 0) {
            for (size_t i = 1; i + 1 < 虚线序列->长度; i++) {
                虚线 *左 = 动态数组_获取(虚线序列, i - 1);
                虚线 *中 = 动态数组_获取(虚线序列, i);
                虚线 *右 = 动态数组_获取(虚线序列, i + 1);
                if (!中枢_基础检查(左, 中, 右)) {
                    continue;
                }
                size_t 序号 = 动态数组_查找(虚线序列, 左);
                if (序号 == (size_t)-1) {
                    printf("数据溢出");
                }
                if (跳过首部 && (左->序号 == 0 || 序号 == 0)) {
                    continue;
                }
                if (序号 >= 2) {
                    相对方向 rel = 相对方向_分析(
                                           虚线_高((虚线 *) 动态数组_获取(虚线序列, 序号 - 2)),
                                           虚线_低((虚线 *) 动态数组_获取(虚线序列, 序号 - 2)),
                                           虚线_高(左), 虚线_低(左));
                    if (相对方向_是否向上(rel) && 虚线_方向(左) == 相对方向_向上) {
                        continue;
                    }
                    if (相对方向_是否向下(rel) && 虚线_方向(左) == 相对方向_向下) {
                        continue;
                    }
                }
                中枢 *z = 中枢_创建(左, 中, 右, 中->级别, 标识);
                中枢_向中枢序列尾部添加(中枢序列, z);
                深度++;
                goto restart;
            }
            return;
        }

        中枢 *cur = 动态数组_获取(中枢序列, 中枢序列->长度 - 1);
        if (!中枢_校验合法性(cur, 虚线序列, 中枢序列)) {
            中枢_从中枢序列尾部弹出(中枢序列, cur);
            深度++;
            continue;
        }

        size_t last_idx = 动态数组_查找(虚线序列, cur->基础序列.数据[cur->基础序列.长度 - 1]);
        if (last_idx == (size_t) - 1) {
            深度++;
            continue;
        }
        size_t start = last_idx + 1;

        /* 收集后续元素 */
        动态数组 后续;
        动态数组_初始化(&后续, 8);
        for (size_t idx = start; idx < 虚线序列->长度; idx++) {
            虚线 *x = 动态数组_获取(虚线序列, idx);
            if (相对方向_是否缺口(相对方向_分析(中枢_高(cur), 中枢_低(cur), 虚线_高(x), 虚线_低(x)))) {
                动态数组_追加(&后续, x);
                if (虚线_之后是((虚线 *) cur->基础序列.数据[cur->基础序列.长度 - 1], x)) {
                    弱引用_设置(cur, 第三买卖线, x);
                }
            } else {
                if (后续.长度 == 0) {
                    assert(虚线_之后是((虚线 *) cur->基础序列.数据[cur->基础序列.长度 - 1], x));
                    中枢_添加虚线(cur, x);
                } else {
                    动态数组_追加(&后续, x);
                }
            }

            while (后续.长度 >= 3) {
                中枢 *z = 中枢_从序列中获取中枢(&后续, 相对方向_翻转(虚线_方向(
                                (虚线 *) cur->基础序列.数据[cur->基础序列.长度 - 1])), 标识);
                if (!z) {
                    /* pop front */
                    for (size_t j = 1; j < 后续.长度; j++) {
                        后续.数据[j - 1] = 后续.数据[j];
                    }
                    后续.长度--;
                } else {
                    中枢_向中枢序列尾部添加(中枢序列, z);
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

void 释放中枢(中枢 *obj) {
    弱引用_数组清除(&obj->基础序列);
    弱引用_设置(obj, 第三买卖线, NULL);
    弱引用_设置(obj, 本级_第三买卖线, NULL);
}

void 中枢_获取数据文本(中枢 *self, char *buf, size_t buf_size) {
    分型 *文 = 中枢_文(self);
    分型 *武 = 中枢_武(self);
    char 三买str[1024];
    char 本级三买str[1024];
    _虚线到字符串(self->第三买卖线, 三买str, sizeof(三买str));
    _虚线到字符串(self->本级_第三买卖线, 本级三买str, sizeof(本级三买str));
    snprintf(buf, buf_size,
             "%s, %d, %d, 文:(%ld,%g), 武:(%ld,%g), %s, %s",
             self->标识, self->序号, self->级别,
             (long) 文->时间戳, 文->分型特征值,
             (long) 武->时间戳, 武->分型特征值,
             三买str, 本级三买str);
}

/* ================================================================
 * 背驰分析
 * ================================================================ */

static void 背驰_获取MACD(虚线 *段, K线 **普K序列, size_t 普K长,
                              double *阳, double *阴, double *合, double *总) {
    K线 **sub = NULL;
    size_t sub_len = 0;
    /* 找标的K线索引 */
    size_t si = (size_t) - 1, ei = (size_t) - 1;
    for (size_t i = 0; i < 普K长; i++) {
        if (普K序列[i] == 段->文->中->标的K线) {
            si = i;
        }
        if (普K序列[i] == 段->武->中->标的K线) {
            ei = i;
            break;
        }
    }
    if (si != (size_t) - 1 && ei != (size_t) - 1 && ei >= si) {
        sub = 普K序列 + si;
        sub_len = ei - si + 1;
    }
    K线_获取MACD(普K序列, 普K长, 段->文->中->标的K线, 段->武->中->标的K线, 阳, 阴, 合, 总);
    (void) sub;
    (void) sub_len;
}

bool 背驰分析_MACD背驰(虚线 *进入段, 虚线 *离开段, K线 **序列, size_t 长度, const char *方式) {
    double 进阳, 进阴, 进合, 进总, 离阳, 离阴, 离合, 离总;
    背驰_获取MACD(进入段, 序列, 长度, &进阳, &进阴, &进合, &进总);
    背驰_获取MACD(离开段, 序列, 长度, &离阳, &离阴, &离合, &离总);

    double 进面积 = (strcmp(方式, "总") == 0) ? 进总 : (虚线_方向(进入段) == 相对方向_向上 ? 进阳 : 进阴);
    double 离面积 = (strcmp(方式, "总") == 0) ? 离总 : (虚线_方向(进入段) == 相对方向_向上 ? 离阳 : 离阴);
    return fabs(离面积) < fabs(进面积);
}

bool 背驰分析_斜率背驰(虚线 *进入段, 虚线 *离开段) {
    double dx1 = difftime(进入段->武->时间戳, 进入段->文->时间戳);
    double dy1 = 进入段->武->分型特征值 - 进入段->文->分型特征值;
    double s1 = dy1 / dx1;

    double dx2 = difftime(离开段->武->时间戳, 离开段->文->时间戳);
    double dy2 = 离开段->武->分型特征值 - 离开段->文->分型特征值;
    double s2 = dy2 / dx2;

    if (虚线_方向(进入段) == 相对方向_向上) {
        return 虚线_高(离开段) > 虚线_高(进入段) && fabs(s2) < fabs(s1);
    }
    else {
        return 虚线_低(离开段) < 虚线_低(进入段) && fabs(s2) < fabs(s1);
    }
}

bool 背驰分析_测度背驰(虚线 *进入段, 虚线 *离开段) {
    double dx1 = difftime(进入段->武->时间戳, 进入段->文->时间戳);
    double dy1 = 进入段->武->分型特征值 - 进入段->文->分型特征值;
    double m1 = sqrt(dx1 * dx1 + dy1 * dy1);

    double dx2 = difftime(离开段->武->时间戳, 离开段->文->时间戳);
    double dy2 = 离开段->武->分型特征值 - 离开段->文->分型特征值;
    double m2 = sqrt(dx2 * dx2 + dy2 * dy2);

    if (虚线_方向(进入段) == 相对方向_向上) {
        return 虚线_高(离开段) > 虚线_高(进入段) && fabs(m2) < fabs(m1);
    }
    else {
        return 虚线_低(离开段) < 虚线_低(进入段) && fabs(m2) < fabs(m1);
    }
}

bool 背驰分析_全量背驰(虚线 *进入段, 虚线 *离开段, K线 **序列, size_t 长度) {
    return 背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总") &&
           背驰分析_测度背驰(进入段, 离开段) &&
           背驰分析_斜率背驰(进入段, 离开段);
}

bool 背驰分析_任意背驰(虚线 *进入段, 虚线 *离开段, K线 **序列, size_t 长度) {
    return 背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总") ||
           背驰分析_测度背驰(进入段, 离开段) ||
           背驰分析_斜率背驰(进入段, 离开段);
}

bool 背驰分析_配置背驰(虚线 *进入段, 虚线 *离开段, K线 **序列, size_t 长度, 缠论配置 *配置) {
    bool a = 背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总");
    bool b = 背驰分析_测度背驰(进入段, 离开段);
    bool c = 背驰分析_斜率背驰(进入段, 离开段);

    if (配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) {
        return a && b && c;
    }
    if (!配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) {
        return false;
    }
    if (配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) {
        return a && c;
    }
    if (!配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) {
        return b;
    }
    if (配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) {
        return a;
    }
    if (!配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) {
        return b && c;
    }
    if (!配置->线段内部背驰_MACD && !配置->线段内部背驰_测度 && 配置->线段内部背驰_斜率) {
        return c;
    }
    if (配置->线段内部背驰_MACD && 配置->线段内部背驰_测度 && !配置->线段内部背驰_斜率) {
        return a && b;
    }
    return false;
}

bool 背驰分析_任选背驰(虚线 *进入段, 虚线 *离开段, K线 **序列, size_t 长度) {
    int count = 0;
    if (背驰分析_MACD背驰(进入段, 离开段, 序列, 长度, "总")) {
        count++;
    }
    if (背驰分析_测度背驰(进入段, 离开段)) {
        count++;
    }
    if (背驰分析_斜率背驰(进入段, 离开段)) {
        count++;
    }
    return count >= 2;
}

bool 背驰分析_背驰模式(虚线 *进入段, 虚线 *离开段, K线 **序列, size_t 长度, 缠论配置 *配置, const char *模式) {
    if (strcmp(模式, "全量") == 0) {
        return 背驰分析_全量背驰(进入段, 离开段, 序列, 长度);
    }
    if (strcmp(模式, "任意") == 0) {
        return 背驰分析_任意背驰(进入段, 离开段, 序列, 长度);
    }
    if (strcmp(模式, "配置") == 0) {
        return 背驰分析_配置背驰(进入段, 离开段, 序列, 长度, 配置);
    }
    if (strcmp(模式, "相对") == 0) {
        return 背驰分析_任选背驰(进入段, 离开段, 序列, 长度);
    }
    return false;
}

/* ================================================================
 * 观察者
 * ================================================================ */

观察者 *观察者_新建(const char *符号, int 周期, 缠论配置 *配置) {
    /* 观察者是顶层协调器，拥有池生命周期，不使用池分配（避免释放池后自身悬空） */
    观察者 *o = calloc(1, sizeof(观察者));
    if (!o) {
        return NULL;
    }
    ((对象头结构 *) o)->引用计数 = 1;
    ((对象头结构 *) o)->类型标记 = CHAN_TYPE_观察者;
    ((对象头结构 *) o)->所属内存池 = NULL; /* 堆分配，解引用归零时 free */
    ((对象头结构 *) o)->下一清理对象 = NULL;
    ((对象头结构 *) o)->已销毁 = false;
    分配计数[CHAN_TYPE_观察者]++;
    strncpy(o->符号, 符号, 63);
    o->符号[63] = '\0';
    o->周期 = 周期;
    配置->标识[0] = '\0';
    strncpy(配置->标识, 符号, 63);
    o->配置 = 配置;
    引用(配置);

    o->有终止 = (配置->手动终止[0] != '\0');
    o->终止时间戳 = o->有终止 ? 转化为时间戳(配置->手动终止) : 0;

    o->基础缠K序列 = calloc(1, sizeof(动态数组));
    o->基础缠K自持 = true;
    动态数组_初始化(o->基础缠K序列, 128);
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

    return o;
}

void 观察者_增加原始K线(观察者 *self, K线 *普K) {
    if (self->有终止 && 普K->时间戳 > self->终止时间戳) {
        return;
    }

    const char *状态 = NULL;
    分型 *当前分型 = NULL;
    缠论K线_分析(普K, &self->缠论K线序列, &self->普通K线序列, self->配置, &状态, &当前分型);
    if (!当前分型) {
        return;
    }

    size_t 笔序列_之前长度 = self->笔序列.长度;
    if (self->配置->分析笔)
        笔_分析(当前分型, &self->分型序列, &self->笔序列,
                   &self->缠论K线序列, &self->普通K线序列, self->配置);
    if (self->分型序列.长度 == 0) {
        return;
    }
    if (self->笔序列.长度 == 0) {
        return;
    }

    if (self->笔序列.长度 < 笔序列_之前长度) {
        return;
    }

    /* Stage: 0=笔中枢, 1=+线段, 2=+段中枢, 3=+扩段(pens), 4=+扩段(段), 5=+段_段 6=+扩扩段 7=全部 */
    int _S = 7;
#define _STAGE(n) if (_S < (n)) return

    if (self->配置->分析笔中枢) {
        中枢_分析(&self->笔序列, &self->笔_中枢序列, true, "");
    }
    _STAGE(1);

    if (self->配置->分析线段) {
        线段_分析(&self->笔序列, &self->线段序列, self->配置);
    }
    _STAGE(2);

    if (self->配置->分析线段中枢) {
        中枢_分析(&self->线段序列, &self->中枢序列, true, "");
    }
    _STAGE(3);

    if (self->配置->分析扩展线段) {
        线段_扩展分析(&self->笔序列, &self->扩展线段序列, self->配置);
    }
    if (self->配置->分析线段中枢) {
        中枢_分析(&self->扩展线段序列, &self->扩展中枢序列, true, "");
    }
    _STAGE(4);

    if (self->配置->分析扩展线段) {
        线段_扩展分析(&self->线段序列, &self->扩展线段序列_线段, self->配置);
    }
    if (self->配置->分析线段中枢) {
        中枢_分析(&self->扩展线段序列_线段, &self->扩展中枢序列_线段, true, "");
    }
    _STAGE(5);

    if (self->配置->分析线段) {
        线段_分析(&self->线段序列, &self->线段_线段序列, self->配置);
    }
    if (self->配置->分析线段中枢) {
        中枢_分析(&self->线段_线段序列, &self->线段_中枢序列, true, "");
    }
    _STAGE(6);

    if (self->配置->分析扩展线段) {
        线段_扩展分析(&self->扩展线段序列, &self->扩展线段序列_扩展线段, self->配置);
    }
    if (self->配置->分析线段中枢) {
        中枢_分析(&self->扩展线段序列_扩展线段, &self->扩展中枢序列_扩展线段, true, "");
    }
}
#undef _STAGE

观察者 *观察者_读取数据文件(const char *文件路径, 缠论配置 *配置) {
    /* 解析文件名: symbol-period-start-end.nb */
    const char *name = strrchr(文件路径, '/');
    if (!name) {
        name = 文件路径;
    }
    else {
        name++;
    }
    char name_buf[256];
    strncpy(name_buf, name, 255);
    name_buf[255] = '\0';
    char *dot = strrchr(name_buf, '.');
    if (dot) {
        *dot = '\0';
    }

    char 符号[64] = "", 周期_str[32] = "";
    sscanf(name_buf, "%63[^-]-%31s", 符号, 周期_str);
    int 周期 = atoi(周期_str);

    观察者 *obs = 观察者_新建(符号, 周期, 配置);

    FILE *f = fopen(文件路径, "rb");
    if (!f) {
        perror(文件路径);
        return obs;
    }

    fseeko(f, 0, SEEK_END);
    off_t fsize = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (fsize <= 0) {
        fclose(f);
        return obs;
    }

    uint8_t *buffer = malloc((size_t) fsize);
    if (!buffer) {
        fclose(f);
        return obs;
    }
    fread(buffer, 1, (size_t) fsize, f);
    fclose(f);

    off_t record_size = 48; /* 6 * 8 bytes */
    int _max_klines = 0;
    for (off_t i = 0; i + record_size <= fsize; i += record_size) {
        if (_max_klines > 0 && (int) (i / record_size) >= _max_klines) {
            break;
        }
        K线 *k = K线_读取大端字节数组(buffer + i, 周期, "Bar");
        k->序号 = (int) (i / record_size);
        观察者_增加原始K线(obs, k);
    }
    free(buffer);
    return obs;
}

void 观察者_重置基础序列(观察者 *self) {
    弱引用_数组清空(&self->普通K线序列);
    弱引用_数组清空(&self->缠论K线序列);
    弱引用_数组清空(self->基础缠K序列);
    弱引用_数组清空(&self->分型序列);
    弱引用_数组清空(&self->笔序列);
    弱引用_数组清空(&self->笔_中枢序列);
    弱引用_数组清空(&self->线段序列);
    弱引用_数组清空(&self->中枢序列);
    弱引用_数组清空(&self->扩展线段序列);
    弱引用_数组清空(&self->扩展中枢序列);
    弱引用_数组清空(&self->扩展线段序列_线段);
    弱引用_数组清空(&self->扩展中枢序列_线段);
    弱引用_数组清空(&self->线段_线段序列);
    弱引用_数组清空(&self->线段_中枢序列);
    弱引用_数组清空(&self->扩展线段序列_扩展线段);
    弱引用_数组清空(&self->扩展中枢序列_扩展线段);
}

void 观察者_设置基础缠K序列(观察者 *self, 动态数组 *外部序列) {
    if (!外部序列) {
        return;
    }
    /* 清除自持数组内容，但不释放指针壳（之后可能重用） */
    弱引用_数组清除(self->基础缠K序列);
    self->基础缠K序列 = 外部序列;
    self->基础缠K自持 = false;
}

void 释放观察者(观察者 *obj) {
    /*
     * 清理：
     *   1. 移除所有序列引用（弱引用）
     *   2. 释放配置
     *   注意：不在此触发池对象级联销毁（全局池为多观察者共享，
     *   过早销毁会破坏其他观察者持有的对象）。
     *   级联销毁由 释放全局内存池() 统一执行。
     */

    /* ======== Phase 1: 移除观察者序列的弱引用 ======== */
#define 清理序列引用(seq) 弱引用_数组清除(&(seq))

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
    弱引用_数组清除(obj->基础缠K序列);
    清理序列引用(obj->缠论K线序列);

    /* 普通K线层 */
    清理序列引用(obj->普通K线序列);

#undef 清理序列引用

    /* 基础缠K序列 — 仅自有指针壳释放 */
    if (obj->基础缠K自持 && obj->基础缠K序列) {
        free(obj->基础缠K序列);
    }
    obj->基础缠K序列 = NULL;

    /* ======== Phase 2: 释放配置（池内对象，由池统一回收内存） ======== */
    if (obj->配置) {
        解引用(obj->配置);
        obj->配置 = NULL;
    }

    /* 全局内存池 不在此释放——它是单例，可能被多个观察者共享。
     * 调用者应在所有观察者释放后手动调用 释放全局内存池()。
     * Python 端可通过 _core._释放全局内存池() 手动触发。 */
}

void 观察者_测试_保存数据(观察者 *self, const char *root) {
    if (self->普通K线序列.长度 == 0) {
        return;
    }

    K线 *首 = 动态数组_获取(&self->普通K线序列, 0);
    K线 *尾 = 动态数组_获取(&self->普通K线序列, self->普通K线序列.长度 - 1);

    char 子目录名[256];
    snprintf(子目录名, sizeof(子目录名), "C99_%s:%d_%ld_%ld",
             self->符号, self->周期, (long) 首->时间戳, (long) 尾->时间戳);

    char 目录名[640];
    if (root && root[0]) {
        snprintf(目录名, sizeof(目录名), "%s/%s", root, 子目录名);
    } else {
        snprintf(目录名, sizeof(目录名), "%s", 子目录名);
    }
    mkdir(目录名, 0755);

    typedef enum { 文本_虚线, 文本_中枢 } 文本类型;
    typedef struct {
        const char *文件名;
        动态数组 *序列;
        文本类型 类型;
    } 保存条目;

    const 保存条目 条目们[] = {
        {"笔序列_文本数据",                &self->笔序列,               文本_虚线},
        {"线段序列_文本数据",              &self->线段序列,             文本_虚线},
        {"扩展线段序列_数据文本",          &self->扩展线段序列,         文本_虚线},
        {"扩展线段序列_线段_数据文本",     &self->扩展线段序列_线段,    文本_虚线},
        {"线段_线段序列_数据文本",         &self->线段_线段序列,        文本_虚线},
        {"扩展线段序列_扩展线段_数据文本", &self->扩展线段序列_扩展线段, 文本_虚线},
        {"笔_中枢序列_数据文本",           &self->笔_中枢序列,          文本_中枢},
        {"中枢序列_数据文本",              &self->中枢序列,             文本_中枢},
        {"扩展中枢序列_数据文本",          &self->扩展中枢序列,         文本_中枢},
        {"扩展中枢序列_线段_数据文本",     &self->扩展中枢序列_线段,    文本_中枢},
        {"线段_中枢序列_数据文本",         &self->线段_中枢序列,        文本_中枢},
        {"扩展中枢序列_扩展线段_数据文本", &self->扩展中枢序列_扩展线段, 文本_中枢},
    };

    for (size_t i = 0; i < sizeof(条目们) / sizeof(条目们[0]); i++) {
        char 路径[1280];
        snprintf(路径, sizeof(路径), "%s/%s.txt", 目录名, 条目们[i].文件名);
        FILE *f = fopen(路径, "w");
        if (!f) {
            continue;
        }
        for (size_t j = 0; j < 条目们[i].序列->长度; j++) {
            char buf[131072];
            void *elem = 动态数组_获取(条目们[i].序列, j);
            if (条目们[i].类型 == 文本_虚线) {
                虚线_获取数据文本((虚线 *) elem, buf, sizeof(buf));
            }
            else {
                中枢_获取数据文本((中枢 *) elem, buf, sizeof(buf));
            }
            fprintf(f, "%s\n", buf);
        }
        fclose(f);
    }

    printf("全部数据拆分保存完成，目录：%s\n", 目录名);
}


/* ================================================================
 *  K线合成器 — 将低周期K线合成为高周期K线
 * ================================================================ */

static time_t _对齐时间戳(time_t ts, int 周期) {
    return (ts / 周期) * 周期;
}

static K线 *_创建合成K线(K线合成器 *self, int i, time_t 时间戳, K线 *普K) {
    int 周期 = self->周期组[i];
    int 序号 = 0;
    动态数组 *列表 = &self->合成K线列表[i];
    if (列表->长度 > 0) {
        K线 *last = 动态数组_获取(列表, 列表->长度 - 1);
        序号 = last->序号 + 1;
    }
    return K线_创建普K(self->标识, 时间戳,
                           普K->开盘价, 普K->高, 普K->低, 普K->收盘价, 普K->成交量,
                           序号, 周期);
}

static void _更新合成K线(K线 *当前, K线 *新数据) {
    当前->高 = fmax(当前->高, 新数据->高);
    当前->低 = fmin(当前->低, 新数据->低);
    当前->收盘价 = 新数据->收盘价;
    当前->成交量 += 新数据->成交量;
}

static void _完成合成K线(K线合成器 *self, int i) {
    K线 *k = self->当前K线[i];
    if (!k) {
        return;
    }

    动态数组 *列表 = &self->合成K线列表[i];
    if (列表->长度 > 0) {
        K线 *last = 动态数组_获取(列表, 列表->长度 - 1);
        k->序号 = last->序号 + 1;
    }
    弱引用_数组追加(列表, k);

    /* 必须在回调之前置空，否则回调中获取当前K线会拿到已完成K线，导致重复投喂 */
    self->当前K线[i] = NULL;

    if (self->事件回调) {
        self->事件回调(self->回调上下文, "K线完成", self->标识,
                           self->周期组[i], k);
    }
}

static void _处理单个合成周期(K线合成器 *self, int i, K线 *普K) {
    int 周期 = self->周期组[i];
    time_t 目标时间戳 = _对齐时间戳(普K->时间戳, 周期);
    K线 *当前 = self->当前K线[i];

    if (!当前) {
        self->当前K线[i] = _创建合成K线(self, i, 目标时间戳, 普K);
    } else if (当前->时间戳 == 目标时间戳) {
        _更新合成K线(当前, 普K);
    } else {
        _完成合成K线(self, i);
        self->当前K线[i] = _创建合成K线(self, i, 目标时间戳, 普K);
    }
}

K线合成器 *K线合成器_新建(const char *标识, const int *周期组, int 周期数量,
                                    K线合成器回调 回调, void *回调上下文) {
    if (周期数量 > K线合成器_最大周期数) {
        周期数量 = K线合成器_最大周期数;
    }

    K线合成器 *self = 分配(sizeof(K线合成器), CHAN_TYPE_K线合成器);
    strncpy(self->标识, 标识, sizeof(self->标识) - 1);
    self->标识[sizeof(self->标识) - 1] = '\0';
    self->周期数量 = 周期数量;

    /* 复制并排序周期组 */
    for (int i = 0; i < 周期数量; i++) {
        self->周期组[i] = 周期组[i];
    }
    for (int i = 0; i < 周期数量 - 1; i++) {
        for (int j = i + 1; j < 周期数量; j++) {
            if (self->周期组[i] > self->周期组[j]) {
                int tmp = self->周期组[i];
                self->周期组[i] = self->周期组[j];
                self->周期组[j] = tmp;
            }
        }
    }

    for (int i = 0; i < 周期数量; i++) {
        self->当前K线[i] = NULL;
        动态数组_初始化(&self->合成K线列表[i], 16);
    }

    self->事件回调 = 回调;
    self->回调上下文 = 回调上下文;
    return self;
}

void K线合成器_投喂(K线合成器 *self, time_t 时间戳, double 开, double 高,
                          double 低, double 收, double 量) {
    K线 *普K = K线_创建普K(self->标识, 时间戳, 开, 高, 低, 收, 量, 0, 0);
    引用(普K);
    K线合成器_投喂K线(self, 普K);
    解引用(普K);
}

void K线合成器_投喂K线(K线合成器 *self, K线 *普K) {
    for (int i = 0; i < self->周期数量; i++) {
        _处理单个合成周期(self, i, 普K);
    }
}

K线 *K线合成器_获取当前K线(K线合成器 *self, int 周期) {
    for (int i = 0; i < self->周期数量; i++) {
        if (self->周期组[i] == 周期) {
            return self->当前K线[i];
        }
    }
    return NULL;
}

void 释放K线合成器(K线合成器 *obj) {
    if (!obj) {
        return;
    }
    for (int i = 0; i < obj->周期数量; i++) {
        弱引用_数组清除(&obj->合成K线列表[i]);
        obj->当前K线[i] = NULL;
    }
}

/* ================================================================
 *  立体分析器 — 多级别联立分析
 * ================================================================ */

static void _立体_K线回调(void *上下文, const char *信号类型, const char *标识,
                               int 周期, K线 *完成K线) {
    (void) 信号类型;
    (void) 标识;
    立体分析器 *self = (立体分析器 *) 上下文;
    /* 找到对应周期的观察者并投喂 */
    for (int i = 0; i < self->周期数量; i++) {
        if (self->周期组[i] == 周期) {
            观察者_增加原始K线(self->单体分析器[i], 完成K线);
            /* 同时投喂当前未完成的K线（实时更新） */
            K线 *当前K = K线合成器_获取当前K线(self->合成器, 周期);
            if (当前K) {
                观察者_增加原始K线(self->单体分析器[i], 当前K);
            }
            return;
        }
    }
}

立体分析器 *立体分析器_新建(const char *符号, const int *周期组, int 周期数量,
                                        缠论配置 *配置, 缠论配置 **配置组) {
    if (周期数量 > K线合成器_最大周期数) {
        周期数量 = K线合成器_最大周期数;
    }

    立体分析器 *self = 分配(sizeof(立体分析器), CHAN_TYPE_立体分析器);
    strncpy(self->符号, 符号, sizeof(self->符号) - 1);
    self->符号[sizeof(self->符号) - 1] = '\0';
    self->周期数量 = 周期数量;

    for (int i = 0; i < 周期数量; i++) {
        self->周期组[i] = 周期组[i];
    }
    self->输入周期 = 周期组[0];
    self->显示周期 = 周期组[1];

    /* 创建 K线合成器 */
    self->合成器 = K线合成器_新建(符号, 周期组, 周期数量,
                                           _立体_K线回调, self);

    /* 创建每个周期的观察者 */
    for (int i = 0; i < 周期数量; i++) {
        int 周期 = self->周期组[i];
        缠论配置 *per配置 = (配置组 && 配置组[周期]) ? 配置组[周期] : 配置;
        per配置->推送K线 = false;
        per配置->推送线段 = false;
        self->单体分析器[i] = 观察者_新建(符号, 周期, per配置);
    }

    /* 显示周期启用推送和展示 */
    观察者 *显示观察者 = self->单体分析器[1];
    显示观察者->配置->推送K线 = true;
    显示观察者->配置->推送笔 = true;
    显示观察者->配置->推送线段 = true;
    显示观察者->配置->图表展示 = true;
    观察者_重置基础序列(显示观察者);

    /* 将各周期的缠K序列对齐到显示周期 */
    for (int i = 0; i < 周期数量; i++) {
        if (self->周期组[i] != self->显示周期) {
            观察者_设置基础缠K序列(self->单体分析器[i],
                                             &显示观察者->缠论K线序列);
        }
    }

    return self;
}

void 立体分析器_投喂K线(立体分析器 *self, K线 *普K) {
    if (普K->周期 != self->输入周期) {
        fprintf(stderr, "立体分析器.投喂K线: 周期不匹配 %d != %d\n",
                普K->周期, self->输入周期);
        return;
    }
    K线合成器_投喂K线(self->合成器, 普K);
}

void 立体分析器_测试_保存数据(立体分析器 *self) {
    观察者 *输入观察者 = self->单体分析器[0];
    if (输入观察者->普通K线序列.长度 == 0) {
        return;
    }

    K线 *首 = 动态数组_获取(&输入观察者->普通K线序列, 0);
    K线 *尾 = 动态数组_获取(&输入观察者->普通K线序列,
                                    输入观察者->普通K线序列.长度 - 1);

    char 目录名[256];
    snprintf(目录名, sizeof(目录名), "C99M_%s:%d_%ld_%ld",
             输入观察者->符号, 输入观察者->周期,
             (long) 首->时间戳, (long) 尾->时间戳);
    mkdir(目录名, 0755);

    for (int i = 0; i < self->周期数量; i++) {
        观察者_测试_保存数据(self->单体分析器[i], 目录名);
    }
    printf("多级别数据拆分保存完成，目录：%s\n", 目录名);
}

void 释放立体分析器(立体分析器 *obj) {
    if (!obj) {
        return;
    }
    for (int i = 0; i < obj->周期数量; i++) {
        if (obj->单体分析器[i]) {
            解引用(obj->单体分析器[i]);
            obj->单体分析器[i] = NULL;
        }
    }
    if (obj->合成器) {
        释放K线合成器(obj->合成器);
        obj->合成器 = NULL;
    }
}


/* ================================================================
 *  验证弱引用计数 — 从观察者和池内交叉视角独立重建
 * ================================================================ */

typedef struct {
    void *ptr;
    int 存储值;
    int 观察者贡献;
} 弱引用快照项;

typedef struct {
    void *ptr;
    size_t index;
} 指针索引项;

static int 指针索引_比较(const void *a, const void *b) {
    uintptr_t pa = (uintptr_t)((const 指针索引项 *) a)->ptr;
    uintptr_t pb = (uintptr_t)((const 指针索引项 *) b)->ptr;
    return (pa > pb) - (pa < pb);
}

static size_t 查找索引(指针索引项 *idx, size_t n, void *ptr) {
    if (!ptr) {
        return (size_t) - 1;
    }
    指针索引项 key = {.ptr = ptr};
    指针索引项 *found = bsearch(&key, idx, n, sizeof(指针索引项), 指针索引_比较);
    return found ? found->index : (size_t) - 1;
}

#define 弱引用_快照贡献(ptr, snaps, idx_arr, n) do { \
        if (ptr) { \
            ((对象头结构*)(ptr))->弱引用计数++; \
            size_t _si = 查找索引(idx_arr, n, ptr); \
            if (_si < n) snaps[_si].观察者贡献++; \
        } \
    } while(0)

#define 快照_数组贡献(arr, snaps, idx_arr, n) do { \
        for (size_t _j = 0; _j < (arr).长度; _j++) \
            弱引用_快照贡献((arr).数据[_j], snaps, idx_arr, n); \
    } while(0)

void 验证弱引用计数(观察者 *obs) {
    内存池 *pool = 获取全局池();
    if (!pool) {
        fprintf(stderr, "验证弱引用计数: 全局池为空\n");
        return;
    }

    /* ---- 1. 收集快照 ---- */
    size_t count = 0;
    void *cursor = pool->清理头;
    while (cursor) {
        if (!已销毁(cursor)) {
            count++;
        }
        cursor = ((对象头结构 *) cursor)->下一清理对象;
    }

    if (count == 0) {
        fprintf(stderr, "验证弱引用计数: 池为空\n");
        return;
    }

    弱引用快照项 *snaps = calloc(count, sizeof(弱引用快照项));
    指针索引项 *idxs = calloc(count, sizeof(指针索引项));
    if (!snaps || !idxs) {
        free(snaps);
        free(idxs);
        return;
    }

    size_t n = 0;
    size_t 已销毁数 = 0;
    cursor = pool->清理头;
    while (cursor) {
        if (!已销毁(cursor)) {
            snaps[n].ptr = cursor;
            snaps[n].存储值 = ((对象头结构 *) cursor)->弱引用计数;
            idxs[n].ptr = cursor;
            idxs[n].index = n;
            n++;
        } else {
            已销毁数++;
        }
        cursor = ((对象头结构 *) cursor)->下一清理对象;
    }
    if (已销毁数 > 0) {
        fprintf(stderr, "验证弱引用计数: 跳过 %zu 个已销毁对象\n", 已销毁数);
    }

    /* ---- 2. 重置所有弱引用计数 ---- */
    for (size_t i = 0; i < n; i++) {
        ((对象头结构 *) snaps[i].ptr)->弱引用计数 = 0;
    }

    /* 排序指针索引供二分查找 */
    qsort(idxs, n, sizeof(指针索引项), 指针索引_比较);

    /* ---- 3a. 观察者视角重建 ---- */
    size_t obs_refs = 0;

#define OBS_SEQ(seq) do { \
        for (size_t _j = 0; _j < obs->seq.长度; _j++) { \
            void* _e = obs->seq.数据[_j]; \
            if (_e) { \
                ((对象头结构*)_e)->弱引用计数++; \
                obs_refs++; \
                size_t _si = 查找索引(idxs, n, _e); \
                if (_si < n) snaps[_si].观察者贡献++; \
            } \
        } \
    } while(0)

    OBS_SEQ(普通K线序列);
    OBS_SEQ(缠论K线序列);
    for (size_t _j = 0; _j < obs->基础缠K序列->长度; _j++) {
        void* _e = obs->基础缠K序列->数据[_j];
        if (_e) {
            ((对象头结构*)_e)->弱引用计数++;
            obs_refs++;
            size_t _si = 查找索引(idxs, n, _e);
            if (_si < n) {
                snaps[_si].观察者贡献++;
            }
        }
    }
    OBS_SEQ(分型序列);
    OBS_SEQ(笔序列);
    OBS_SEQ(笔_中枢序列);
    OBS_SEQ(线段序列);
    OBS_SEQ(中枢序列);
    OBS_SEQ(扩展线段序列);
    OBS_SEQ(扩展中枢序列);
    OBS_SEQ(扩展线段序列_线段);
    OBS_SEQ(扩展中枢序列_线段);
    OBS_SEQ(线段_线段序列);
    OBS_SEQ(线段_中枢序列);
    OBS_SEQ(扩展线段序列_扩展线段);
    OBS_SEQ(扩展中枢序列_扩展线段);

    /* 观察者 → 配置 是强引用（计划中保留），不纳入弱引用验证 */

#undef OBS_SEQ

    /* ---- 3b. 池内交叉引用视角重建 ---- */

    /* 仅递增目标 弱引用计数，不记入观察者贡献 */
#define 池内_指针(field) do { if (obj->field) ((对象头结构*)(obj->field))->弱引用计数++; } while(0)
#define 池内_数组(arr)   do { \
        for (size_t _j = 0; _j < obj->arr.长度; _j++) { \
            void* _e = obj->arr.数据[_j]; \
            if (_e) ((对象头结构*)_e)->弱引用计数++; \
        } \
    } while(0)

    size_t pool_refs = 0;

    for (size_t i = 0; i < n; i++) {
        void *obj_ptr = snaps[i].ptr;
        对象类型 t = 对象类型_取(obj_ptr);
        switch (t) {
            case CHAN_TYPE_K线: {
                K线 *obj = (K线 *) obj_ptr;
                池内_指针(macd);
                池内_指针(rsi);
                池内_指针(kdj);
                break;
            }
            case CHAN_TYPE_缠论K线: {
                /* 缠论K线.标的K线 弱引用不计入——K线不参与弱引用系统 */
                break;
            }
            case CHAN_TYPE_分型: {
                分型 *obj = (分型 *) obj_ptr;
                池内_指针(左);
                池内_指针(中);
                池内_指针(右);
                break;
            }
            case CHAN_TYPE_虚线: {
                虚线 *obj = (虚线 *) obj_ptr;
                池内_数组(基础序列);
                池内_数组(特征序列);
                池内_数组(实_中枢序列);
                池内_数组(虚_中枢序列);
                池内_数组(合_中枢序列);
                池内_指针(文);
                池内_指针(武);
                池内_指针(确认K线);
                池内_指针(前一缺口);
                池内_指针(前一结束位置);
                break;
            }
            case CHAN_TYPE_中枢: {
                中枢 *obj = (中枢 *) obj_ptr;
                池内_数组(基础序列);
                池内_指针(第三买卖线);
                池内_指针(本级_第三买卖线);
                break;
            }
            case CHAN_TYPE_线段特征: {
                线段特征 *obj = (线段特征 *) obj_ptr;
                池内_数组(基础序列);
                break;
            }
            case CHAN_TYPE_特征分型: {
                特征分型 *obj = (特征分型 *) obj_ptr;
                池内_指针(左);
                池内_指针(中);
                池内_指针(右);
                break;
            }
            case CHAN_TYPE_K线合成器: {
                K线合成器 *syn = (K线合成器 *) obj_ptr;
                for (int _i = 0; _i < syn->周期数量; _i++) {
                    if (syn->当前K线[_i]) {
                        ((对象头结构 *) syn->当前K线[_i])->弱引用计数++;
                    }
                    for (size_t _j = 0; _j < syn->合成K线列表[_i].长度; _j++) {
                        void *_e = syn->合成K线列表[_i].数据[_j];
                        if (_e) {
                            ((对象头结构 *) _e)->弱引用计数++;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    /* 统计池内引用数 = 总重建数 - 观察者贡献数 */
    for (size_t i = 0; i < n; i++) {
        int 总重建 = ((对象头结构 *) snaps[i].ptr)->弱引用计数;
        pool_refs += 总重建 - snaps[i].观察者贡献;
    }

#undef 池内_指针
#undef 池内_数组

    /* ---- 4. 三方对比 ---- */
    size_t 差异数 = 0;
    fprintf(stderr, "\n=== 验证弱引用计数: 池对象=%zu ===\n", n);
    fprintf(stderr, "  观察者序列引用: %zu 次\n", obs_refs);
    fprintf(stderr, "  池内交叉引用:   %zu 次\n", pool_refs);
    fprintf(stderr, "  引用总计:       %zu 次\n\n", obs_refs + pool_refs);

    for (size_t i = 0; i < n; i++) {
        void *obj_ptr = snaps[i].ptr;
        int 存储 = snaps[i].存储值;
        int 观察者 = snaps[i].观察者贡献;
        int 重建 = ((对象头结构 *) obj_ptr)->弱引用计数;
        int 差值 = 存储 - 重建;

        if (差值 != 0) {
            fprintf(stderr,
                    "差异: type=%-2d(%-12s) ptr=%p 存储=%-4d 观察者=%-4d 池内=%-4d 重建=%-4d 差值=%+-4d\n",
                    (int) 对象类型_取(obj_ptr), 类型名称[(int) 对象类型_取(obj_ptr)],
                    obj_ptr, 存储, 观察者, 重建 - 观察者, 重建, 差值);
            差异数++;
        }
    }

    if (差异数 == 0) {
        fprintf(stderr, "  结果: 全部 %zu 对象弱引用计数一致 ✓\n", n);
    } else {
        fprintf(stderr, "\n  差异合计: %zu 对象 / %zu 一致\n", 差异数, n - 差异数);
    }

    /* ---- 5. 恢复存储值 ---- */
    for (size_t i = 0; i < n; i++) {
        ((对象头结构 *) snaps[i].ptr)->弱引用计数 = snaps[i].存储值;
    }

    free(snaps);
    free(idxs);
}

#undef 弱引用_快照贡献
#undef 快照_数组贡献


/* ================================================================
 * 多线程并发测试（编译时定义 CHAN_MULTITHREAD_TEST 启用）
 * ================================================================ */

#ifdef CHAN_MULTITHREAD_TEST

#include <pthread.h>

#define MT_THREADS 4
#define MT_EXPECTED_S 2796
#define MT_EXPECTED_G 374
#define MT_EXPECTED_Z 48

typedef struct {
    int id;
    const char *file;
    size_t strokes, segments, hubs;
    int errors;
} MTResult;

/* 测试 1: 并行完整分析 —— 每个线程独立跑全量数据 */
static void *mt_并行分析(void *arg) {
    MTResult *r = (MTResult *) arg;
    缠论配置 *cfg = 缠论配置_不推送();
    观察者 *obs = 观察者_读取数据文件(r->file, cfg);
    r->strokes = obs->笔序列.长度;
    r->segments = obs->线段序列.长度;
    r->hubs = obs->中枢序列.长度;
    if (r->strokes != MT_EXPECTED_S) {
        r->errors++;
    }
    if (r->segments != MT_EXPECTED_G) {
        r->errors++;
    }
    if (r->hubs != MT_EXPECTED_Z) {
        r->errors++;
    }
    解引用(obs);
    return NULL;
}

/* 测试 2: 并发池分配压力 —— 每线程分配 10000 根 K 线 */
static void *mt_创建K线(void *arg) {
    MTResult *r = (MTResult *) arg;
    for (int i = 0; i < 10000; i++) {
        char id[32];
        snprintf(id, sizeof(id), "MT%d-K%d", r->id, i);
        K线 *k = K线_新建(id, i, 300, 1761327300 + i * 60,
                              50000.0 + (double) (i % 100),
                              50100.0 + (double) (i % 100),
                              49900.0 + (double) (i % 100),
                              50050.0 + (double) (i % 100),
                              100.0);
        if (!k) {
            r->errors++;
            return NULL;
        }
    }
    return NULL;
}

void 运行多线程测试(const char *文件路径) {
    fprintf(stderr, "\n========== 多线程并发测试 ==========\n");
    fprintf(stderr, "线程数: %d  数据: %s\n\n", MT_THREADS, 文件路径);

    int failed = 0;

    /* ---- 测试 1: 并行完整分析 ---- */
    fprintf(stderr, "--- 测试 1: 并行完整分析 ---\n");
    {
        pthread_t threads[MT_THREADS];
        MTResult results[MT_THREADS];

        for (int i = 0; i < MT_THREADS; i++) {
            results[i].id = i;
            results[i].file = 文件路径;
            results[i].errors = 0;
            pthread_create(&threads[i], NULL, mt_并行分析, &results[i]);
        }
        for (int i = 0; i < MT_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        for (int i = 0; i < MT_THREADS; i++) {
            fprintf(stderr, "  线程 %d: 笔=%-4zu 线段=%-3zu 中枢=%-2zu %s\n",
                    i, results[i].strokes, results[i].segments, results[i].hubs,
                    results[i].errors ? "✗" : "✓");
            if (results[i].errors) {
                failed++;
            }
        }

        int consistent = 1;
        for (int i = 1; i < MT_THREADS; i++) {
            if (results[i].strokes != results[0].strokes ||
                    results[i].segments != results[0].segments ||
                    results[i].hubs != results[0].hubs) {
                consistent = 0;
                break;
            }
        }
        fprintf(stderr, "  一致性: %s\n\n", consistent ? "✓ 全部一致" : "✗ 不一致");
        if (!consistent) {
            failed++;
        }
    }

    释放全局内存池();

    /* ---- 测试 2: 并发池分配压力 ---- */
    fprintf(stderr, "--- 测试 2: 并发池分配压力 (每线程 10000 K线) ---\n");
    {
        pthread_t threads[MT_THREADS];
        MTResult results[MT_THREADS];

        for (int i = 0; i < MT_THREADS; i++) {
            results[i].id = i;
            results[i].errors = 0;
            pthread_create(&threads[i], NULL, mt_创建K线, &results[i]);
        }
        for (int i = 0; i < MT_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        for (int i = 0; i < MT_THREADS; i++) {
            fprintf(stderr, "  线程 %d: %s\n", i,
                    results[i].errors ? "✗ 分配失败" : "✓");
            if (results[i].errors) {
                failed++;
            }
        }
        fprintf(stderr, "\n");
    }

    释放全局内存池();

    /* ---- 测试 3: 并发压力后单线程验证 ---- */
    fprintf(stderr, "--- 测试 3: 并发压力后单线程验证 ---\n");
    {
        缠论配置 *cfg = 缠论配置_不推送();
        观察者 *obs = 观察者_读取数据文件(文件路径, cfg);

        int ok = (obs->笔序列.长度 == MT_EXPECTED_S &&
                  obs->线段序列.长度 == MT_EXPECTED_G &&
                  obs->中枢序列.长度 == MT_EXPECTED_Z);

        fprintf(stderr, "  结果: 笔=%zu 线段=%zu 中枢=%zu %s\n",
                obs->笔序列.长度, obs->线段序列.长度, obs->中枢序列.长度,
                ok ? "✓" : "✗");
        if (!ok) {
            failed++;
        }

        验证弱引用计数(obs);
        解引用(obs);
    }

    释放全局内存池();
    打印内存摘要();

    fprintf(stderr, "\n========== %d/3 测试通过 ==========\n", 3 - failed);
}

#endif /* CHAN_MULTITHREAD_TEST */

/* ================================================================
 *  测试_周期合成
 * ================================================================ */

static void 测试_周期合成(const char *文件路径) {
    /* 解析文件名: 符号-周期-起始-结束.nb */
    const char *文件名 = strrchr(文件路径, '/');
    文件名 = 文件名 ? 文件名 + 1 : 文件路径;

    char 名称缓冲[256];
    strncpy(名称缓冲, 文件名, sizeof(名称缓冲) - 1);
    名称缓冲[sizeof(名称缓冲) - 1] = '\0';

    /* 去掉 .nb 后缀 */
    char *点 = strrchr(名称缓冲, '.');
    if (点) {
        *点 = '\0';
    }

    /* 分割: 符号-周期-起始-结束 */
    char *符号 = 名称缓冲;
    char *周期串 = strchr(名称缓冲, '-');
    if (!周期串) {
        fprintf(stderr, "文件名格式错误\n");
        return;
    }
    *周期串++ = '\0';

    char *起始串 = strchr(周期串, '-');
    if (!起始串) {
        fprintf(stderr, "文件名格式错误\n");
        return;
    }
    *起始串++ = '\0';

    char *结束串 = strchr(起始串, '-');
    if (!结束串) {
        fprintf(stderr, "文件名格式错误\n");
        return;
    }
    *结束串++ = '\0';

    int 周期 = atoi(周期串);

    int 周期组[] = { 周期, 周期 * 5, 周期 * 5 * 6 };
    int 周期数量 = 3;

    printf("测试_周期合成: 符号=%s, 周期组=[%d, %d, %d]\n",
           符号, 周期组[0], 周期组[1], 周期组[2]);

    缠论配置 *配置 = 缠论配置_不推送();
    配置->加载文件路径[0] = '\0';
    strncpy(配置->加载文件路径, 文件路径, 255);

    立体分析器 *多级别分析 = 立体分析器_新建(符号, 周期组, 周期数量, 配置, NULL);

    /* 读取 .nb 文件并逐根投喂 */
    FILE *f = fopen(文件路径, "rb");
    if (!f) {
        fprintf(stderr, "无法打开文件: %s\n", 文件路径);
        释放立体分析器(多级别分析);
        释放缠论配置(配置);
        return;
    }

    time_t 开始时间 = time(NULL);
    double buf[6];
    size_t 总K线数 = 0;

    while (fread(buf, sizeof(double), 6, f) == 6) {
        /* .nb 文件是大端序 double */
        for (int i = 0; i < 6; i++) {
            uint64_t raw;
            memcpy(&raw, &buf[i], sizeof(raw));
            raw = __builtin_bswap64(raw);
            memcpy(&buf[i], &raw, sizeof(raw));
        }

        K线 *普K = K线_创建普K(符号, (time_t) buf[0],
                                     buf[1], buf[2], buf[3], buf[4], buf[5],
                                     (int) 总K线数, 周期);
        立体分析器_投喂K线(多级别分析, 普K);
        总K线数++;
    }

    fclose(f);
    time_t 消耗用时 = time(NULL) - 开始时间;

    printf("测试_周期合成: 耗时=%lds, 总K线=%zu\n", (long) 消耗用时, 总K线数);

    立体分析器_测试_保存数据(多级别分析);
    释放立体分析器(多级别分析);
    释放缠论配置(配置);
}

/* ================================================================
 *  测试_读取数据
 * ================================================================ */

static 观察者 *测试_读取数据(缠论配置 *配置) {
    time_t 启动时间 = time(NULL);
    观察者 *观察员 = 观察者_读取数据文件(配置->加载文件路径, 配置);
    time_t 消耗用时 = time(NULL) - 启动时间;

    if (观察员) {
        printf("测试_读取数据 耗时=%lds 普K数量=%zu\n",
               (long) 消耗用时, 观察员->普通K线序列.长度);
    }
    return 观察员;
}

/* ================================================================
 * main
 * ================================================================ */

int main(int argc, char **argv) {
    const char *默认路径 = "./btcusd-300-1761327300-1776327900.nb";
    const char *文件路径 = 默认路径;

#ifdef CHAN_MULTITHREAD_TEST
    if (argc > 1 && strcmp(argv[1], "--mt-test") == 0) {
        运行多线程测试(argc > 2 ? argv[2] : 默认路径);
        return 0;
    }
#endif

    if (argc > 1 && strcmp(argv[1], "--synth") == 0) {
        测试_周期合成(argc > 2 ? argv[2] : 默认路径);
        释放全局内存池();
        打印内存摘要();
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--read") == 0) {
        文件路径 = argc > 2 ? argv[2] : 默认路径;
        缠论配置 *配置 = 缠论配置_不推送();
        strncpy(配置->加载文件路径, 文件路径, 255);
        观察者 *obs = 测试_读取数据(配置);
        if (obs) {
            观察者_测试_保存数据(obs, NULL);
            验证弱引用计数(obs);
            解引用(obs);
        }
        释放缠论配置(配置);
        释放全局内存池();
        打印内存摘要();
        return 0;
    }

    return 0;
}
