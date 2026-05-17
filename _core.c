/*
 * MIT License — Copyright (c) 2024 YuYuKunKun
 *
 * _core.c — Python C API native extension for the chan2c99 缠论 library.
 *
 * Wraps every C struct as a proper Python type object with direct
 * field access (PyGetSetDef), methods (PyMethodDef), and sequence
 * protocol support.  Zero ctypes overhead.
 *
 * Compile with -DPYTHON_NATIVE (not -DPYTHON_WRAPPER) to avoid
 * pulling in the old ctypes wrapper.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "chan.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  OOM / fatal error handler
 * ================================================================ */

static void Py_OOM处理器(size_t sz) {
    PyErr_Format(PyExc_MemoryError,
                 "chan_c99: allocation of %zu bytes failed", sz);
}

__attribute__((constructor))
static void Py_初始化原生处理器(void) {
    chan_oom_handler = Py_OOM处理器;
}

/* ================================================================
 *  Forward declarations of all type objects
 * ================================================================ */

static PyTypeObject ChanObject_Type; /* base */
static PyTypeObject KLine_Type;
static PyTypeObject ChanKLine_Type;
static PyTypeObject Gap_Type;
static PyTypeObject Fractal_Type;
static PyTypeObject DashLine_Type;
static PyTypeObject Stroke_Type;
static PyTypeObject Segment_Type;
static PyTypeObject SegFeature_Type;
static PyTypeObject Hub_Type;
static PyTypeObject ChanConfig_Type;
static PyTypeObject Observer_Type;

/* ================================================================
 *  ChanObject — base type for all C object wrappers
 * ================================================================ */

typedef struct {
    PyObject_HEAD
    void *ptr; /* C object pointer */
    int owns; /* 1 = owned (call 解引用 in tp_dealloc) */
} ChanObject;

/* Forward declarations for methods that use 动态数组 (defined later) */
static PyObject *Py_笔_创建笔(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_线段_创建线段(PyObject *py_类, PyObject *py_参数);

static PyObject *Py_线段特征_静态分析(PyObject *py_类, PyObject *args);

static PyObject *Py_中枢_获取序列(ChanObject *self, PyObject *py_忽略);

static PyObject *Py_笔_分析(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_线段_分析(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_中枢_分析(PyObject *py_类, PyObject *args, PyObject *kw);

/* Forward declarations for 笔 classmethods (matching chan.py 笔) */
static PyObject *Py_笔_获取缠K数量(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_笔_次高(PyObject *py_类, PyObject *args);

static PyObject *Py_笔_次低(PyObject *py_类, PyObject *args);

static PyObject *Py_笔_实际高点(PyObject *py_类, PyObject *args);

static PyObject *Py_笔_实际低点(PyObject *py_类, PyObject *args);

static PyObject *Py_笔_以文会友(PyObject *py_类, PyObject *args);

static PyObject *Py_笔_以武会友(PyObject *py_类, PyObject *args);

static PyObject *Py_笔_根据缠K找笔(PyObject *py_类, PyObject *args, PyObject *kw);

/* Forward declarations for 背驰分析 classmethods (matching chan.py 背驰分析) */
static PyObject *Py_背驰分析_斜率背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_测度背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_MACD背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_全量背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_任意背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_配置背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_任选背驰(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_背驰分析_背驰模式(PyObject *py_类, PyObject *args, PyObject *kw);

static void Py_Chan对象_释放(ChanObject *self) {
    /* 弱引用模型：pool 对象由 释放全局内存池 统一清理，不单独 解引用 */
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *Py_Chan对象_引用(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    if (self->ptr) 引用(self->ptr);
    self->owns = 1;
    Py_RETURN_NONE;
}

static PyObject *Py_Chan对象_解引用(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    /* 弱引用模型：pool 对象不单独解引用，仅清除 Python 层引用 */
    self->ptr = NULL;
    self->owns = 0;
    Py_RETURN_NONE;
}

static Py_hash_t Py_Chan对象_哈希(ChanObject *self) {
    return _Py_HashPointer(self->ptr);
}

static PyObject *Py_Chan对象_比较(ChanObject *self, PyObject *其他对象, int op) {
    if (op != Py_EQ && op != Py_NE) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    if (!PyObject_TypeCheck(其他对象, &ChanObject_Type)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    bool eq = (self->ptr == ((ChanObject *) 其他对象)->ptr);
    if (op == Py_EQ) return PyBool_FromLong(eq);
    return PyBool_FromLong(!eq);
}

static PyObject *Py_Chan对象_获取_ptr(ChanObject *self, void *closure) {
    return PyLong_FromVoidPtr(self->ptr);
}

static PyObject *Py_Chan对象_获取_owns(ChanObject *self, void *closure) {
    return PyBool_FromLong(self->owns);
}

static PyGetSetDef ChanObject_getset[] = {
    {"ptr", (getter) Py_Chan对象_获取_ptr, NULL, "Raw C pointer value", NULL},
    {"owns", (getter) Py_Chan对象_获取_owns, NULL, "Whether this wrapper owns a C reference", NULL},
    {"_ptr", (getter) Py_Chan对象_获取_ptr, NULL, NULL, NULL},
    {"_owns", (getter) Py_Chan对象_获取_owns, NULL, NULL, NULL},
    {NULL}
};

static PyMethodDef ChanObject_methods[] = {
    {
        "retain", (PyCFunction) Py_Chan对象_引用, METH_NOARGS,
        "Increment the reference count (acquire ownership)."
    },
    {
        "release", (PyCFunction) Py_Chan对象_解引用, METH_NOARGS,
        "Decrement the reference count. C object freed when reaching 0."
    },
    {NULL}
};

static PyTypeObject ChanObject_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core._ChanObject",
    .tp_basicsize = sizeof(ChanObject),
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getset = ChanObject_getset,
    .tp_methods = ChanObject_methods,
    .tp_hash = (hashfunc) Py_Chan对象_哈希,
    .tp_richcompare = (richcmpfunc) Py_Chan对象_比较,
    .tp_doc = "C 对象指针的基础包装，含引用计数。",
};

/* ---- helpers to create owned / borrowed wrappers ---- */

static PyObject *Py_制作_拥有(PyTypeObject *type, void *ptr) {
    if (!ptr) Py_RETURN_NONE;
    ChanObject *py_对象 = (ChanObject *) type->tp_alloc(type, 0);
    if (!py_对象) return NULL;
    py_对象->ptr = ptr;
    py_对象->owns = 1;
    return (PyObject *) py_对象;
}

static PyObject *Py_制作_借用(PyTypeObject *type, void *ptr) {
    if (!ptr) Py_RETURN_NONE;
    ChanObject *py_对象 = (ChanObject *) type->tp_alloc(type, 0);
    if (!py_对象) return NULL;
    py_对象->ptr = ptr;
    py_对象->owns = 0;
    return (PyObject *) py_对象;
}

static void *Py_解包(PyObject *py_对象, PyTypeObject *py_期望类型) {
    if (!PyObject_TypeCheck(py_对象, py_期望类型)) {
        PyErr_Format(PyExc_TypeError, "expected %s", py_期望类型->tp_name);
        return NULL;
    }
    return ((ChanObject *) py_对象)->ptr;
}

/* ================================================================
 *  Enum → name helpers (shared by getters/setters below)
 * ================================================================ */

static const char *相对方向_到名称(相对方向 d) {
    switch (d) {
        case 相对方向_向上: return "向上";
        case 相对方向_向下: return "向下";
        case 相对方向_向上缺口: return "向上缺口";
        case 相对方向_向下缺口: return "向下缺口";
        case 相对方向_衔接向上: return "衔接向上";
        case 相对方向_衔接向下: return "衔接向下";
        case 相对方向_顺: return "顺";
        case 相对方向_逆: return "逆";
        case 相对方向_同: return "同";
        default: return "未知";
    }
}

static const char *分型结构_到名称(分型结构 s) {
    switch (s) {
        case 分型结构_顶: return "顶";
        case 分型结构_底: return "底";
        case 分型结构_上: return "上";
        case 分型结构_下: return "下";
        case 分型结构_散: return "散";
        default: return "未知";
    }
}

static int 相对方向_从名称(const char *s, 相对方向 *out) {
    if (strcmp(s, "向上") == 0) {
        *out = 相对方向_向上;
        return 1;
    }
    if (strcmp(s, "向下") == 0) {
        *out = 相对方向_向下;
        return 1;
    }
    if (strcmp(s, "向上缺口") == 0) {
        *out = 相对方向_向上缺口;
        return 1;
    }
    if (strcmp(s, "向下缺口") == 0) {
        *out = 相对方向_向下缺口;
        return 1;
    }
    if (strcmp(s, "衔接向上") == 0) {
        *out = 相对方向_衔接向上;
        return 1;
    }
    if (strcmp(s, "衔接向下") == 0) {
        *out = 相对方向_衔接向下;
        return 1;
    }
    if (strcmp(s, "顺") == 0) {
        *out = 相对方向_顺;
        return 1;
    }
    if (strcmp(s, "逆") == 0) {
        *out = 相对方向_逆;
        return 1;
    }
    if (strcmp(s, "同") == 0) {
        *out = 相对方向_同;
        return 1;
    }
    return 0;
}

static int 分型结构_从名称(const char *s, 分型结构 *out) {
    if (strcmp(s, "顶") == 0) {
        *out = 分型结构_顶;
        return 1;
    }
    if (strcmp(s, "底") == 0) {
        *out = 分型结构_底;
        return 1;
    }
    if (strcmp(s, "上") == 0) {
        *out = 分型结构_上;
        return 1;
    }
    if (strcmp(s, "下") == 0) {
        *out = 分型结构_下;
        return 1;
    }
    if (strcmp(s, "散") == 0) {
        *out = 分型结构_散;
        return 1;
    }
    return 0;
}

/* Helper: parse setter value — string name or int enum */
static int 解析方向(PyObject *v, 相对方向 *out) {
    if (PyLong_Check(v)) {
        long x = PyLong_AsLong(v);
        if (x == -1 && PyErr_Occurred()) return 0;
        *out = (相对方向) x;
        return 1;
    }
    if (PyUnicode_Check(v)) {
        const char *s = PyUnicode_AsUTF8(v);
        if (!s) return 0;
        return 相对方向_从名称(s, out);
    }
    PyErr_SetString(PyExc_TypeError, "方向 must be int or str");
    return 0;
}

static int 解析分型结构(PyObject *v, 分型结构 *out) {
    if (PyLong_Check(v)) {
        long x = PyLong_AsLong(v);
        if (x == -1 && PyErr_Occurred()) return 0;
        *out = (分型结构) x;
        return 1;
    }
    if (PyUnicode_Check(v)) {
        const char *s = PyUnicode_AsUTF8(v);
        if (!s) return 0;
        return 分型结构_从名称(s, out);
    }
    PyErr_SetString(PyExc_TypeError, "分型结构 must be int or str");
    return 0;
}

/* ================================================================
 *  Custom __repr__ / __str__ override mechanism
 *
 *  Python can call _设置自定义strrepr(type, "__repr__", func) to
 *  inject a Python callback into a C type's tp_dict.  The C repr/str
 *  functions check tp_dict first before falling back to the default.
 *  This bypasses the "immutable type" restriction because C writes
 *  tp_dict directly.
 * ================================================================ */

/* 内部键名不能与 CPython 的 slot wrapper 描述符冲突（tp_dict["__repr__"] 默认就是
 * 指向 tp_repr 的 slot wrapper，直接取出调用会造成无限递归）。用带前缀的私有键名。
 */
#define CUSTOM_REPR_KEY "_custom_repr_cb"
#define CUSTOM_STR_KEY  "_custom_str_cb"

static PyObject *_try_custom_repr(PyObject *self) {
    PyObject *custom = PyDict_GetItemString(Py_TYPE(self)->tp_dict, CUSTOM_REPR_KEY);
    if (custom) return PyObject_CallOneArg(custom, self);
    return NULL;
}

static PyObject *_try_custom_str(PyObject *self) {
    PyObject *custom = PyDict_GetItemString(Py_TYPE(self)->tp_dict, CUSTOM_STR_KEY);
    if (custom) return PyObject_CallOneArg(custom, self);
    return NULL;
}

/* Shared tp_str for all types — checks __str__ callback then falls back to tp_repr */
static PyObject *Py_通用_str(PyObject *self) {
    PyObject *custom = _try_custom_str(self);
    if (custom) return custom;
    reprfunc repr = Py_TYPE(self)->tp_repr;
    if (repr) return repr(self);
    return PyUnicode_FromFormat("<%s at %p>", Py_TYPE(self)->tp_name, self);
}

static PyObject *Py__设置自定义strrepr(PyObject *m, PyObject *args) {
    PyTypeObject *type;
    const char *name;
    PyObject *func;
    if (!PyArg_ParseTuple(args, "OsO:设置自定义strrepr", &type, &name, &func))
        return NULL;
    if (strcmp(name, "__repr__") != 0 && strcmp(name, "__str__") != 0) {
        PyErr_SetString(PyExc_ValueError, "name must be '__repr__' or '__str__'");
        return NULL;
    }
    /* Translate API names to internal keys that won't clash with CPython's
     * slot wrapper descriptors in tp_dict. */
    const char *key = (strcmp(name, "__repr__") == 0) ? CUSTOM_REPR_KEY : CUSTOM_STR_KEY;
    if (func == Py_None) {
        int r = PyDict_DelItemString(type->tp_dict, key);
        if (r < 0) { PyErr_Clear(); } /* 键不存在，忽略 */
    } else if (PyCallable_Check(func)) {
        PyDict_SetItemString(type->tp_dict, key, func);
    } else {
        PyErr_SetString(PyExc_TypeError, "callback must be callable or None");
        return NULL;
    }
    Py_RETURN_NONE;
}

/* ================================================================
 *  K线 (Raw K-line) type
 * ================================================================ */

typedef struct {
    ChanObject base;
} KLineObject;

/* -- properties -- */

static PyObject *Py_K线_获取_高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((K线 *) self->ptr)->高);
}

static PyObject *Py_K线_获取_低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((K线 *) self->ptr)->低);
}

static PyObject *Py_K线_获取_开盘价(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((K线 *) self->ptr)->开盘价);
}

static PyObject *Py_K线_获取_收盘价(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((K线 *) self->ptr)->收盘价);
}

static PyObject *Py_K线_获取_成交量(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((K线 *) self->ptr)->成交量);
}

static PyObject *Py_K线_获取_序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((K线 *) self->ptr)->序号);
}

static PyObject *Py_K线_获取_周期(ChanObject *self, void *c) {
    return PyLong_FromLong(((K线 *) self->ptr)->周期);
}

static PyObject *Py_K线_获取_方向(ChanObject *self, void *c) {
    return PyUnicode_FromString(相对方向_到名称(K线_方向((K线 *) self->ptr)));
}

static PyObject *Py_K线_获取_时间戳(ChanObject *self, void *c) {
    return PyLong_FromLongLong((long long) ((K线 *) self->ptr)->时间戳);
}

static PyObject *Py_K线_获取_标识(ChanObject *self, void *c) {
    return PyUnicode_FromString(((K线 *) self->ptr)->标识);
}

/* --- K线 setters --- */
static int Py_K线_设置_高(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->高 = v;
    return 0;
}

static int Py_K线_设置_低(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->低 = v;
    return 0;
}

static int Py_K线_设置_开盘价(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->开盘价 = v;
    return 0;
}

static int Py_K线_设置_收盘价(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->收盘价 = v;
    return 0;
}

static int Py_K线_设置_成交量(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->成交量 = v;
    return 0;
}

static int Py_K线_设置_序号(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_K线_设置_周期(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->周期 = (int) v;
    return 0;
}

static int Py_K线_设置_时间戳(ChanObject *self, PyObject *value, void *c) {
    long long v = PyLong_AsLongLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((K线 *) self->ptr)->时间戳 = (time_t) v;
    return 0;
}

static int Py_K线_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) return -1;
    strncpy(((K线 *) self->ptr)->标识, s, 63);
    ((K线 *) self->ptr)->标识[63] = '\0';
    return 0;
}

static PyGetSetDef KLine_getset[] = {
    {"标识", (getter) Py_K线_获取_标识, (setter) Py_K线_设置_标识, "Symbol identifier", NULL},
    {"序号", (getter) Py_K线_获取_序号, (setter) Py_K线_设置_序号, "Index in sequence", NULL},
    {"周期", (getter) Py_K线_获取_周期, (setter) Py_K线_设置_周期, "Period in seconds", NULL},
    {"时间戳", (getter) Py_K线_获取_时间戳, (setter) Py_K线_设置_时间戳, "Unix timestamp", NULL},
    {"高", (getter) Py_K线_获取_高, (setter) Py_K线_设置_高, "High price", NULL},
    {"低", (getter) Py_K线_获取_低, (setter) Py_K线_设置_低, "Low price", NULL},
    {"开盘价", (getter) Py_K线_获取_开盘价, (setter) Py_K线_设置_开盘价, "Open price", NULL},
    {"收盘价", (getter) Py_K线_获取_收盘价, (setter) Py_K线_设置_收盘价, "Close price", NULL},
    {"成交量", (getter) Py_K线_获取_成交量, (setter) Py_K线_设置_成交量, "Volume", NULL},
    {"方向", (getter) Py_K线_获取_方向, NULL, "Direction (相对方向 enum, read-only)", NULL},
    {NULL}
};

/* -- 创建普K -- */

static PyObject *Py_K线_创建普K(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {
        "标识", "时间戳", "开盘价", "高", "低", "收盘价", "成交量", "序号", "周期", NULL
    };
    const char *id;
    long long ts;
    double py_开盘, py_最高, py_最低, py_收盘, py_量;
    int index, period;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "sLdddddi|i", kwnames,
                                     &id, &ts, &py_开盘, &py_最高, &py_最低, &py_收盘, &py_量, &index, &period))
        return NULL;

    void *ptr = K线_创建普K(id, (time_t) ts, py_开盘, py_最高, py_最低, py_收盘, py_量, index, period);
    if (!ptr) return PyErr_NoMemory();
    return Py_制作_拥有(&KLine_Type, ptr);
}

static PyMethodDef KLine_methods[] = {
    {
        "创建普K", (PyCFunction) Py_K线_创建普K,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "Create a new plain K-line (OHLCV bar)."
    },
    {NULL}
};

static PyObject *Py_K线_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    K线 *k = (K线 *) self->ptr;
    char 缓冲[384];
    snprintf(缓冲, sizeof(缓冲),
             "%s<%d, %d, %s, %lld, %.2f, %.2f, %.2f, %.2f>",
             k->标识, k->序号, k->周期,
             相对方向_到名称(K线_方向(k)),
             (long long) k->时间戳,
             k->开盘价, k->高, k->低, k->收盘价);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject KLine_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.K线",
    .tp_basicsize = sizeof(KLineObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = KLine_getset,
    .tp_methods = KLine_methods,
    .tp_repr = (reprfunc) Py_K线_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "原始K线 — 含 OHLCV 数据和元信息。",
};

/* ================================================================
 *  缠论K线 (Chan K-line) type — merged K-line with direction
 * ================================================================ */

typedef struct {
    ChanObject base;
} ChanKLineObject;

static PyObject *Py_缠论K线_获取_序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((缠论K线 *) self->ptr)->序号);
}

static PyObject *Py_缠论K线_获取_时间戳(ChanObject *self, void *c) {
    return PyLong_FromLongLong((long long) ((缠论K线 *) self->ptr)->时间戳);
}

static PyObject *Py_缠论K线_获取_高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((缠论K线 *) self->ptr)->高);
}

static PyObject *Py_缠论K线_获取_低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((缠论K线 *) self->ptr)->低);
}

static PyObject *Py_缠论K线_获取_方向(ChanObject *self, void *c) {
    return PyUnicode_FromString(相对方向_到名称(((缠论K线 *) self->ptr)->方向));
}

static PyObject *Py_缠论K线_获取_分型(ChanObject *self, void *c) {
    return PyUnicode_FromString(分型结构_到名称(((缠论K线 *) self->ptr)->分型));
}

static PyObject *Py_缠论K线_获取_分型特征值(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((缠论K线 *) self->ptr)->分型特征值);
}

static PyObject *Py_缠论K线_获取_周期(ChanObject *self, void *c) {
    return PyLong_FromLong(((缠论K线 *) self->ptr)->周期);
}

static PyObject *Py_缠论K线_获取_标识(ChanObject *self, void *c) {
    return PyUnicode_FromString(((缠论K线 *) self->ptr)->标识);
}

static PyObject *Py_缠论K线_获取_原始起始序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((缠论K线 *) self->ptr)->原始起始序号);
}

static PyObject *Py_缠论K线_获取_原始结束序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((缠论K线 *) self->ptr)->原始结束序号);
}

static PyObject *Py_缠论K线_获取_标的K线(ChanObject *self, void *c) {
    K线 *ref = ((缠论K线 *) self->ptr)->标的K线;
    if (!ref) Py_RETURN_NONE;
    return Py_制作_借用(&KLine_Type, ref);
}

/* --- 缠论K线 setters --- */
static int Py_缠论K线_设置_序号(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->序号 = (int) x;
    return 0;
}

static int Py_缠论K线_设置_时间戳(ChanObject *self, PyObject *v, void *c) {
    long long x = PyLong_AsLongLong(v);
    if (x == -1 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->时间戳 = (time_t) x;
    return 0;
}

static int Py_缠论K线_设置_高(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->高 = x;
    return 0;
}

static int Py_缠论K线_设置_低(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->低 = x;
    return 0;
}

static int Py_缠论K线_设置_方向(ChanObject *self, PyObject *v, void *c) {
    相对方向 x;
    if (!解析方向(v, &x)) return -1;
    ((缠论K线 *) self->ptr)->方向 = x;
    return 0;
}

static int Py_缠论K线_设置_分型(ChanObject *self, PyObject *v, void *c) {
    分型结构 x;
    if (!解析分型结构(v, &x)) return -1;
    ((缠论K线 *) self->ptr)->分型 = x;
    return 0;
}

static int Py_缠论K线_设置_分型特征值(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->分型特征值 = x;
    return 0;
}

static int Py_缠论K线_设置_周期(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->周期 = (int) x;
    return 0;
}

static int Py_缠论K线_设置_标识(ChanObject *self, PyObject *v, void *c) {
    const char *s = PyUnicode_AsUTF8(v);
    if (!s)return -1;
    strncpy(((缠论K线 *) self->ptr)->标识, s, 63);
    ((缠论K线 *) self->ptr)->标识[63] = '\0';
    return 0;
}

static int Py_缠论K线_设置_原始起始序号(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->原始起始序号 = (int) x;
    return 0;
}

static int Py_缠论K线_设置_原始结束序号(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred())return -1;
    ((缠论K线 *) self->ptr)->原始结束序号 = (int) x;
    return 0;
}

static PyGetSetDef ChanKLine_getset[] = {
    {"序号", (getter) Py_缠论K线_获取_序号, (setter) Py_缠论K线_设置_序号, NULL, NULL},
    {"时间戳", (getter) Py_缠论K线_获取_时间戳, (setter) Py_缠论K线_设置_时间戳, NULL, NULL},
    {"高", (getter) Py_缠论K线_获取_高, (setter) Py_缠论K线_设置_高, NULL, NULL},
    {"低", (getter) Py_缠论K线_获取_低, (setter) Py_缠论K线_设置_低, NULL, NULL},
    {"方向", (getter) Py_缠论K线_获取_方向, (setter) Py_缠论K线_设置_方向, NULL, NULL},
    {"分型", (getter) Py_缠论K线_获取_分型, (setter) Py_缠论K线_设置_分型, NULL, NULL},
    {"分型特征值", (getter) Py_缠论K线_获取_分型特征值, (setter) Py_缠论K线_设置_分型特征值, NULL, NULL},
    {"周期", (getter) Py_缠论K线_获取_周期, (setter) Py_缠论K线_设置_周期, NULL, NULL},
    {"标识", (getter) Py_缠论K线_获取_标识, (setter) Py_缠论K线_设置_标识, NULL, NULL},
    {"原始起始序号", (getter) Py_缠论K线_获取_原始起始序号, (setter) Py_缠论K线_设置_原始起始序号, NULL, NULL},
    {"原始结束序号", (getter) Py_缠论K线_获取_原始结束序号, (setter) Py_缠论K线_设置_原始结束序号, NULL, NULL},
    {"标的K线", (getter) Py_缠论K线_获取_标的K线, NULL, "Reference K-line (read-only)", NULL},
    {NULL}
};

static PyObject *Py_缠论K线_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    缠论K线 *ck = (缠论K线 *) self->ptr;
    char 缓冲[384];
    snprintf(缓冲, sizeof(缓冲),
             "%s<%d, %s, %d, %s, %lld, %.2f, %.2f>",
             ck->标识, ck->序号,
             分型结构_到名称(ck->分型), ck->周期,
             相对方向_到名称(ck->方向),
             (long long) ck->时间戳, ck->高, ck->低);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject ChanKLine_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.缠论K线",
    .tp_basicsize = sizeof(ChanKLineObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = ChanKLine_getset,
    .tp_repr = (reprfunc) Py_缠论K线_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "缠论K线 — 由原始K线合并而来，含方向和分型信息。",
};

/* ================================================================
 *  缺口 (Gap) type
 * ================================================================ */

typedef struct {
    ChanObject base;
} GapObject;

static PyObject *Py_缺口_获取_高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((缺口 *) self->ptr)->高);
}

static PyObject *Py_缺口_获取_低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((缺口 *) self->ptr)->低);
}

static int Py_缺口_设置_高(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred())return -1;
    ((缺口 *) self->ptr)->高 = x;
    return 0;
}

static int Py_缺口_设置_低(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred())return -1;
    ((缺口 *) self->ptr)->低 = x;
    return 0;
}

static PyGetSetDef Gap_getset[] = {
    {"高", (getter) Py_缺口_获取_高, (setter) Py_缺口_设置_高, NULL, NULL},
    {"低", (getter) Py_缺口_获取_低, (setter) Py_缺口_设置_低, NULL, NULL},
    {NULL}
};

static PyObject *Py_缺口_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    缺口 *g = (缺口 *) self->ptr;
    char 缓冲[128];
    snprintf(缓冲, sizeof(缓冲), "缺口区间<%.2f <=> %.2f>", g->低, g->高);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject Gap_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.缺口",
    .tp_basicsize = sizeof(GapObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = Gap_getset,
    .tp_repr = (reprfunc) Py_缺口_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "缺口 — 两个价格区间之间的跳空。",
};

/* ================================================================
 *  分型 (Fractal) type
 * ================================================================ */

typedef struct {
    ChanObject base;
} FractalObject;

static PyObject *Py_分型_获取_左(ChanObject *self, void *c) {
    return Py_制作_借用(&ChanKLine_Type, ((分型 *) self->ptr)->左);
}

static PyObject *Py_分型_获取_中(ChanObject *self, void *c) {
    return Py_制作_借用(&ChanKLine_Type, ((分型 *) self->ptr)->中);
}

static PyObject *Py_分型_获取_右(ChanObject *self, void *c) {
    return Py_制作_借用(&ChanKLine_Type, ((分型 *) self->ptr)->右);
}

static PyObject *Py_分型_获取_结构(ChanObject *self, void *c) {
    return PyUnicode_FromString(分型结构_到名称(((分型 *) self->ptr)->结构));
}

static PyObject *Py_分型_获取_时间戳(ChanObject *self, void *c) {
    return PyLong_FromLongLong((long long) ((分型 *) self->ptr)->时间戳);
}

static PyObject *Py_分型_获取_分型特征值(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((分型 *) self->ptr)->分型特征值);
}

static int Py_分型_设置_结构(ChanObject *self, PyObject *v, void *c) {
    分型结构 x;
    if (!解析分型结构(v, &x)) return -1;
    ((分型 *) self->ptr)->结构 = x;
    return 0;
}

static int Py_分型_设置_时间戳(ChanObject *self, PyObject *v, void *c) {
    long long x = PyLong_AsLongLong(v);
    if (x == -1 && PyErr_Occurred())return -1;
    ((分型 *) self->ptr)->时间戳 = (time_t) x;
    return 0;
}

static int Py_分型_设置_分型特征值(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred())return -1;
    ((分型 *) self->ptr)->分型特征值 = x;
    return 0;
}

static PyGetSetDef Fractal_getset[] = {
    {"左", (getter) Py_分型_获取_左, NULL, "Left Chan K-line (read-only)", NULL},
    {"中", (getter) Py_分型_获取_中, NULL, "Mid Chan K-line (read-only)", NULL},
    {"右", (getter) Py_分型_获取_右, NULL, "Right Chan K-line (read-only)", NULL},
    {"结构", (getter) Py_分型_获取_结构, (setter) Py_分型_设置_结构, NULL, NULL},
    {"时间戳", (getter) Py_分型_获取_时间戳, (setter) Py_分型_设置_时间戳, NULL, NULL},
    {"分型特征值", (getter) Py_分型_获取_分型特征值, (setter) Py_分型_设置_分型特征值, NULL, NULL},
    {NULL}
};

static PyObject *Py_分型_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    分型 *f = (分型 *) self->ptr;
    char 缓冲[256];
    snprintf(缓冲, sizeof(缓冲),
             "%s<%lld, %.6f, 左缺失=%d, 右缺失=%d>",
             分型结构_到名称(f->中->分型),
             (long long) f->时间戳, f->分型特征值,
             f->左 == NULL, f->右 == NULL);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject Fractal_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.分型",
    .tp_basicsize = sizeof(FractalObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = Fractal_getset,
    .tp_repr = (reprfunc) Py_分型_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "分型 — 三根连续缠论K线形成的顶/底形态。",
};

/* ================================================================
 *  虚线 (Dash Line) type — base for 笔 and 线段
 * ================================================================ */

typedef struct {
    ChanObject base;
} DashLineObject;

/* Direction helper */
static PyObject *Py_虚线_获取_方向(ChanObject *self, void *c) {
    return PyUnicode_FromString(相对方向_到名称(虚线_方向((虚线 *) self->ptr)));
}

static PyObject *Py_虚线_获取_序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((虚线 *) self->ptr)->序号);
}

static PyObject *Py_虚线_获取_标识(ChanObject *self, void *c) {
    return PyUnicode_FromString(((虚线 *) self->ptr)->标识);
}

static PyObject *Py_虚线_获取_级别(ChanObject *self, void *c) {
    return PyLong_FromLong(((虚线 *) self->ptr)->级别);
}

static PyObject *Py_虚线_获取_文(ChanObject *self, void *c) {
    return Py_制作_借用(&Fractal_Type, ((虚线 *) self->ptr)->文);
}

static PyObject *Py_虚线_获取_武(ChanObject *self, void *c) {
    return Py_制作_借用(&Fractal_Type, ((虚线 *) self->ptr)->武);
}

static PyObject *Py_虚线_获取_高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((虚线 *) self->ptr)->高);
}

static PyObject *Py_虚线_获取_低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(((虚线 *) self->ptr)->低);
}

static PyObject *Py_虚线_获取_有效性(ChanObject *self, void *c) {
    return PyBool_FromLong(((虚线 *) self->ptr)->有效性);
}

static PyObject *Py_虚线_获取_模式(ChanObject *self, void *c) {
    return PyUnicode_FromString(((虚线 *) self->ptr)->模式);
}

static PyObject *Py_虚线_获取_确认K线(ChanObject *self, void *c) {
    缠论K线 *ck = ((虚线 *) self->ptr)->确认K线;
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_虚线_获取_前一缺口(ChanObject *self, void *c) {
    缺口 *g = ((虚线 *) self->ptr)->前一缺口;
    if (!g) Py_RETURN_NONE;
    return Py_制作_借用(&Gap_Type, g);
}

static PyObject *Py_虚线_获取_前一结束位置(ChanObject *self, void *c) {
    虚线 *d = ((虚线 *) self->ptr)->前一结束位置;
    if (!d) Py_RETURN_NONE;
    return Py_制作_借用(&DashLine_Type, d);
}

static PyObject *Py_虚线_获取_短路修正(ChanObject *self, void *c) {
    return PyBool_FromLong(((虚线 *) self->ptr)->短路修正);
}

/* Sequence getters for 虚线 internal arrays */

static PyObject *Py_虚线_获取_基础序列(ChanObject *self, void *c) {
    /* Return py_列表 for simplicity; Phase 3 will use proper sequence view */
    虚线 *d = (虚线 *) self->ptr;
    PyObject *py_列表 = PyList_New((Py_ssize_t) d->基础序列.长度);
    if (!py_列表) return NULL;
    for (size_t i = 0; i < d->基础序列.长度; i++) {
        void *元素 = 动态数组_获取(&d->基础序列, i);
        PyObject *py_包装 = Py_制作_借用(&DashLine_Type, 元素);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

#define DASH_SEQ_GETTER(cname, pyname, item_type) \
    static PyObject *Py_虚线_获取_##cname(ChanObject *self, void *c) { \
        虚线 *d = (虚线*)self->ptr; \
        size_t n = d->pyname.长度; \
        PyObject *py_列表 = PyList_New((Py_ssize_t)n); \
        if (!py_列表) return NULL; \
        for (size_t i = 0; i < n; i++) { \
            void *元素 = 动态数组_获取(&d->pyname, i); \
            PyObject *py_包装 = Py_制作_借用(item_type, 元素); \
            if (!py_包装) { Py_DECREF(py_列表); return NULL; } \
            PyList_SET_ITEM(py_列表, (Py_ssize_t)i, py_包装); \
        } \
        return py_列表; \
    }

DASH_SEQ_GETTER(特征序列, 特征序列, &SegFeature_Type)
DASH_SEQ_GETTER(实_中枢序列, 实_中枢序列, &Hub_Type)
DASH_SEQ_GETTER(虚_中枢序列, 虚_中枢序列, &Hub_Type)
DASH_SEQ_GETTER(合_中枢序列, 合_中枢序列, &Hub_Type)

#undef DASH_SEQ_GETTER

/* 虚线 methods */

static PyObject *Py_虚线_之前是(ChanObject *self, PyObject *py_其他) {
    void *其他对象 = Py_解包(py_其他, &DashLine_Type);
    if (!其他对象) return NULL;
    return PyBool_FromLong(虚线_之前是((虚线 *) self->ptr, (虚线 *) 其他对象));
}

static PyObject *Py_虚线_之后是(ChanObject *self, PyObject *py_其他) {
    void *其他对象 = Py_解包(py_其他, &DashLine_Type);
    if (!其他对象) return NULL;
    return PyBool_FromLong(虚线_之后是((虚线 *) self->ptr, (虚线 *) 其他对象));
}

static PyObject *Py_虚线_获取普K序列(ChanObject *self, PyObject *py_观察者) {
    /* py_观察者 must be an 观察者 */
    void *观察者指针 = Py_解包(py_观察者, &Observer_Type);
    if (!观察者指针) return NULL;
    K线 **py_输出 = NULL;
    size_t 输出长度 = 0;
    虚线_获取普K序列((虚线 *) self->ptr, (观察者 *) 观察者指针, &py_输出, &输出长度);
    if (!py_输出) Py_RETURN_NONE;
    PyObject *py_列表 = PyList_New((Py_ssize_t) 输出长度);
    if (!py_列表) return NULL;
    for (size_t i = 0; i < 输出长度; i++) {
        PyObject *py_包装 = Py_制作_借用(&KLine_Type, py_输出[i]);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

static PyObject *Py_虚线_获取缠K序列(ChanObject *self, PyObject *py_观察者) {
    void *观察者指针 = Py_解包(py_观察者, &Observer_Type);
    if (!观察者指针) return NULL;
    缠论K线 **py_输出 = NULL;
    size_t 输出长度 = 0;
    虚线_获取缠K序列((虚线 *) self->ptr, (观察者 *) 观察者指针, &py_输出, &输出长度);
    if (!py_输出) Py_RETURN_NONE;
    PyObject *py_列表 = PyList_New((Py_ssize_t) 输出长度);
    if (!py_列表) return NULL;
    for (size_t i = 0; i < 输出长度; i++) {
        PyObject *py_包装 = Py_制作_借用(&ChanKLine_Type, py_输出[i]);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

/* --- 虚线 setters --- */
static int Py_虚线_设置_序号(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((虚线 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_虚线_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) return -1;
    strncpy(((虚线 *) self->ptr)->标识, s, 63);
    ((虚线 *) self->ptr)->标识[63] = '\0';
    return 0;
}

static int Py_虚线_设置_级别(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((虚线 *) self->ptr)->级别 = (int) v;
    return 0;
}

static int Py_虚线_设置_高(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((虚线 *) self->ptr)->高 = v;
    return 0;
}

static int Py_虚线_设置_低(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) return -1;
    ((虚线 *) self->ptr)->低 = v;
    return 0;
}

static int Py_虚线_设置_有效性(ChanObject *self, PyObject *value, void *c) {
    ((虚线 *) self->ptr)->有效性 = PyObject_IsTrue(value) ? true : false;
    return 0;
}

static int Py_虚线_设置_模式(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) return -1;
    strncpy(((虚线 *) self->ptr)->模式, s, 31);
    ((虚线 *) self->ptr)->模式[31] = '\0';
    return 0;
}

static int Py_虚线_设置_短路修正(ChanObject *self, PyObject *value, void *c) {
    ((虚线 *) self->ptr)->短路修正 = PyObject_IsTrue(value) ? true : false;
    return 0;
}

static PyGetSetDef DashLine_getset[] = {
    {"序号", (getter) Py_虚线_获取_序号, (setter) Py_虚线_设置_序号, NULL, NULL},
    {"标识", (getter) Py_虚线_获取_标识, (setter) Py_虚线_设置_标识, NULL, NULL},
    {"级别", (getter) Py_虚线_获取_级别, (setter) Py_虚线_设置_级别, NULL, NULL},
    {"文", (getter) Py_虚线_获取_文, NULL, NULL, NULL},
    {"武", (getter) Py_虚线_获取_武, NULL, NULL, NULL},
    {"方向", (getter) Py_虚线_获取_方向, NULL, NULL, NULL},
    {"高", (getter) Py_虚线_获取_高, (setter) Py_虚线_设置_高, NULL, NULL},
    {"低", (getter) Py_虚线_获取_低, (setter) Py_虚线_设置_低, NULL, NULL},
    {"有效性", (getter) Py_虚线_获取_有效性, (setter) Py_虚线_设置_有效性, NULL, NULL},
    {"模式", (getter) Py_虚线_获取_模式, (setter) Py_虚线_设置_模式, NULL, NULL},
    {"确认K线", (getter) Py_虚线_获取_确认K线, NULL, NULL, NULL},
    {"前一缺口", (getter) Py_虚线_获取_前一缺口, NULL, NULL, NULL},
    {"前一结束位置", (getter) Py_虚线_获取_前一结束位置, NULL, NULL, NULL},
    {"短路修正", (getter) Py_虚线_获取_短路修正, (setter) Py_虚线_设置_短路修正, NULL, NULL},
    {"基础序列", (getter) Py_虚线_获取_基础序列, NULL, NULL, NULL},
    {"特征序列", (getter) Py_虚线_获取_特征序列, NULL, NULL, NULL},
    {"实_中枢序列", (getter) Py_虚线_获取_实_中枢序列, NULL, NULL, NULL},
    {"虚_中枢序列", (getter) Py_虚线_获取_虚_中枢序列, NULL, NULL, NULL},
    {"合_中枢序列", (getter) Py_虚线_获取_合_中枢序列, NULL, NULL, NULL},
    {NULL}
};

static PyMethodDef DashLine_methods[] = {
    {"之前是", (PyCFunction) Py_虚线_之前是, METH_O, "Check if self precedes 其他对象."},
    {"之后是", (PyCFunction) Py_虚线_之后是, METH_O, "Check if self follows 其他对象."},
    {"获取普K序列", (PyCFunction) Py_虚线_获取普K序列, METH_O, "Extract raw K-line pointers for this dash."},
    {"获取缠K序列", (PyCFunction) Py_虚线_获取缠K序列, METH_O, "Extract Chan K-line pointers for this dash."},
    {
        "创建笔", (PyCFunction) Py_笔_创建笔,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "虚线.创建笔(文, 武, 有效性=True) — create a stroke from two fractals."
    },
    {
        "创建线段", (PyCFunction) Py_线段_创建线段,
        METH_CLASS | METH_O,
        "虚线.创建线段(arr) — create a segment from a 动态数组 of strokes."
    },
    {NULL}
};

static PyObject *Py_虚线_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    虚线 *d = (虚线 *) self->ptr;
    char 缓冲[384];
    if (strcmp(d->标识, "笔") == 0) {
        snprintf(缓冲, sizeof(缓冲),
                 "笔(%d, %s, %s, %s, 周期: %d, 数量: %d)",
                 d->序号,
                 相对方向_到名称(虚线_方向(d)),
                 分型结构_到名称(d->文->结构),
                 分型结构_到名称(d->武->结构),
                 d->文->中->周期,
                 d->武->中->序号 - d->文->中->序号 + 1);
    } else {
        snprintf(缓冲, sizeof(缓冲),
                 "%s<%d, %s, %s, %s, %s, 数量: %zu>",
                 d->标识, d->序号,
                 线段_四象(d),
                 相对方向_到名称(虚线_方向(d)),
                 分型结构_到名称(d->文->结构),
                 分型结构_到名称(d->武->结构),
                 d->基础序列.长度);
    }
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject DashLine_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.虚线",
    .tp_basicsize = sizeof(DashLineObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getset = DashLine_getset,
    .tp_methods = DashLine_methods,
    .tp_repr = (reprfunc) Py_虚线_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "虚线 — 笔和线段的抽象基类。",
};

/* ================================================================
 *  笔 (Stroke) type — extends 虚线
 * ================================================================ */

typedef struct {
    ChanObject base;
} StrokeObject;

static PyObject *Py_笔_创建笔(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"文", "武", "有效性", NULL};
    PyObject *py_文, *py_武;
    int 有效性 = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|p", kwnames,
                                     &Fractal_Type, &py_文, &Fractal_Type, &py_武, &有效性))
        return NULL;
    void *ptr = 虚线_创建笔(
        (分型 *) ((ChanObject *) py_文)->ptr,
        (分型 *) ((ChanObject *) py_武)->ptr,
        (bool) 有效性);
    if (!ptr) return PyErr_NoMemory();
    return Py_制作_拥有(&Stroke_Type, ptr);
}

static PyObject *Py_笔_相对关系(ChanObject *self, PyObject *py_配置) {
    void *配置 = Py_解包(py_配置, &ChanConfig_Type);
    if (!配置) return NULL;
    return PyBool_FromLong(笔_相对关系((虚线 *) self->ptr, (缠论配置 *) 配置));
}

static PyMethodDef Stroke_methods[] = {
    {
        "创建笔", (PyCFunction) Py_笔_创建笔,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "Create a stroke (笔) from two fractals."
    },
    {
        "相对关系", (PyCFunction) Py_笔_相对关系, METH_O,
        "Determine relative relationship of this stroke."
    },
    {
        "分析", (PyCFunction) Py_笔_分析,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "笔.分析(初始分型, 分型序列, 笔序列, 缠K序列, 普K序列, 配置) — "
        "run stroke analysis on arbitrary dynamic arrays."
    },
    {
        "获取缠K数量", (PyCFunction) Py_笔_获取缠K数量,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "笔.获取缠K数量(缠K序列, 笔序列, 配置) — count Chan K-lines in strokes."
    },
    {
        "次高", (PyCFunction) Py_笔_次高,
        METH_CLASS | METH_VARARGS,
        "笔.次高(缠K序列, 笔内相同终点取舍) — second highest Chan K-line."
    },
    {
        "次低", (PyCFunction) Py_笔_次低,
        METH_CLASS | METH_VARARGS,
        "笔.次低(缠K序列, 笔内相同终点取舍) — second lowest Chan K-line."
    },
    {
        "实际高点", (PyCFunction) Py_笔_实际高点,
        METH_CLASS | METH_VARARGS,
        "笔.实际高点(缠K序列, 笔内相同终点取舍) — actual highest Chan K-line."
    },
    {
        "实际低点", (PyCFunction) Py_笔_实际低点,
        METH_CLASS | METH_VARARGS,
        "笔.实际低点(缠K序列, 笔内相同终点取舍) — actual lowest Chan K-line."
    },
    {
        "以文会友", (PyCFunction) Py_笔_以文会友,
        METH_CLASS | METH_VARARGS,
        "笔.以文会友(笔序列, 文) — find stroke by civil (文) fractal."
    },
    {
        "以武会友", (PyCFunction) Py_笔_以武会友,
        METH_CLASS | METH_VARARGS,
        "笔.以武会友(笔序列, 武) — find stroke by martial (武) fractal."
    },
    {
        "根据缠K找笔", (PyCFunction) Py_笔_根据缠K找笔,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "笔.根据缠K找笔(笔序列, 缠K, 偏移=0) — find stroke by Chan K-line."
    },
    {NULL}
};

static PyObject *Py_笔_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    虚线 *d = (虚线 *) self->ptr;
    char 缓冲[384];
    snprintf(缓冲, sizeof(缓冲),
             "笔(%d, %s, %s<%lld,%.2f>, %s<%lld,%.2f>, 周期: %d, 数量: %d)",
             d->序号,
             相对方向_到名称(虚线_方向(d)),
             分型结构_到名称(d->文->结构),
             (long long) d->文->时间戳, d->文->分型特征值,
             分型结构_到名称(d->武->结构),
             (long long) d->武->时间戳, d->武->分型特征值,
             d->文->中->周期,
             d->武->中->序号 - d->文->中->序号 + 1);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject Stroke_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.笔",
    .tp_basicsize = sizeof(StrokeObject),
    .tp_base = &DashLine_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = Stroke_methods,
    .tp_repr = (reprfunc) Py_笔_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "笔 — 两个分型之间的缠论K线序列。",
};

/* ================================================================
 *  线段特征 (Segment Feature) type
 * ================================================================ */

typedef struct {
    ChanObject base;
} SegFeatureObject;

static PyObject *Py_线段特征_获取_序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((线段特征 *) self->ptr)->序号);
}

static PyObject *Py_线段特征_获取_标识(ChanObject *self, void *c) {
    return PyUnicode_FromString(((线段特征 *) self->ptr)->标识);
}

static PyObject *Py_线段特征_获取_方向(ChanObject *self, void *c) {
    return PyLong_FromLong((long) 线段特征_方向((线段特征 *) self->ptr));
}

static PyObject *Py_线段特征_获取_文(ChanObject *self, void *c) {
    return Py_制作_借用(&Fractal_Type, 线段特征_文((线段特征 *) self->ptr));
}

static PyObject *Py_线段特征_获取_武(ChanObject *self, void *c) {
    return Py_制作_借用(&Fractal_Type, 线段特征_武((线段特征 *) self->ptr));
}

static PyObject *Py_线段特征_获取_高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(线段特征_高((线段特征 *) self->ptr));
}

static PyObject *Py_线段特征_获取_低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(线段特征_低((线段特征 *) self->ptr));
}

static PyObject *Py_线段特征_获取_元素(ChanObject *self, void *c) {
    线段特征 *sf = (线段特征 *) self->ptr;
    PyObject *py_列表 = PyList_New((Py_ssize_t) sf->元素.长度);
    if (!py_列表) return NULL;
    for (size_t i = 0; i < sf->元素.长度; i++) {
        void *元素 = 动态数组_获取(&sf->元素, i);
        PyObject *py_包装 = Py_制作_借用(&DashLine_Type, 元素);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

/* --- 线段特征 setters --- */
static int Py_线段特征_设置_序号(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((线段特征 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_线段特征_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) return -1;
    strncpy(((线段特征 *) self->ptr)->标识, s, 127);
    ((线段特征 *) self->ptr)->标识[127] = '\0';
    return 0;
}

static PyGetSetDef SegFeature_getset[] = {
    {"序号", (getter) Py_线段特征_获取_序号, (setter) Py_线段特征_设置_序号, NULL, NULL},
    {"标识", (getter) Py_线段特征_获取_标识, (setter) Py_线段特征_设置_标识, NULL, NULL},
    {"方向", (getter) Py_线段特征_获取_方向, NULL, NULL, NULL},
    {"文", (getter) Py_线段特征_获取_文, NULL, NULL, NULL},
    {"武", (getter) Py_线段特征_获取_武, NULL, NULL, NULL},
    {"高", (getter) Py_线段特征_获取_高, NULL, NULL, NULL},
    {"低", (getter) Py_线段特征_获取_低, NULL, NULL, NULL},
    {"元素", (getter) Py_线段特征_获取_元素, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_线段特征_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    线段特征 *sf = (线段特征 *) self->ptr;
    char 缓冲[384];
    snprintf(缓冲, sizeof(缓冲),
             "%s<%s, %s, %s, %zu>",
             sf->标识,
             相对方向_到名称(线段特征_方向(sf)),
             线段特征_文(sf) ? 分型结构_到名称(线段特征_文(sf)->结构) : "?",
             线段特征_武(sf) ? 分型结构_到名称(线段特征_武(sf)->结构) : "?",
             sf->元素.长度);
    return PyUnicode_FromString(缓冲);
}

static PyMethodDef SegFeature_methods[] = {
    {
        "静态分析", (PyCFunction) Py_线段特征_静态分析,
        METH_CLASS | METH_VARARGS,
        "Static analysis: 线段特征.静态分析(动态数组, 方向, 四象, 是否忽视, 结果动态数组)"
    },
    {NULL}
};

static PyTypeObject SegFeature_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.线段特征",
    .tp_basicsize = sizeof(SegFeatureObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = SegFeature_getset,
    .tp_methods = SegFeature_methods,
    .tp_repr = (reprfunc) Py_线段特征_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "线段特征 — 线段分析的特征序列元素。",
};

/* ================================================================
 *  线段 (Segment) type — extends 虚线
 * ================================================================ */

typedef struct {
    ChanObject base;
} SegmentObject;

static PyObject *Py_线段_获取_四象(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    return PyUnicode_FromString(线段_四象((虚线 *) self->ptr));
}

static PyObject *Py_线段_获取_特征分型终结(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    return PyBool_FromLong(线段_特征分型终结((虚线 *) self->ptr));
}

static PyObject *Py_线段_获取_特征序列状态(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    bool left = false, mid = false, right = false;
    线段_特征序列状态((虚线 *) self->ptr, &left, &mid, &right);
    return Py_BuildValue("(ppp)", left, mid, right);
}

static PyObject *Py_线段_获取_缺口(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    缺口 *g = 线段_获取缺口((虚线 *) self->ptr);
    if (!g) Py_RETURN_NONE;
    return Py_制作_借用(&Gap_Type, g);
}

static PyObject *Py_线段_查找贯穿伤(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    虚线 *d = 线段_查找贯穿伤((虚线 *) self->ptr);
    if (!d) Py_RETURN_NONE;
    return Py_制作_借用(&DashLine_Type, d);
}

static PyMethodDef Segment_methods[] = {
    {
        "四象", (PyCFunction) Py_线段_获取_四象, METH_NOARGS,
        "Get the Sixiang (四象 = 老阴/老阳/少阴/少阳) of this segment."
    },
    {
        "特征分型终结", (PyCFunction) Py_线段_获取_特征分型终结, METH_NOARGS,
        "Check if the feature fractal sequence has ended."
    },
    {
        "特征序列状态", (PyCFunction) Py_线段_获取_特征序列状态, METH_NOARGS,
        "Get the state of the feature sequence (left, mid, right)."
    },
    {
        "获取缺口", (PyCFunction) Py_线段_获取_缺口, METH_NOARGS,
        "Get the gap for this segment."
    },
    {
        "查找贯穿伤", (PyCFunction) Py_线段_查找贯穿伤, METH_NOARGS,
        "Find the penetrating injury (贯穿伤) in this segment."
    },
    {
        "创建线段", (PyCFunction) Py_线段_创建线段,
        METH_CLASS | METH_O,
        "Create a segment from a 动态数组 of strokes."
    },
    {
        "分析", (PyCFunction) Py_线段_分析,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "线段.分析(笔序列, 线段序列, 配置) — run segment analysis on stroke arrays."
    },
    {NULL}
};

static PyObject *Py_线段_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    虚线 *d = (虚线 *) self->ptr;
    char 缓冲[384];
    snprintf(缓冲, sizeof(缓冲),
             "%s<%d, %s, %s, %s<%lld,%.2f>, %s<%lld,%.2f>, 数量: %zu>",
             d->标识, d->序号,
             线段_四象(d),
             相对方向_到名称(虚线_方向(d)),
             分型结构_到名称(d->文->结构),
             (long long) d->文->时间戳, d->文->分型特征值,
             分型结构_到名称(d->武->结构),
             (long long) d->武->时间戳, d->武->分型特征值,
             d->基础序列.长度);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject Segment_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.线段",
    .tp_basicsize = sizeof(SegmentObject),
    .tp_base = &DashLine_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = Segment_methods,
    .tp_repr = (reprfunc) Py_线段_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "线段 — 重叠笔构成的序列。",
};

/* ================================================================
 *  中枢 (Hub) type
 * ================================================================ */

typedef struct {
    ChanObject base;
} HubObject;

static PyObject *Py_中枢_获取_序号(ChanObject *self, void *c) {
    return PyLong_FromLong(((中枢 *) self->ptr)->序号);
}

static PyObject *Py_中枢_获取_标识(ChanObject *self, void *c) {
    return PyUnicode_FromString(((中枢 *) self->ptr)->标识);
}

static PyObject *Py_中枢_获取_级别(ChanObject *self, void *c) {
    return PyLong_FromLong(((中枢 *) self->ptr)->级别);
}

static PyObject *Py_中枢_获取_方向(ChanObject *self, void *c) {
    return PyLong_FromLong((long) 中枢_方向((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(中枢_高((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(中枢_低((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_高高(ChanObject *self, void *c) {
    return PyFloat_FromDouble(中枢_高高((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_低低(ChanObject *self, void *c) {
    return PyFloat_FromDouble(中枢_低低((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_完整性(ChanObject *self, void *c) {
    return PyBool_FromLong(中枢_完整性((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_第三买卖线(ChanObject *self, void *c) {
    虚线 *d = ((中枢 *) self->ptr)->第三买卖线;
    if (!d) Py_RETURN_NONE;
    return Py_制作_借用(&DashLine_Type, d);
}

static PyObject *Py_中枢_获取_本级_第三买卖线(ChanObject *self, void *c) {
    虚线 *d = ((中枢 *) self->ptr)->本级_第三买卖线;
    if (!d) Py_RETURN_NONE;
    return Py_制作_借用(&DashLine_Type, d);
}

static PyObject *Py_中枢_获取_元素(ChanObject *self, void *c) {
    中枢 *h = (中枢 *) self->ptr;
    PyObject *py_列表 = PyList_New((Py_ssize_t) h->元素.长度);
    if (!py_列表) return NULL;
    for (size_t i = 0; i < h->元素.长度; i++) {
        void *元素 = 动态数组_获取(&h->元素, i);
        PyObject *py_包装 = Py_制作_借用(&DashLine_Type, 元素);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

static PyObject *Py_中枢_当前状态(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    return PyUnicode_FromString(中枢_当前状态((中枢 *) self->ptr));
}

/* --- 中枢 setters --- */
static int Py_中枢_设置_序号(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((中枢 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_中枢_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) return -1;
    strncpy(((中枢 *) self->ptr)->标识, s, 127);
    ((中枢 *) self->ptr)->标识[127] = '\0';
    return 0;
}

static int Py_中枢_设置_级别(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) return -1;
    ((中枢 *) self->ptr)->级别 = (int) v;
    return 0;
}

static PyGetSetDef Hub_getset[] = {
    {"序号", (getter) Py_中枢_获取_序号, (setter) Py_中枢_设置_序号, NULL, NULL},
    {"标识", (getter) Py_中枢_获取_标识, (setter) Py_中枢_设置_标识, NULL, NULL},
    {"级别", (getter) Py_中枢_获取_级别, (setter) Py_中枢_设置_级别, NULL, NULL},
    {"方向", (getter) Py_中枢_获取_方向, NULL, NULL, NULL},
    {"高", (getter) Py_中枢_获取_高, NULL, NULL, NULL},
    {"低", (getter) Py_中枢_获取_低, NULL, NULL, NULL},
    {"高高", (getter) Py_中枢_获取_高高, NULL, NULL, NULL},
    {"低低", (getter) Py_中枢_获取_低低, NULL, NULL, NULL},
    {"完整性", (getter) Py_中枢_获取_完整性, NULL, NULL, NULL},
    {"第三买卖线", (getter) Py_中枢_获取_第三买卖线, NULL, NULL, NULL},
    {"本级_第三买卖线", (getter) Py_中枢_获取_本级_第三买卖线, NULL, NULL, NULL},
    {"元素", (getter) Py_中枢_获取_元素, NULL, NULL, NULL},
    {NULL}
};

static PyMethodDef Hub_methods[] = {
    {
        "当前状态", (PyCFunction) Py_中枢_当前状态, METH_NOARGS,
        "Get the current state of this hub (中枢状态 string)."
    },
    {
        "获取序列", (PyCFunction) Py_中枢_获取序列, METH_NOARGS,
        "Get the element sequence of this hub as a 动态数组."
    },
    {
        "分析", (PyCFunction) Py_中枢_分析,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "中枢.分析(虚线序列, 中枢序列, 跳过首部, 标识) — run hub analysis on dashline arrays."
    },
    {NULL}
};

static PyObject *Py_中枢_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    中枢 *h = (中枢 *) self->ptr;
    char 缓冲[384];
    if (h->元素.长度 > 0) {
        虚线 *首 = (虚线 *) 动态数组_获取(&h->元素, 0);
        虚线 *末 = (虚线 *) 动态数组_获取(&h->元素, h->元素.长度 - 1);
        snprintf(缓冲, sizeof(缓冲),
                 "%s(%.2f, %.2f, 元素数量: %zu, %lld ===>>> %lld)",
                 h->标识,
                 中枢_高(h), 中枢_低(h),
                 h->元素.长度,
                 (long long) 首->文->时间戳,
                 (long long) 末->武->时间戳);
    } else {
        snprintf(缓冲, sizeof(缓冲),
                 "%s(%.2f, %.2f, 元素数量: 0)",
                 h->标识, 中枢_高(h), 中枢_低(h));
    }
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject Hub_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.中枢",
    .tp_basicsize = sizeof(HubObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getset = Hub_getset,
    .tp_methods = Hub_methods,
    .tp_repr = (reprfunc) Py_中枢_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "中枢 — 三个连续笔/线段的重叠区域。",
};

/* ================================================================
 *  动态数组 (Dynamic Array) type — Python py_列表-like wrapper
 * ================================================================ */

typedef struct {
    PyObject_HEAD
    动态数组 arr;
    bool owns_elements; /* true if append() added refs; false if C-populated */
} DynArrayObject;

static void Py_动态数组_释放(DynArrayObject *self) {
    /* 弱引用模型：清除所有弱引用后释放数组缓冲区 */
    弱引用_数组清除(&self->arr);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *Py_动态数组_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
    DynArrayObject *self = (DynArrayObject *) type->tp_alloc(type, 0);
    if (!self) return NULL;
    动态数组_初始化(&self->arr, 4);
    self->owns_elements = true; /* user-created: pool owns elements, append() no longer calls 引用() */
    return (PyObject *) self;
}

static Py_ssize_t Py_动态数组_长度(DynArrayObject *self) {
    return (Py_ssize_t) self->arr.长度;
}

static PyObject *Py_动态数组_获取元素(DynArrayObject *self, Py_ssize_t i) {
    if (i < 0) i += (Py_ssize_t) self->arr.长度;
    if (i < 0 || (size_t) i >= self->arr.长度) {
        PyErr_SetString(PyExc_IndexError, "动态数组 index py_输出 of range");
        return NULL;
    }
    void *ptr = 动态数组_获取(&self->arr, (size_t) i);
    if (!ptr) Py_RETURN_NONE;
    /* Return a borrowed reference — the array manages the lifetime */
    /* We infer the type from the object header... but we don't know the exact type.
       Return raw pointer as fallback. For typed arrays, use specific accessors. */
    PyObject *m = PyLong_FromVoidPtr(ptr);
    if (!m) return NULL;
    /* Attach type info as an attribute for debugging */
    PyObject *py_类型整 = PyLong_FromLong((long) ((对象头结构 *) ptr)->类型标记);
    PyObject_SetAttrString(m, "_chan_type", py_类型整);
    Py_DECREF(py_类型整);
    return m;
}

/* append(py_元素) — 弱引用模型：增加元素弱引用计数 */
static PyObject *Py_动态数组_追加(DynArrayObject *self, PyObject *py_元素) {
    /* Accept any ChanObject wrapper */
    if (!PyObject_TypeCheck(py_元素, &ChanObject_Type)) {
        PyErr_SetString(PyExc_TypeError,
                        "动态数组.append() requires a chan object (K线, 缠论K线, 分型, 笔, 线段, 中枢, etc.)");
        return NULL;
    }
    void *ptr = ((ChanObject *) py_元素)->ptr;
    if (!ptr) {
        PyErr_SetString(PyExc_ValueError, "cannot append a released object");
        return NULL;
    }
    弱引用_数组追加(&self->arr, ptr);
    Py_RETURN_NONE;
}

/* pop() — returns owned wrapper, caller must release */
static PyObject *Py_动态数组_弹出(DynArrayObject *self, PyObject *Py_UNUSED(py_忽略)) {
    if (self->arr.长度 == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty 动态数组");
        return NULL;
    }
    void *ptr = 弱引用_数组弹出(&self->arr);
    if (!ptr) Py_RETURN_NONE;
    /* Return as an owned ChanObject base wrapper.
       The caller can inspect _chan_type attribute to determine the exact type. */
    return Py_制作_拥有(&ChanObject_Type, ptr);
}

/* pop_typed(type, pytype) — pop and py_包装 with a specific type */
static PyObject *Py_动态数组_弹出类型(DynArrayObject *self, PyObject *args) {
    PyTypeObject *pytype = NULL;
    if (!PyArg_ParseTuple(args, "O!", &PyType_Type, &pytype))
        return NULL;
    if (self->arr.长度 == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty 动态数组");
        return NULL;
    }
    void *ptr = 弱引用_数组弹出(&self->arr);
    if (!ptr) Py_RETURN_NONE;
    return Py_制作_借用(pytype, ptr);
}

/* clear() */
static PyObject *Py_动态数组_清空(DynArrayObject *self, PyObject *Py_UNUSED(py_忽略)) {
    弱引用_数组清除(&self->arr);
    动态数组_初始化(&self->arr, 4);
    Py_RETURN_NONE;
}

static PySequenceMethods DynArray_as_seq = {
    .sq_length = (lenfunc) Py_动态数组_长度,
    .sq_item = (ssizeargfunc) Py_动态数组_获取元素,
};

static PyMethodDef DynArray_methods[] = {
    {
        "append", (PyCFunction) Py_动态数组_追加, METH_O,
        "Append a chan object (transfers ownership via 引用)."
    },
    {
        "pop", (PyCFunction) Py_动态数组_弹出, METH_NOARGS,
        "Pop the last element (returns owned wrapper)."
    },
    {
        "pop_typed", (PyCFunction) Py_动态数组_弹出类型, METH_VARARGS,
        "Pop the last element and py_包装 with the given Python type."
    },
    {
        "clear", (PyCFunction) Py_动态数组_清空, METH_NOARGS,
        "Clear all elements (releases array buffer, pool owns element memory)."
    },
    {NULL}
};

static PyObject *Py_动态数组_repr(DynArrayObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    char 缓冲[128];
    snprintf(缓冲, sizeof(缓冲), "<动态数组 当前长度=%zu 容量=%zu>",
             self->arr.长度, self->arr.容量);
    return PyUnicode_FromString(缓冲);
}

static PyTypeObject DynArray_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.动态数组",
    .tp_basicsize = sizeof(DynArrayObject),
    .tp_dealloc = (destructor) Py_动态数组_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = Py_动态数组_new,
    .tp_as_sequence = &DynArray_as_seq,
    .tp_methods = DynArray_methods,
    .tp_repr = (reprfunc) Py_动态数组_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "动态数组 — Python 列表风格的 C 动态数组包装。\n\n"
    "通过引用计数管理元素生命周期。\n"
    "使用 append() 添加缠论对象（自动增加引用计数）。\n"
    "使用 pop() 移除元素（返回拥有包装）。\n"
    "Supports len() and [] indexing.",
};

/* ================================================================
 *  Phase 3 methods — use 动态数组
 * ================================================================ */

/* 线段.创建线段(动态数组) -> 线段 */
static PyObject *Py_线段_创建线段(PyObject *py_类, PyObject *py_参数) {
    if (!PyObject_TypeCheck(py_参数, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError,
                        "创建线段 requires a 动态数组 of strokes");
        return NULL;
    }
    DynArrayObject *da = (DynArrayObject *) py_参数;
    虚线 *ptr = 虚线_创建线段(&da->arr);
    if (!ptr) return PyErr_NoMemory();
    /* C function took ownership of elements; prevent double-free */
    da->owns_elements = false;
    return Py_制作_拥有(&Segment_Type, ptr);
}

/* 线段特征.静态分析(虚线序列, 方向, 四象, 是否忽视, 结果) */
static PyObject *Py_线段特征_静态分析(PyObject *py_类, PyObject *args) {
    PyObject *py_虚线序列;
    int direction, ignore;
    const char *sixiang;
    PyObject *result_obj;

    if (!PyArg_ParseTuple(args, "OispO!",
                          &py_虚线序列, &direction, &sixiang, &ignore,
                          &DynArray_Type, &result_obj))
        return NULL;

    if (!PyObject_TypeCheck(py_虚线序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError,
                        "静态分析 arg1 requires a 动态数组 of dashes");
        return NULL;
    }

    线段特征_静态分析(
        &((DynArrayObject *) py_虚线序列)->arr,
        (相对方向) direction, sixiang, (bool) ignore,
        &((DynArrayObject *) result_obj)->arr);
    /* C function populated py_结果 without calling 引用() */
    ((DynArrayObject *) result_obj)->owns_elements = false;
    Py_RETURN_NONE;
}

/* 中枢.获取序列() -> 动态数组 */
static PyObject *Py_中枢_获取序列(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    DynArrayObject *py_结果 = (DynArrayObject *) Py_动态数组_new(&DynArray_Type, NULL, NULL);
    if (!py_结果) return NULL;
    中枢_获取序列((中枢 *) self->ptr, &py_结果->arr);
    py_结果->owns_elements = false; /* C function populated without 引用() */
    return (PyObject *) py_结果;
}

/* 笔.分析(初始分型, 分型序列, 笔序列, 缠K序列, 普K序列, 配置) */
static PyObject *Py_笔_分析(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {
        "初始分型", "分型序列", "笔序列", "缠K序列", "普K序列", "配置", NULL
    };
    PyObject *init_frac_obj = Py_None; /* optional, defaults to None → NULL */
    PyObject *frac_seq_obj, *py_笔序列, *py_缠K序列, *k_seq_obj, *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "|OO!O!O!O!O!", kwnames,
                                     &init_frac_obj,
                                     &DynArray_Type, &frac_seq_obj,
                                     &DynArray_Type, &py_笔序列,
                                     &DynArray_Type, &py_缠K序列,
                                     &DynArray_Type, &k_seq_obj,
                                     &ChanConfig_Type, &py_配置))
        return NULL;

    /* Extract initial fractal (optional) */
    分型 *init_fractal = NULL;
    if (init_frac_obj != Py_None) {
        if (!PyObject_TypeCheck(init_frac_obj, &Fractal_Type)) {
            PyErr_SetString(PyExc_TypeError,
                            "初始分型 must be a 分型 instance or None");
            return NULL;
        }
        init_fractal = (分型 *) ((ChanObject *) init_frac_obj)->ptr;
    }

    缠论配置 *配置 = (缠论配置 *) ((ChanObject *) py_配置)->ptr;

    笔_分析(init_fractal,
         &((DynArrayObject *) frac_seq_obj)->arr,
         &((DynArrayObject *) py_笔序列)->arr,
         &((DynArrayObject *) py_缠K序列)->arr,
         &((DynArrayObject *) k_seq_obj)->arr,
         配置);

    /* C function populates 笔序列 without calling 引用() */
    ((DynArrayObject *) py_笔序列)->owns_elements = false;

    Py_RETURN_NONE;
}

static PyObject *Py_线段_分析(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {
        "笔序列", "线段序列", "配置", NULL
    };
    PyObject *py_笔序列, *seg_seq_obj, *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!", kwnames,
                                     &DynArray_Type, &py_笔序列,
                                     &DynArray_Type, &seg_seq_obj,
                                     &ChanConfig_Type, &py_配置))
        return NULL;

    缠论配置 *配置 = (缠论配置 *) ((ChanObject *) py_配置)->ptr;

    线段_分析(&((DynArrayObject *) py_笔序列)->arr,
          &((DynArrayObject *) seg_seq_obj)->arr,
          配置);

    ((DynArrayObject *) seg_seq_obj)->owns_elements = false;
    Py_RETURN_NONE;
}

static PyObject *Py_中枢_分析(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {
        "虚线序列", "中枢序列", "跳过首部", "标识", NULL
    };
    PyObject *py_虚线序列, *中枢_seq_obj;
    int 跳过首个 = 0;
    const char *id_str = "hub";

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|ps", kwnames,
                                     &DynArray_Type, &py_虚线序列,
                                     &DynArray_Type, &中枢_seq_obj,
                                     &跳过首个, &id_str))
        return NULL;

    中枢_分析(&((DynArrayObject *) py_虚线序列)->arr,
          &((DynArrayObject *) 中枢_seq_obj)->arr,
          (bool) 跳过首个, id_str);

    ((DynArrayObject *) 中枢_seq_obj)->owns_elements = false;
    Py_RETURN_NONE;
}

/* ================================================================
 *  笔 (Stroke) classmethods — matching chan.py 笔
 * ================================================================ */

static PyObject *Py_笔_获取缠K数量(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"缠K序列", "笔序列", "配置", NULL};
    PyObject *py_缠K序列, *py_笔序列, *py_配置;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!", kwnames,
                                     &DynArray_Type, &py_缠K序列,
                                     &DynArray_Type, &py_笔序列,
                                     &ChanConfig_Type, &py_配置))
        return NULL;
    int n = 笔_获取缠K数量(&((DynArrayObject *) py_缠K序列)->arr,
                     &((DynArrayObject *) py_笔序列)->arr,
                     (缠论配置 *) ((ChanObject *) py_配置)->ptr);
    return PyLong_FromLong(n);
}

static PyObject *Py_笔_次高(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点))
        return NULL;
    缠论K线 *ck = 笔_次高(&((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_次低(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点))
        return NULL;
    缠论K线 *ck = 笔_次低(&((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_实际高点(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点))
        return NULL;
    缠论K线 *ck = 笔_实际高点(&((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_实际低点(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点))
        return NULL;
    缠论K线 *ck = 笔_实际低点(&((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_以文会友(PyObject *py_类, PyObject *args) {
    PyObject *py_笔序列, *分型_obj;
    if (!PyArg_ParseTuple(args, "O!O!", &DynArray_Type, &py_笔序列,
                          &Fractal_Type, &分型_obj))
        return NULL;
    虚线 *s = 笔_以文会友(&((DynArrayObject *) py_笔序列)->arr,
                   (分型 *) ((ChanObject *) 分型_obj)->ptr);
    if (!s) Py_RETURN_NONE;
    return Py_制作_借用(&Stroke_Type, s);
}

static PyObject *Py_笔_以武会友(PyObject *py_类, PyObject *args) {
    PyObject *py_笔序列, *分型_obj;
    if (!PyArg_ParseTuple(args, "O!O!", &DynArray_Type, &py_笔序列,
                          &Fractal_Type, &分型_obj))
        return NULL;
    虚线 *s = 笔_以武会友(&((DynArrayObject *) py_笔序列)->arr,
                   (分型 *) ((ChanObject *) 分型_obj)->ptr);
    if (!s) Py_RETURN_NONE;
    return Py_制作_借用(&Stroke_Type, s);
}

static PyObject *Py_笔_根据缠K找笔(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"笔序列", "缠K", "偏移", NULL};
    PyObject *py_笔序列, *py_缠K;
    int 偏移量 = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|i", kwnames,
                                     &DynArray_Type, &py_笔序列,
                                     &ChanKLine_Type, &py_缠K,
                                     &偏移量))
        return NULL;
    虚线 *s = 笔_根据缠K找笔(&((DynArrayObject *) py_笔序列)->arr,
                     (缠论K线 *) ((ChanObject *) py_缠K)->ptr, 偏移量);
    if (!s) Py_RETURN_NONE;
    return Py_制作_借用(&Stroke_Type, s);
}

/* ================================================================
 *  缠论配置 (Config) type
 * ================================================================ */

/* Field name tables for string-keyed dispatch (same as chan_wrapper.c) */

static const char *CONFIG_BOOL_FIELDS[] = {
    "缠K合并替换", "笔内相同终点取舍", "笔内起始分型包含整笔",
    "笔内起始分型包含整笔_包括右", "笔内原始K线包含整笔",
    "笔次级成笔", "笔弱化", "线段_非缺口下穿刺",
    "线段_特征序列忽视老阴老阳", "线段_缺口后紧急修正", "线段_修正",
    "线段内部中枢图显", "扩展线段_当下分析",
    "分析笔", "分析线段", "分析扩展线段", "分析笔中枢", "分析线段中枢",
    "计算指标", "图表展示",
    "推送K线", "推送笔", "推送线段", "推送中枢",
    "图表展示_笔", "图表展示_线段", "图表展示_扩展线段",
    "图表展示_扩展线段_线段", "图表展示_线段_线段",
    "图表展示_中枢_笔", "图表展示_中枢_线段",
    "图表展示_中枢_扩展线段", "图表展示_中枢_扩展线段_线段",
    "图表展示_中枢_线段_线段", "图表展示_中枢_线段内部",
    "买卖点激进识别", "买卖点与MACD柱强相关",
    "买卖点_指标匹配_MACD", "买卖点_指标匹配_KDJ", "买卖点_指标匹配_RSI",
    "买卖点_峰值条件", "买卖点_计算线段BSP1", "买卖点_处理BSP2",
    "买卖点_计算线段BSP3", "买卖点_依赖T1", "买卖点_调试输出",
    "线段内部背驰_MACD", "线段内部背驰_斜率", "线段内部背驰_测度",
    NULL
};

static const char *CONFIG_INT_FIELDS[] = {
    "笔内元素数量", "笔弱化_原始数量", "买卖点偏移", "买卖点_T2S_最大层级",
    "平滑异同移动平均线_快线周期", "平滑异同移动平均线_慢线周期",
    "平滑异同移动平均线_信号周期", "相对强弱指数_周期",
    "相对强弱指数_移动平均线周期", "随机指标_RSV周期",
    "随机指标_K值平滑周期", "随机指标_D值平滑周期",
    NULL
};

static const char *CONFIG_DOUBLE_FIELDS[] = {
    "相对强弱指数_超买阈值", "相对强弱指数_超卖阈值",
    "随机指标_超买阈值", "随机指标_超卖阈值",
    "买卖点错过误差值", "买卖点_背离率", "买卖点_T2_回调阈值",
    NULL
};

static const char *CONFIG_STRING_FIELDS[] = {
    "手动终止", "指标计算方式", "买卖点_指标模式",
    "买卖点_计算方式", "买卖点_中枢来源", "线段内部背驰_模式",
    "标识", "加载文件路径",
    NULL
};

typedef struct {
    ChanObject base;
} ChanConfigObject;

/* field-level dispatch using 缠论配置* */

static bool Py_配置_设置_bool字段(缠论配置 *c, const char *field, bool value) {
    if (0) {
    } else if (strcmp(field, "缠K合并替换") == 0) c->缠K合并替换 = value;
    else if (strcmp(field, "笔内相同终点取舍") == 0) c->笔内相同终点取舍 = value;
    else if (strcmp(field, "笔内起始分型包含整笔") == 0) c->笔内起始分型包含整笔 = value;
    else if (strcmp(field, "笔内起始分型包含整笔_包括右") == 0) c->笔内起始分型包含整笔_包括右 = value;
    else if (strcmp(field, "笔内原始K线包含整笔") == 0) c->笔内原始K线包含整笔 = value;
    else if (strcmp(field, "笔次级成笔") == 0) c->笔次级成笔 = value;
    else if (strcmp(field, "笔弱化") == 0) c->笔弱化 = value;
    else if (strcmp(field, "线段_非缺口下穿刺") == 0) c->线段_非缺口下穿刺 = value;
    else if (strcmp(field, "线段_特征序列忽视老阴老阳") == 0) c->线段_特征序列忽视老阴老阳 = value;
    else if (strcmp(field, "线段_缺口后紧急修正") == 0) c->线段_缺口后紧急修正 = value;
    else if (strcmp(field, "线段_修正") == 0) c->线段_修正 = value;
    else if (strcmp(field, "线段内部中枢图显") == 0) c->线段内部中枢图显 = value;
    else if (strcmp(field, "扩展线段_当下分析") == 0) c->扩展线段_当下分析 = value;
    else if (strcmp(field, "分析笔") == 0) c->分析笔 = value;
    else if (strcmp(field, "分析线段") == 0) c->分析线段 = value;
    else if (strcmp(field, "分析扩展线段") == 0) c->分析扩展线段 = value;
    else if (strcmp(field, "分析笔中枢") == 0) c->分析笔中枢 = value;
    else if (strcmp(field, "分析线段中枢") == 0) c->分析线段中枢 = value;
    else if (strcmp(field, "计算指标") == 0) c->计算指标 = value;
    else if (strcmp(field, "图表展示") == 0) c->图表展示 = value;
    else if (strcmp(field, "推送K线") == 0) c->推送K线 = value;
    else if (strcmp(field, "推送笔") == 0) c->推送笔 = value;
    else if (strcmp(field, "推送线段") == 0) c->推送线段 = value;
    else if (strcmp(field, "推送中枢") == 0) c->推送中枢 = value;
    else if (strcmp(field, "图表展示_笔") == 0) c->图表展示_笔 = value;
    else if (strcmp(field, "图表展示_线段") == 0) c->图表展示_线段 = value;
    else if (strcmp(field, "图表展示_扩展线段") == 0) c->图表展示_扩展线段 = value;
    else if (strcmp(field, "图表展示_扩展线段_线段") == 0) c->图表展示_扩展线段_线段 = value;
    else if (strcmp(field, "图表展示_线段_线段") == 0) c->图表展示_线段_线段 = value;
    else if (strcmp(field, "图表展示_中枢_笔") == 0) c->图表展示_中枢_笔 = value;
    else if (strcmp(field, "图表展示_中枢_线段") == 0) c->图表展示_中枢_线段 = value;
    else if (strcmp(field, "图表展示_中枢_扩展线段") == 0) c->图表展示_中枢_扩展线段 = value;
    else if (strcmp(field, "图表展示_中枢_扩展线段_线段") == 0) c->图表展示_中枢_扩展线段_线段 = value;
    else if (strcmp(field, "图表展示_中枢_线段_线段") == 0) c->图表展示_中枢_线段_线段 = value;
    else if (strcmp(field, "图表展示_中枢_线段内部") == 0) c->图表展示_中枢_线段内部 = value;
    else if (strcmp(field, "买卖点激进识别") == 0) c->买卖点激进识别 = value;
    else if (strcmp(field, "买卖点与MACD柱强相关") == 0) c->买卖点与MACD柱强相关 = value;
    else if (strcmp(field, "买卖点_指标匹配_MACD") == 0) c->买卖点_指标匹配_MACD = value;
    else if (strcmp(field, "买卖点_指标匹配_KDJ") == 0) c->买卖点_指标匹配_KDJ = value;
    else if (strcmp(field, "买卖点_指标匹配_RSI") == 0) c->买卖点_指标匹配_RSI = value;
    else if (strcmp(field, "买卖点_峰值条件") == 0) c->买卖点_峰值条件 = value;
    else if (strcmp(field, "买卖点_计算线段BSP1") == 0) c->买卖点_计算线段BSP1 = value;
    else if (strcmp(field, "买卖点_处理BSP2") == 0) c->买卖点_处理BSP2 = value;
    else if (strcmp(field, "买卖点_计算线段BSP3") == 0) c->买卖点_计算线段BSP3 = value;
    else if (strcmp(field, "买卖点_依赖T1") == 0) c->买卖点_依赖T1 = value;
    else if (strcmp(field, "买卖点_调试输出") == 0) c->买卖点_调试输出 = value;
    else if (strcmp(field, "线段内部背驰_MACD") == 0) c->线段内部背驰_MACD = value;
    else if (strcmp(field, "线段内部背驰_斜率") == 0) c->线段内部背驰_斜率 = value;
    else if (strcmp(field, "线段内部背驰_测度") == 0) c->线段内部背驰_测度 = value;
    else return false;
    return true;
}

static bool Py_配置_获取_bool字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "缠K合并替换") == 0) return c->缠K合并替换;
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
    else return false;
}

static bool Py_配置_设置_int字段(缠论配置 *c, const char *field, int value) {
    if (0) {
    } else if (strcmp(field, "笔内元素数量") == 0) c->笔内元素数量 = value;
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
    else return false;
    return true;
}

static int Py_配置_获取int字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "笔内元素数量") == 0) return c->笔内元素数量;
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
    else return 0;
}

static bool Py_配置_设置_double字段(缠论配置 *c, const char *field, double value) {
    if (0) {
    } else if (strcmp(field, "相对强弱指数_超买阈值") == 0) c->相对强弱指数_超买阈值 = value;
    else if (strcmp(field, "相对强弱指数_超卖阈值") == 0) c->相对强弱指数_超卖阈值 = value;
    else if (strcmp(field, "随机指标_超买阈值") == 0) c->随机指标_超买阈值 = value;
    else if (strcmp(field, "随机指标_超卖阈值") == 0) c->随机指标_超卖阈值 = value;
    else if (strcmp(field, "买卖点错过误差值") == 0) c->买卖点错过误差值 = value;
    else if (strcmp(field, "买卖点_背离率") == 0) c->买卖点_背离率 = value;
    else if (strcmp(field, "买卖点_T2_回调阈值") == 0) c->买卖点_T2_回调阈值 = value;
    else return false;
    return true;
}

static double Py_配置_获取_double字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "相对强弱指数_超买阈值") == 0) return c->相对强弱指数_超买阈值;
    else if (strcmp(field, "相对强弱指数_超卖阈值") == 0) return c->相对强弱指数_超卖阈值;
    else if (strcmp(field, "随机指标_超买阈值") == 0) return c->随机指标_超买阈值;
    else if (strcmp(field, "随机指标_超卖阈值") == 0) return c->随机指标_超卖阈值;
    else if (strcmp(field, "买卖点错过误差值") == 0) return c->买卖点错过误差值;
    else if (strcmp(field, "买卖点_背离率") == 0) return c->买卖点_背离率;
    else if (strcmp(field, "买卖点_T2_回调阈值") == 0) return c->买卖点_T2_回调阈值;
    else return 0.0;
}

static bool Py_配置_设置_string字段(缠论配置 *c, const char *field, const char *value) {
    if (0) {
    } else if (strcmp(field, "手动终止") == 0) {
        c->手动终止[0] = '\0';
        strncpy(c->手动终止, value, 31);
        c->手动终止[31] = '\0';
    } else if (strcmp(field, "指标计算方式") == 0) {
        c->指标计算方式[0] = '\0';
        strncpy(c->指标计算方式, value, 31);
        c->指标计算方式[31] = '\0';
    } else if (strcmp(field, "买卖点_指标模式") == 0) {
        c->买卖点_指标模式[0] = '\0';
        strncpy(c->买卖点_指标模式, value, 31);
        c->买卖点_指标模式[31] = '\0';
    } else if (strcmp(field, "买卖点_计算方式") == 0) {
        c->买卖点_计算方式[0] = '\0';
        strncpy(c->买卖点_计算方式, value, 31);
        c->买卖点_计算方式[31] = '\0';
    } else if (strcmp(field, "买卖点_中枢来源") == 0) {
        c->买卖点_中枢来源[0] = '\0';
        strncpy(c->买卖点_中枢来源, value, 31);
        c->买卖点_中枢来源[31] = '\0';
    } else if (strcmp(field, "线段内部背驰_模式") == 0) {
        c->线段内部背驰_模式[0] = '\0';
        strncpy(c->线段内部背驰_模式, value, 31);
        c->线段内部背驰_模式[31] = '\0';
    } else if (strcmp(field, "标识") == 0) {
        c->标识[0] = '\0';
        strncpy(c->标识, value, 63);
        c->标识[63] = '\0';
    } else if (strcmp(field, "加载文件路径") == 0) {
        c->加载文件路径[0] = '\0';
        strncpy(c->加载文件路径, value, 255);
        c->加载文件路径[255] = '\0';
    } else return false;
    return true;
}

static const char *Py_配置_获取_string字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "手动终止") == 0) return c->手动终止;
    else if (strcmp(field, "指标计算方式") == 0) return c->指标计算方式;
    else if (strcmp(field, "买卖点_指标模式") == 0) return c->买卖点_指标模式;
    else if (strcmp(field, "买卖点_计算方式") == 0) return c->买卖点_计算方式;
    else if (strcmp(field, "买卖点_中枢来源") == 0) return c->买卖点_中枢来源;
    else if (strcmp(field, "线段内部背驰_模式") == 0) return c->线段内部背驰_模式;
    else if (strcmp(field, "标识") == 0) return c->标识;
    else if (strcmp(field, "加载文件路径") == 0) return c->加载文件路径;
    else return "";
}

/* -- Config type -- */

static int Py_缠论配置_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    ChanObject *py_对象 = (ChanObject *) self;
    static char *kwnames[] = {"no_push", NULL};
    int no_push = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|p", kwnames, &no_push))
        return -1;

    if (no_push)
        py_对象->ptr = 缠论配置_不推送();
    else
        py_对象->ptr = 缠论配置_新建();

    if (!py_对象->ptr) {
        PyErr_NoMemory();
        return -1;
    }
    py_对象->owns = 1;
    return 0;
}

static PyObject *Py_缠论配置_获取元素(PyObject *self, PyObject *key) {
    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    const char *field = PyUnicode_AsUTF8(key);
    if (!field) return NULL;

    /* Check each category */
    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyBool_FromLong(Py_配置_获取_bool字段(c, field));
    }
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyLong_FromLong(Py_配置_获取int字段(c, field));
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyFloat_FromDouble(Py_配置_获取_double字段(c, field));
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyUnicode_FromString(Py_配置_获取_string字段(c, field));
    }
    PyErr_Format(PyExc_KeyError, "unknown config field: %s", field);
    return NULL;
}

static int Py_缠论配置_设置元素(PyObject *self, PyObject *key, PyObject *value) {
    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    const char *field = PyUnicode_AsUTF8(key);
    if (!field) return -1;

    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            int v = PyObject_IsTrue(value);
            if (v < 0) return -1;
            Py_配置_设置_bool字段(c, field, (bool) v);
            return 0;
        }
    }
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            long v = PyLong_AsLong(value);
            if (v == -1 && PyErr_Occurred()) return -1;
            Py_配置_设置_int字段(c, field, (int) v);
            return 0;
        }
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            double v = PyFloat_AsDouble(value);
            if (v == -1.0 && PyErr_Occurred()) return -1;
            Py_配置_设置_double字段(c, field, v);
            return 0;
        }
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            const char *v = PyUnicode_AsUTF8(value);
            if (!v) return -1;
            Py_配置_设置_string字段(c, field, v);
            return 0;
        }
    }
    PyErr_Format(PyExc_KeyError, "unknown config field: %s", field);
    return -1;
}

static PyMappingMethods ChanConfig_as_mapping = {
    .mp_length = NULL,
    .mp_subscript = (binaryfunc) Py_缠论配置_获取元素,
    .mp_ass_subscript = (objobjargproc) Py_缠论配置_设置元素,
};

static PyObject *Py_缠论配置_获取属性(PyObject *self, PyObject *name) {
    /* Allow attribute-style access for Chinese field names */
    const char *field = PyUnicode_AsUTF8(name);
    if (!field) return NULL;

    /* Check if it's a 有效性 config field */
    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyBool_FromLong(Py_配置_获取_bool字段(c, field));
    }
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyLong_FromLong(Py_配置_获取int字段(c, field));
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyFloat_FromDouble(Py_配置_获取_double字段(c, field));
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0)
            return PyUnicode_FromString(Py_配置_获取_string字段(c, field));
    }
    /* Fall back to standard attribute lookup */
    return PyObject_GenericGetAttr(self, name);
}

static int Py_缠论配置_设置属性(PyObject *self, PyObject *name, PyObject *value) {
    const char *field = PyUnicode_AsUTF8(name);
    if (!field) return -1;
    if (field[0] == '_')
        return PyObject_GenericSetAttr(self, name, value);

    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            int v = PyObject_IsTrue(value);
            if (v < 0) return -1;
            Py_配置_设置_bool字段(c, field, (bool) v);
            return 0;
        }
    }
    /* Try int, double, string field tables... */
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            long v = PyLong_AsLong(value);
            if (v == -1 && PyErr_Occurred()) return -1;
            Py_配置_设置_int字段(c, field, (int) v);
            return 0;
        }
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            double v = PyFloat_AsDouble(value);
            if (v == -1.0 && PyErr_Occurred()) return -1;
            Py_配置_设置_double字段(c, field, v);
            return 0;
        }
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            const char *v = PyUnicode_AsUTF8(value);
            if (!v) return -1;
            Py_配置_设置_string字段(c, field, v);
            return 0;
        }
    }
    return PyObject_GenericSetAttr(self, name, value);
}

static PyObject *Py_缠论配置_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    char 缓冲[64];
    snprintf(缓冲, sizeof(缓冲), "<缠论配置 at %p>", self->ptr);
    return PyUnicode_FromString(缓冲);
}

static PyObject *Py_缠论配置_不推送(PyObject *py_类, PyObject *Py_UNUSED(py_忽略)) {
    void *ptr = 缠论配置_不推送();
    if (!ptr) return PyErr_NoMemory();
    return Py_制作_拥有(&ChanConfig_Type, ptr);
}

static PyObject *Py_缠论配置___dir__(PyObject *self, PyObject *Py_UNUSED(py_忽略)) {
    /* Build a list of all attributes: standard ones + config field names */
    PyObject *list = PyList_New(0);
    if (!list) return NULL;

    /* Standard attributes: methods, getsets, members from the type */
    PyTypeObject *tp = Py_TYPE(self);
    for (PyMethodDef *m = tp->tp_methods; m && m->ml_name; m++)
        PyList_Append(list, PyUnicode_FromString(m->ml_name));
    for (PyGetSetDef *g = tp->tp_getset; g && g->name; g++)
        PyList_Append(list, PyUnicode_FromString(g->name));
    for (PyMemberDef *mb = tp->tp_members; mb && mb->name; mb++)
        PyList_Append(list, PyUnicode_FromString(mb->name));

    /* Config field names from the four category arrays */
    static const char **all_fields[] = {
        CONFIG_BOOL_FIELDS, CONFIG_INT_FIELDS,
        CONFIG_DOUBLE_FIELDS, CONFIG_STRING_FIELDS, NULL
    };
    for (const char ***cat = all_fields; *cat; cat++)
        for (const char **p = *cat; *p; p++)
            PyList_Append(list, PyUnicode_FromString(*p));

    return list;
}

static PyMethodDef ChanConfig_methods[] = {
    {
        "不推送", (PyCFunction) Py_缠论配置_不推送, METH_CLASS | METH_NOARGS,
        "Create a no-push (no broadcast) configuration."
    },
    {
        "__dir__", (PyCFunction) Py_缠论配置___dir__, METH_NOARGS,
        "List all config field names."
    },
    {
        "release", (PyCFunction) Py_Chan对象_解引用, METH_NOARGS,
        "Explicitly release the C config object."
    },
    {
        "ptr", (PyCFunction) Py_Chan对象_获取_ptr, METH_NOARGS,
        "Raw C pointer value."
    },
    {NULL}
};

static PyTypeObject ChanConfig_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.缠论配置",
    .tp_basicsize = sizeof(ChanConfigObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = Py_缠论配置_初始化,
    .tp_repr = (reprfunc) Py_缠论配置_repr,
    .tp_str = Py_通用_str,
    .tp_methods = ChanConfig_methods,
    .tp_getattro = Py_缠论配置_获取属性,
    .tp_setattro = Py_缠论配置_设置属性,
    .tp_as_mapping = &ChanConfig_as_mapping,
    .tp_doc = "缠论分析流水线的配置。\n\n"
    "典型用法：\n"
    "    config = 缠论配置(no_push=True)\n"
    "    config['分析笔'] = True\n"
    "    obs = 观察者.读取数据文件('data.nb', config)",
};

/* ================================================================
 *  观察者 (Observer) type
 * ================================================================ */

typedef struct {
    ChanObject base;
    PyObject *config; /* keep-alive reference to 缠论配置 */
} ObserverObject;

static void Py_观察者_释放内存(ObserverObject *self) {
    if (self->base.owns && self->base.ptr) {
        释放观察者((观察者 *) self->base.ptr);
        self->base.ptr = NULL;
        self->base.owns = 0;
    }
    Py_XDECREF(self->config);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* ---- macro-generated sequence length / at helpers ---- */

#define SEQ_IMPL(name, seq_field) \
    static size_t obs_##name##_len(观察者 *o) { return o->seq_field.长度; } \
    static void  *obs_##name##_at(观察者 *o, size_t i) { return 动态数组_获取(&o->seq_field, i); }

SEQ_IMPL(raw_klines, 普通K线序列)
SEQ_IMPL(chan_klines, 缠论K线序列)
SEQ_IMPL(base_chan_klines, 基础缠K序列)
SEQ_IMPL(fractals, 分型序列)
SEQ_IMPL(strokes, 笔序列)
SEQ_IMPL(stroke_hubs, 笔_中枢序列)
SEQ_IMPL(segments, 线段序列)
SEQ_IMPL(hubs, 中枢序列)
SEQ_IMPL(ext_segments, 扩展线段序列)
SEQ_IMPL(ext_hubs, 扩展中枢序列)
SEQ_IMPL(ext_segments_seg, 扩展线段序列_线段)
SEQ_IMPL(ext_hubs_seg, 扩展中枢序列_线段)
SEQ_IMPL(seg_seg, 线段_线段序列)
SEQ_IMPL(seg_hubs, 线段_中枢序列)
SEQ_IMPL(ext_seg_extseg, 扩展线段序列_扩展线段)
SEQ_IMPL(ext_hubs_extseg, 扩展中枢序列_扩展线段)

#undef SEQ_IMPL

/* ---- sequence view object (supports len / [] / iter) ---- */

/* Sequence type name → py_元素 Python type mapping */
typedef struct {
    const char *seq_name;
    PyTypeObject *item_type;

    size_t (*len_fn)(观察者 *);

    void *(*at_fn)(观察者 *, size_t);
} SeqViewDef;

/* We build the seq view definitions lazily */

typedef struct {
    PyObject_HEAD
    ObserverObject *obs; /* borrowed reference to observer */
    PyTypeObject *item_type;

    size_t (*len_fn)(观察者 *);

    void *(*at_fn)(观察者 *, size_t);

    const char *name;
} ObserverSeqView;

static void Py_序列视图_释放(ObserverSeqView *self) {
    Py_XDECREF(self->obs);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static Py_ssize_t Py_序列视图_长度(ObserverSeqView *self) {
    return (Py_ssize_t) self->len_fn((观察者 *) self->obs->base.ptr);
}

static PyObject *Py_序列视图_元素(ObserverSeqView *self, Py_ssize_t i) {
    size_t n = self->len_fn((观察者 *) self->obs->base.ptr);
    if (i < 0) i += (Py_ssize_t) n;
    if (i < 0 || (size_t) i >= n) {
        PyErr_SetString(PyExc_IndexError, "sequence index py_输出 of range");
        return NULL;
    }
    void *ptr = self->at_fn((观察者 *) self->obs->base.ptr, (size_t) i);
    if (!ptr) {
        PyErr_Format(PyExc_IndexError, "%s[%zd] is NULL", self->name, i);
        return NULL;
    }
    return Py_制作_借用(self->item_type, ptr);
}

static PySequenceMethods SeqView_as_seq = {
    .sq_length = (lenfunc) Py_序列视图_长度,
    .sq_item = (ssizeargfunc) Py_序列视图_元素,
};

static PyTypeObject ObserverSeqView_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core._SequenceView",
    .tp_basicsize = sizeof(ObserverSeqView),
    .tp_dealloc = (destructor) Py_序列视图_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_as_sequence = &SeqView_as_seq,
    .tp_iter = (getiterfunc) PySeqIter_New,
    .tp_doc = "观察者序列数组的惰性视图。",
};

static PyObject *Py_制作_序列视图(ObserverObject *obs, PyTypeObject *item_type,
                            size_t( *len_fn)(观察者 *),
                            void *(*at_fn)(观察者 *, size_t),
                            const char *name) {
    ObserverSeqView *view = (ObserverSeqView *) ObserverSeqView_Type.tp_alloc(
        &ObserverSeqView_Type, 0);
    if (!view) return NULL;
    Py_INCREF(obs);
    view->obs = obs;
    view->item_type = item_type;
    view->len_fn = len_fn;
    view->at_fn = at_fn;
    view->name = name;
    return (PyObject *) view;
}

/* ================================================================
 *  背驰分析 (Divergence Analysis) — matching chan.py 背驰分析
 *  Pure namespace type; all methods are classmethods.
 * ================================================================ */

static bool Py_背驰分析_提取K线(PyObject *py_普K序列, K线 ***py_输出, size_t *输出长度) {
    if (!PyObject_TypeCheck(py_普K序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "普K序列 must be a 动态数组 of K线");
        return false;
    }
    DynArrayObject *da = (DynArrayObject *) py_普K序列;
    *py_输出 = (K线 **) da->arr.数据;
    *输出长度 = da->arr.长度;
    return true;
}

static PyObject *Py_背驰分析_斜率背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", NULL};
    PyObject *进入对象, *离开对象;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象))
        return NULL;
    bool r = 背驰分析_斜率背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_测度背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", NULL};
    PyObject *进入对象, *离开对象;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象))
        return NULL;
    bool r = 背驰分析_测度背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_MACD背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", "普K序列", "方式", NULL};
    PyObject *进入对象, *离开对象, *py_普K序列;
    const char *method = "default";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!|z", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象,
                                     &DynArray_Type, &py_普K序列,
                                     &method))
        return NULL;
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) return NULL;
    bool r = 背驰分析_MACD背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                         (虚线 *) ((ChanObject *) 离开对象)->ptr,
                         klines, k线长度, method);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_全量背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", "普K序列", NULL};
    PyObject *进入对象, *离开对象, *py_普K序列;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象,
                                     &DynArray_Type, &py_普K序列))
        return NULL;
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) return NULL;
    bool r = 背驰分析_全量背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr,
                       klines, k线长度);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_任意背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", "普K序列", NULL};
    PyObject *进入对象, *离开对象, *py_普K序列;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象,
                                     &DynArray_Type, &py_普K序列))
        return NULL;
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) return NULL;
    bool r = 背驰分析_任意背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr,
                       klines, k线长度);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_配置背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", "普K序列", "配置", NULL};
    PyObject *进入对象, *离开对象, *py_普K序列, *py_配置;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象,
                                     &DynArray_Type, &py_普K序列,
                                     &ChanConfig_Type, &py_配置))
        return NULL;
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) return NULL;
    bool r = 背驰分析_配置背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr,
                       klines, k线长度,
                       (缠论配置 *) ((ChanObject *) py_配置)->ptr);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_任选背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", "普K序列", NULL};
    PyObject *进入对象, *离开对象, *py_普K序列;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象,
                                     &DynArray_Type, &py_普K序列))
        return NULL;
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) return NULL;
    bool r = 背驰分析_任选背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr,
                       klines, k线长度);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_背驰模式(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", "普K序列", "配置", "模式", NULL};
    PyObject *进入对象, *离开对象, *py_普K序列, *py_配置;
    const char *模式字符串 = "default";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!O!|z", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象,
                                     &DynArray_Type, &py_普K序列,
                                     &ChanConfig_Type, &py_配置,
                                     &模式字符串))
        return NULL;
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) return NULL;
    bool r = 背驰分析_背驰模式((虚线 *) ((ChanObject *) 进入对象)->ptr,
                       (虚线 *) ((ChanObject *) 离开对象)->ptr,
                       klines, k线长度,
                       (缠论配置 *) ((ChanObject *) py_配置)->ptr,
                       模式字符串);
    return PyBool_FromLong(r);
}

static PyMethodDef Divergence_methods[] = {
    {
        "MACD背驰", (PyCFunction) Py_背驰分析_MACD背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.MACD背驰(进入段, 离开段, 普K序列, 方式='default')"
    },
    {
        "斜率背驰", (PyCFunction) Py_背驰分析_斜率背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.斜率背驰(进入段, 离开段)"
    },
    {
        "测度背驰", (PyCFunction) Py_背驰分析_测度背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.测度背驰(进入段, 离开段)"
    },
    {
        "全量背驰", (PyCFunction) Py_背驰分析_全量背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.全量背驰(进入段, 离开段, 普K序列)"
    },
    {
        "任意背驰", (PyCFunction) Py_背驰分析_任意背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.任意背驰(进入段, 离开段, 普K序列)"
    },
    {
        "配置背驰", (PyCFunction) Py_背驰分析_配置背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.配置背驰(进入段, 离开段, 普K序列, 配置)"
    },
    {
        "任选背驰", (PyCFunction) Py_背驰分析_任选背驰,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.任选背驰(进入段, 离开段, 普K序列)"
    },
    {
        "背驰模式", (PyCFunction) Py_背驰分析_背驰模式,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "背驰分析.背驰模式(进入段, 离开段, 普K序列, 配置, 模式='default')"
    },
    {NULL}
};

static PyObject *Py_背驰分析_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
    PyErr_SetString(PyExc_TypeError,
                    "背驰分析 cannot be instantiated; use classmethods like "
                    "背驰分析.斜率背驰(进入段, 离开段)");
    return NULL;
}

static PyTypeObject 背驰分析_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.背驰分析",
    .tp_basicsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = Py_背驰分析_new,
    .tp_methods = Divergence_methods,
    .tp_doc = "背驰分析 — 静态方法容器，与 chan.py 背驰分析 对齐。"
    "所有方法均为类方法。",
};

/* ---- Observer init / methods ---- */

static int Py_观察者_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    ObserverObject *obs = (ObserverObject *) self;
    static char *kwnames[] = {"符号", "周期", "配置", NULL};
    const char *symbol;
    int period;
    PyObject *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "siO", kwnames,
                                     &symbol, &period, &py_配置))
        return -1;

    if (!PyObject_TypeCheck(py_配置, &ChanConfig_Type)) {
        PyErr_SetString(PyExc_TypeError, "配置 must be a 缠论配置 instance");
        return -1;
    }

    缠论配置 *config = (缠论配置 *) ((ChanObject *) py_配置)->ptr;
    void *ptr = 观察者_新建(symbol, period, config);
    if (!ptr) {
        PyErr_NoMemory();
        return -1;
    }
    obs->base.ptr = ptr;
    obs->base.owns = 1;
    Py_INCREF(py_配置);
    obs->config = py_配置;
    return 0;
}

static PyObject *Py_观察者_增加原始K线(ObserverObject *self, PyObject *py_参数) {
    void *kptr = Py_解包(py_参数, &KLine_Type);
    if (!kptr) return NULL;
    观察者_增加原始K线((观察者 *) self->base.ptr, (K线 *) kptr);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_读取数据文件(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"文件路径", "配置", NULL};
    const char *path;
    PyObject *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "sO", kwnames,
                                     &path, &py_配置))
        return NULL;

    if (!PyObject_TypeCheck(py_配置, &ChanConfig_Type)) {
        PyErr_SetString(PyExc_TypeError, "配置 must be a 缠论配置 instance");
        return NULL;
    }

    缠论配置 *config = (缠论配置 *) ((ChanObject *) py_配置)->ptr;
    void *ptr = 观察者_读取数据文件(path, config);
    if (!ptr) {
        PyErr_Format(PyExc_FileNotFoundError, "Failed to read: %s", path);
        return NULL;
    }

    ObserverObject *obs = (ObserverObject *) Observer_Type.tp_alloc(&Observer_Type, 0);
    if (!obs) {
        释放观察者((观察者 *) ptr);
        return NULL;
    }
    obs->base.ptr = ptr;
    obs->base.owns = 1;
    Py_INCREF(py_配置);
    obs->config = py_配置;
    return (PyObject *) obs;
}

static PyObject *Py_观察者_释放方法(ObserverObject *self, PyObject *Py_UNUSED(py_忽略)) {
    if (self->base.owns && self->base.ptr) {
        释放观察者((观察者 *) self->base.ptr);
        self->base.ptr = NULL;
        self->base.owns = 0;
    }
    Py_RETURN_NONE;
}

/* Context manager: __enter__ */
static PyObject *Py_观察者_进入(ObserverObject *self, PyObject *Py_UNUSED(py_忽略)) {
    Py_INCREF(self);
    return (PyObject *) self;
}

/* Context manager: __exit__ */
static PyObject *Py_观察者_退出(ObserverObject *self, PyObject *Py_UNUSED(args)) {
    if (self->base.owns && self->base.ptr) {
        释放观察者((观察者 *) self->base.ptr);
        self->base.ptr = NULL;
        self->base.owns = 0;
    }
    Py_RETURN_NONE;
}

/* ---- Observer sequence properties ---- */

#define OBS_SEQ_GETTER(pyname, cname, item_type) \
    static PyObject *Py_观察者_获取_##cname(ObserverObject *self, void *c) { \
        return Py_制作_序列视图(self, item_type, \
                             obs_##cname##_len, obs_##cname##_at, #cname); \
    }

OBS_SEQ_GETTER(raw_klines, raw_klines, &KLine_Type)
OBS_SEQ_GETTER(chan_klines, chan_klines, &ChanKLine_Type)
OBS_SEQ_GETTER(base_chan_klines, base_chan_klines, &ChanKLine_Type)
OBS_SEQ_GETTER(fractals, fractals, &Fractal_Type)
OBS_SEQ_GETTER(strokes, strokes, &Stroke_Type)
OBS_SEQ_GETTER(stroke_hubs, stroke_hubs, &Hub_Type)
OBS_SEQ_GETTER(segments, segments, &Segment_Type)
OBS_SEQ_GETTER(hubs, hubs, &Hub_Type)
OBS_SEQ_GETTER(ext_segments, ext_segments, &Segment_Type)
OBS_SEQ_GETTER(ext_hubs, ext_hubs, &Hub_Type)
OBS_SEQ_GETTER(ext_segments_seg, ext_segments_seg, &Segment_Type)
OBS_SEQ_GETTER(ext_hubs_seg, ext_hubs_seg, &Hub_Type)
OBS_SEQ_GETTER(seg_seg, seg_seg, &Segment_Type)
OBS_SEQ_GETTER(seg_hubs, seg_hubs, &Hub_Type)
OBS_SEQ_GETTER(ext_seg_extseg, ext_seg_extseg, &Segment_Type)
OBS_SEQ_GETTER(ext_hubs_extseg, ext_hubs_extseg, &Hub_Type)

#undef OBS_SEQ_GETTER

static PyGetSetDef Observer_getset[] = {
    {"普通K线序列", (getter) Py_观察者_获取_raw_klines, NULL, NULL, NULL},
    {"缠论K线序列", (getter) Py_观察者_获取_chan_klines, NULL, NULL, NULL},
    {"基础缠K序列", (getter) Py_观察者_获取_base_chan_klines, NULL, NULL, NULL},
    {"分型序列", (getter) Py_观察者_获取_fractals, NULL, NULL, NULL},
    {"笔序列", (getter) Py_观察者_获取_strokes, NULL, NULL, NULL},
    {"笔_中枢序列", (getter) Py_观察者_获取_stroke_hubs, NULL, NULL, NULL},
    {"线段序列", (getter) Py_观察者_获取_segments, NULL, NULL, NULL},
    {"中枢序列", (getter) Py_观察者_获取_hubs, NULL, NULL, NULL},
    {"扩展线段序列", (getter) Py_观察者_获取_ext_segments, NULL, NULL, NULL},
    {"扩展中枢序列", (getter) Py_观察者_获取_ext_hubs, NULL, NULL, NULL},
    {"扩展线段序列_线段", (getter) Py_观察者_获取_ext_segments_seg, NULL, NULL, NULL},
    {"扩展中枢序列_线段", (getter) Py_观察者_获取_ext_hubs_seg, NULL, NULL, NULL},
    {"线段_线段序列", (getter) Py_观察者_获取_seg_seg, NULL, NULL, NULL},
    {"线段_中枢序列", (getter) Py_观察者_获取_seg_hubs, NULL, NULL, NULL},
    {"扩展线段序列_扩展线段", (getter) Py_观察者_获取_ext_seg_extseg, NULL, NULL, NULL},
    {"扩展中枢序列_扩展线段", (getter) Py_观察者_获取_ext_hubs_extseg, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_观察者_repr(ObserverObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) return custom;
    if (!self->base.ptr)
        return PyUnicode_FromString("<观察者 (released)>");
    char 缓冲[64];
    snprintf(缓冲, sizeof(缓冲), "<观察者 at %p>", self->base.ptr);
    return PyUnicode_FromString(缓冲);
}

/* ---- Observer analysis methods ---- */

static PyObject *Py_观察者_笔分析(ObserverObject *self, PyObject *py_配置) {
    void *配置指针 = Py_解包(py_配置, &ChanConfig_Type);
    if (!配置指针) return NULL;
    笔_分析(NULL, &((观察者 *) self->base.ptr)->分型序列,
         &((观察者 *) self->base.ptr)->笔序列,
         &((观察者 *) self->base.ptr)->缠论K线序列,
         &((观察者 *) self->base.ptr)->普通K线序列,
         (缠论配置 *) 配置指针);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_线段分析(ObserverObject *self, PyObject *py_配置) {
    void *配置指针 = Py_解包(py_配置, &ChanConfig_Type);
    if (!配置指针) return NULL;
    线段_分析(&((观察者 *) self->base.ptr)->笔序列,
          &((观察者 *) self->base.ptr)->线段序列,
          (缠论配置 *) 配置指针);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_扩展线段分析(ObserverObject *self, PyObject *py_配置) {
    void *配置指针 = Py_解包(py_配置, &ChanConfig_Type);
    if (!配置指针) return NULL;
    线段_扩展分析(&((观察者 *) self->base.ptr)->线段序列,
            &((观察者 *) self->base.ptr)->扩展线段序列,
            (缠论配置 *) 配置指针);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_笔中枢分析(ObserverObject *self, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"跳过首部", "标识", NULL};
    int 跳过首个 = 0;
    const char *id = "stroke_hub";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|ps", kwnames,
                                     &跳过首个, &id))
        return NULL;
    中枢_分析(&((观察者 *) self->base.ptr)->笔序列,
          &((观察者 *) self->base.ptr)->笔_中枢序列,
          (bool) 跳过首个, id);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_线段中枢分析(ObserverObject *self, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"跳过首部", "标识", NULL};
    int 跳过首个 = 0;
    const char *id = "seg_hub";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|ps", kwnames,
                                     &跳过首个, &id))
        return NULL;
    中枢_分析(&((观察者 *) self->base.ptr)->线段序列,
          &((观察者 *) self->base.ptr)->中枢序列,
          (bool) 跳过首个, id);
    Py_RETURN_NONE;
}

/* ---- 笔 utilities operating on observer sequences ---- */

static PyObject *Py_观察者_获取缠K数量(ObserverObject *self, PyObject *py_配置) {
    void *配置指针 = Py_解包(py_配置, &ChanConfig_Type);
    if (!配置指针) return NULL;
    int n = 笔_获取缠K数量(
        &((观察者 *) self->base.ptr)->缠论K线序列,
        &((观察者 *) self->base.ptr)->笔序列,
        (缠论配置 *) 配置指针);
    return PyLong_FromLong(n);
}

static PyObject *Py_观察者_次高(ObserverObject *self, PyObject *py_参数) {
    int 相同终点 = PyObject_IsTrue(py_参数);
    if (相同终点 < 0) return NULL;
    缠论K线 *ck = 笔_次高(&((观察者 *) self->base.ptr)->缠论K线序列, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_观察者_次低(ObserverObject *self, PyObject *py_参数) {
    int 相同终点 = PyObject_IsTrue(py_参数);
    if (相同终点 < 0) return NULL;
    缠论K线 *ck = 笔_次低(&((观察者 *) self->base.ptr)->缠论K线序列, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_观察者_实际高点(ObserverObject *self, PyObject *py_参数) {
    int 相同终点 = PyObject_IsTrue(py_参数);
    if (相同终点 < 0) return NULL;
    缠论K线 *ck = 笔_实际高点(&((观察者 *) self->base.ptr)->缠论K线序列, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_观察者_实际低点(ObserverObject *self, PyObject *py_参数) {
    int 相同终点 = PyObject_IsTrue(py_参数);
    if (相同终点 < 0) return NULL;
    缠论K线 *ck = 笔_实际低点(&((观察者 *) self->base.ptr)->缠论K线序列, (bool) 相同终点);
    if (!ck) Py_RETURN_NONE;
    return Py_制作_借用(&ChanKLine_Type, ck);
}

/* ---- 笔 lookup methods ---- */

static PyObject *Py_观察者_以文会友(ObserverObject *self, PyObject *分型_obj) {
    void *f = Py_解包(分型_obj, &Fractal_Type);
    if (!f) return NULL;
    虚线 *s = 笔_以文会友(&((观察者 *) self->base.ptr)->笔序列, (分型 *) f);
    if (!s) Py_RETURN_NONE;
    return Py_制作_借用(&Stroke_Type, s);
}

static PyObject *Py_观察者_以武会友(ObserverObject *self, PyObject *分型_obj) {
    void *f = Py_解包(分型_obj, &Fractal_Type);
    if (!f) return NULL;
    虚线 *s = 笔_以武会友(&((观察者 *) self->base.ptr)->笔序列, (分型 *) f);
    if (!s) Py_RETURN_NONE;
    return Py_制作_借用(&Stroke_Type, s);
}

static PyObject *Py_观察者_根据缠K找笔(ObserverObject *self, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"缠K", "偏移", NULL};
    PyObject *py_缠K;
    int 偏移量 = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!|i", kwnames,
                                     &ChanKLine_Type, &py_缠K, &偏移量))
        return NULL;
    虚线 *s = 笔_根据缠K找笔(&((观察者 *) self->base.ptr)->笔序列,
                     (缠论K线 *) ((ChanObject *) py_缠K)->ptr, 偏移量);
    if (!s) Py_RETURN_NONE;
    return Py_制作_借用(&Stroke_Type, s);
}

/* ---- 背驰 (Divergence) analysis ---- */

static int Py_提取_普K数组(观察者 *o, void *虚线_ptr,
                      K线 ***py_输出, size_t *输出长度) {
    虚线_获取普K序列((虚线 *) 虚线_ptr, o, py_输出, 输出长度);
    return *py_输出 != NULL;
}

static PyObject *Py_观察者_背驰通用(
    ObserverObject *self, PyObject *args, PyObject *kw,
    int mode) /* 0=slope, 1=measure, 2=macd, 3=full, 4=any, 5=config, 6=optional, 7=模式字符串 */
{
    static char *kwnames[] = {"进入段", "离开段", "方式", "配置", "模式", NULL};
    PyObject *进入对象, *离开对象;
    const char *method = NULL;
    PyObject *py_配置 = NULL;
    const char *模式字符串 = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|zOz", kwnames,
                                     &DashLine_Type, &进入对象, &DashLine_Type, &离开对象,
                                     &method, &py_配置, &模式字符串))
        return NULL;

    虚线 *enter = (虚线 *) ((ChanObject *) 进入对象)->ptr;
    虚线 *leave = (虚线 *) ((ChanObject *) 离开对象)->ptr;
    观察者 *o = (观察者 *) self->base.ptr;

    /* Extract K-line arrays for both dashes */
    K线 **enter_k = NULL, **leave_k = NULL;
    size_t 进入长度 = 0, 离开长度 = 0;
    K线 **combined = NULL;

    if (mode >= 2) {
        虚线_获取普K序列(enter, o, &enter_k, &进入长度);
        虚线_获取普K序列(leave, o, &leave_k, &离开长度);
        size_t total = 进入长度 + 离开长度;
        combined = (K线 **) malloc(total * sizeof(K线 *));
        if (!combined) return PyErr_NoMemory();
        memcpy(combined, enter_k, 进入长度 * sizeof(K线 *));
        memcpy(combined + 进入长度, leave_k, 离开长度 * sizeof(K线 *));
    }

    bool py_结果 = false;
    switch (mode) {
        case 0: py_结果 = 背驰分析_斜率背驰(enter, leave);
            break;
        case 1: py_结果 = 背驰分析_测度背驰(enter, leave);
            break;
        case 2:
            py_结果 = 背驰分析_MACD背驰(enter, leave, combined,
                                进入长度 + 离开长度, method ? method : "default");
            break;
        case 3:
            py_结果 = 背驰分析_全量背驰(enter, leave, combined,
                              进入长度 + 离开长度);
            break;
        case 4:
            py_结果 = 背驰分析_任意背驰(enter, leave, combined,
                              进入长度 + 离开长度);
            break;
        case 5: {
            void *配置 = 配置 ? ((ChanObject *) py_配置)->ptr : NULL;
            py_结果 = 背驰分析_配置背驰(enter, leave, combined,
                              进入长度 + 离开长度, (缠论配置 *) 配置);
            break;
        }
        case 6:
            py_结果 = 背驰分析_任选背驰(enter, leave, combined,
                              进入长度 + 离开长度);
            break;
        case 7: {
            void *配置 = 配置 ? ((ChanObject *) py_配置)->ptr : NULL;
            py_结果 = 背驰分析_背驰模式(enter, leave, combined,
                              进入长度 + 离开长度, (缠论配置 *) 配置,
                              模式字符串 ? 模式字符串 : "default");
            break;
        }
    }

    free(combined);
    return PyBool_FromLong(py_结果);
}

#define OBS_DIVERGENCE_METHOD(cname, cmode) \
    static PyObject *obs_divergence_##cname(ObserverObject *self, PyObject *args, PyObject *kw) { \
        return Py_观察者_背驰通用(self, args, kw, cmode); \
    }

OBS_DIVERGENCE_METHOD(slope, 0)
OBS_DIVERGENCE_METHOD(measure, 1)
OBS_DIVERGENCE_METHOD(macd, 2)
OBS_DIVERGENCE_METHOD(full, 3)
OBS_DIVERGENCE_METHOD(any, 4)
OBS_DIVERGENCE_METHOD(config, 5)
OBS_DIVERGENCE_METHOD(optional, 6)
OBS_DIVERGENCE_METHOD(mode, 7)

#undef OBS_DIVERGENCE_METHOD

static PyMethodDef Observer_methods[] = {
    {
        "增加原始K线", (PyCFunction) Py_观察者_增加原始K线, METH_O,
        "Feed a single raw K-line into the incremental analysis pipeline."
    },
    {
        "读取数据文件", (PyCFunction) Py_观察者_读取数据文件,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "Load a .nb data file and run the full analysis pipeline."
    },
    {
        "release", (PyCFunction) Py_观察者_释放方法, METH_NOARGS,
        "Explicitly release the C observer and all its managed objects."
    },
    {
        "__enter__", (PyCFunction) Py_观察者_进入, METH_NOARGS,
        "Context manager entry."
    },
    {
        "__exit__", (PyCFunction) Py_观察者_退出, METH_VARARGS,
        "Context manager exit — releases the C observer."
    },

    /* Pipeline analysis */
    {
        "笔_分析", (PyCFunction) Py_观察者_笔分析, METH_O,
        "Re-run stroke analysis on the observer's sequences."
    },
    {
        "线段_分析", (PyCFunction) Py_观察者_线段分析, METH_O,
        "Re-run segment analysis on the observer's sequences."
    },
    {
        "扩展线段_分析", (PyCFunction) Py_观察者_扩展线段分析, METH_O,
        "Re-run extended segment analysis."
    },
    {
        "笔_中枢分析", (PyCFunction) Py_观察者_笔中枢分析,
        METH_VARARGS | METH_KEYWORDS,
        "Re-run stroke hub analysis."
    },
    {
        "线段_中枢分析", (PyCFunction) Py_观察者_线段中枢分析,
        METH_VARARGS | METH_KEYWORDS,
        "Re-run segment hub analysis."
    },

    /* 笔 utilities */
    {
        "获取缠K数量", (PyCFunction) Py_观察者_获取缠K数量, METH_O,
        "Count Chan K-lines in strokes."
    },
    {
        "次高", (PyCFunction) Py_观察者_次高, METH_O,
        "Find the second highest Chan K-line."
    },
    {
        "次低", (PyCFunction) Py_观察者_次低, METH_O,
        "Find the second lowest Chan K-line."
    },
    {
        "实际高点", (PyCFunction) Py_观察者_实际高点, METH_O,
        "Find the actual highest Chan K-line."
    },
    {
        "实际低点", (PyCFunction) Py_观察者_实际低点, METH_O,
        "Find the actual lowest Chan K-line."
    },

    /* 笔 lookup */
    {
        "以文会友", (PyCFunction) Py_观察者_以文会友, METH_O,
        "Find stroke by its civil (文) fractal."
    },
    {
        "以武会友", (PyCFunction) Py_观察者_以武会友, METH_O,
        "Find stroke by its martial (武) fractal."
    },
    {
        "根据缠K找笔", (PyCFunction) Py_观察者_根据缠K找笔,
        METH_VARARGS | METH_KEYWORDS,
        "Find stroke by a Chan K-line and 偏移量."
    },

    /* 背驰 analysis */
    {
        "背驰_斜率", (PyCFunction) obs_divergence_slope,
        METH_VARARGS | METH_KEYWORDS,
        "Check slope divergence between two dashes."
    },
    {
        "背驰_测度", (PyCFunction) obs_divergence_measure,
        METH_VARARGS | METH_KEYWORDS,
        "Check measure divergence between two dashes."
    },
    {
        "背驰_MACD", (PyCFunction) obs_divergence_macd,
        METH_VARARGS | METH_KEYWORDS,
        "Check MACD divergence between two dashes."
    },
    {
        "背驰_全量", (PyCFunction) obs_divergence_full,
        METH_VARARGS | METH_KEYWORDS,
        "Check all divergence methods."
    },
    {
        "背驰_任意", (PyCFunction) obs_divergence_any,
        METH_VARARGS | METH_KEYWORDS,
        "Check any divergence."
    },
    {
        "背驰_配置", (PyCFunction) obs_divergence_config,
        METH_VARARGS | METH_KEYWORDS,
        "Check divergence with config-driven settings."
    },
    {
        "背驰_任选", (PyCFunction) obs_divergence_optional,
        METH_VARARGS | METH_KEYWORDS,
        "Check divergence with optional settings."
    },
    {
        "背驰_模式", (PyCFunction) obs_divergence_mode,
        METH_VARARGS | METH_KEYWORDS,
        "Check divergence with a specific mode string."
    },

    {NULL}
};

static PyTypeObject Observer_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.观察者",
    .tp_basicsize = sizeof(ObserverObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_观察者_释放内存,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = Py_观察者_初始化,
    .tp_getset = Observer_getset,
    .tp_methods = Observer_methods,
    .tp_repr = (reprfunc) Py_观察者_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "顶层协调器。持有所有序列数组，"
    "驱动缠论分析流水线。",
};

/* ================================================================
 *  Module-level utility functions (called from Python enums.py)
 * ================================================================ */

static PyObject *Py_相对方向_分析(PyObject *m, PyObject *args) {
    double 前高, 前低, 后高, 后低;
    if (!PyArg_ParseTuple(args, "dddd", &前高, &前低, &后高, &后低))
        return NULL;
    return PyLong_FromLong(相对方向_分析(前高, 前低, 后高, 后低));
}

static PyObject *Py_相对方向_翻转(PyObject *m, PyObject *py_参数) {
    int 方向值 = PyLong_AsLong(py_参数);
    if (方向值 == -1 && PyErr_Occurred()) return NULL;
    return PyLong_FromLong(相对方向_翻转((相对方向) 方向值));
}

static PyObject *Py_分型结构_分析(PyObject *m, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"左", "中", "右", "可以逆序包含", "忽视顺序包含", NULL};
    PyObject *左_obj, *中_obj, *右_obj;
    int 可以逆序包含 = 0, 忽视顺序包含 = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OOO|pp", kwnames,
                                     &左_obj, &中_obj, &右_obj,
                                     &可以逆序包含, &忽视顺序包含))
        return NULL;

    /* Extract 高/低 from arbitrary objects (works with 缠论K线, K线, 缺口, etc.) */
    double 数值组[6]; /* 左高,左低, 中高,中低, 右高,右低 */
    PyObject *objs[3] = {左_obj, 中_obj, 右_obj};
    for (int i = 0; i < 3; i++) {
        PyObject *h = PyObject_GetAttrString(objs[i], "高");
        PyObject *l = PyObject_GetAttrString(objs[i], "低");
        if (!h || !l) {
            Py_XDECREF(h);
            Py_XDECREF(l);
            PyErr_SetString(PyExc_TypeError,
                            "Arguments 左/中/右 must have 高 and 低 attributes");
            return NULL;
        }
        数值组[i * 2] = PyFloat_AsDouble(h);
        数值组[i * 2 + 1] = PyFloat_AsDouble(l);
        Py_DECREF(h);
        Py_DECREF(l);
        if (PyErr_Occurred()) return NULL;
    }

    /* Create temporary 缺口 structs on the stack (use designated
       initializers: 缺口 has 对象头 then 高/低) */
    缺口 g0 = {.高 = 数值组[0], .低 = 数值组[1]};
    缺口 g1 = {.高 = 数值组[2], .低 = 数值组[3]};
    缺口 g2 = {.高 = 数值组[4], .低 = 数值组[5]};

    分型结构 r = 分型结构_分析(&g0, &g1, &g2,
                     (bool) 可以逆序包含, (bool) 忽视顺序包含);
    return PyLong_FromLong(r);
}

/* ---- 释放全局内存池 (manual pool cleanup) ---- */
static PyObject *Py_释放全局内存池(PyObject *Py_UNUSED(m), PyObject *Py_UNUSED(args)) {
    if (释放全局内存池()) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

static PyMethodDef _core_functions[] = {
    {
        "_rel_dir_analyze", Py_相对方向_分析, METH_VARARGS,
        "Analyze relative direction of two price ranges."
    },
    {
        "_rel_dir_flip", Py_相对方向_翻转, METH_O,
        "Flip a relative direction (up<->down, etc.)."
    },
    {
        "_frac_struct_analyze", (PyCFunction) Py_分型结构_分析, METH_VARARGS | METH_KEYWORDS,
        "Analyze fractal structure from three gaps."
    },
    {
        "_设置自定义strrepr", Py__设置自定义strrepr, METH_VARARGS,
        "Set a custom __repr__ or __str__ callback on a C type. "
        "Usage: _设置自定义strrepr(类型, '__repr__', callback) or pass None to remove."
    },
    {
        "_释放全局内存池", Py_释放全局内存池, METH_NOARGS,
        "Manually release the global memory pool. "
        "Must only be called after ALL observers have been released."
    },
    {NULL}
};

/* ================================================================
 *  Module definition
 * ================================================================ */

static PyModuleDef _core_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_core",
    .m_doc = "chan2c99 缠论 library — native Python C API extension.\n\n"
    "Provides direct access to the C99 Chan Theory analysis engine.\n"
    "All class and method names use Chinese (matching chan.py).",
    .m_size = -1,
};

PyMODINIT_FUNC PyInit__core(void) {
    PyObject *m;

    /* Finalise all type objects */
    if (PyType_Ready(&ChanObject_Type) < 0) return NULL;
    if (PyType_Ready(&KLine_Type) < 0) return NULL;
    if (PyType_Ready(&ChanKLine_Type) < 0) return NULL;
    if (PyType_Ready(&Gap_Type) < 0) return NULL;
    if (PyType_Ready(&Fractal_Type) < 0) return NULL;
    if (PyType_Ready(&DashLine_Type) < 0) return NULL;
    if (PyType_Ready(&Stroke_Type) < 0) return NULL;
    if (PyType_Ready(&SegFeature_Type) < 0) return NULL;
    if (PyType_Ready(&Segment_Type) < 0) return NULL;
    if (PyType_Ready(&Hub_Type) < 0) return NULL;
    if (PyType_Ready(&DynArray_Type) < 0) return NULL;
    if (PyType_Ready(&ChanConfig_Type) < 0) return NULL;
    if (PyType_Ready(&Observer_Type) < 0) return NULL;
    if (PyType_Ready(&ObserverSeqView_Type) < 0) return NULL;
    if (PyType_Ready(&背驰分析_Type) < 0) return NULL;

    m = PyModule_Create(&_core_module);
    if (!m) return NULL;

    /* Add module-level functions */
    if (PyModule_AddFunctions(m, _core_functions) < 0) return NULL;

    /* Add types to module */
    Py_INCREF(&KLine_Type);
    PyModule_AddObject(m, "K线", (PyObject *) &KLine_Type);

    Py_INCREF(&ChanKLine_Type);
    PyModule_AddObject(m, "缠论K线", (PyObject *) &ChanKLine_Type);

    Py_INCREF(&Gap_Type);
    PyModule_AddObject(m, "缺口", (PyObject *) &Gap_Type);

    Py_INCREF(&Fractal_Type);
    PyModule_AddObject(m, "分型", (PyObject *) &Fractal_Type);

    Py_INCREF(&DashLine_Type);
    PyModule_AddObject(m, "虚线", (PyObject *) &DashLine_Type);

    Py_INCREF(&Stroke_Type);
    PyModule_AddObject(m, "笔", (PyObject *) &Stroke_Type);

    Py_INCREF(&SegFeature_Type);
    PyModule_AddObject(m, "线段特征", (PyObject *) &SegFeature_Type);

    Py_INCREF(&Segment_Type);
    PyModule_AddObject(m, "线段", (PyObject *) &Segment_Type);

    Py_INCREF(&Hub_Type);
    PyModule_AddObject(m, "中枢", (PyObject *) &Hub_Type);

    Py_INCREF(&DynArray_Type);
    PyModule_AddObject(m, "动态数组", (PyObject *) &DynArray_Type);

    Py_INCREF(&ChanConfig_Type);
    PyModule_AddObject(m, "缠论配置", (PyObject *) &ChanConfig_Type);

    Py_INCREF(&Observer_Type);
    PyModule_AddObject(m, "观察者", (PyObject *) &Observer_Type);

    Py_INCREF(&背驰分析_Type);
    PyModule_AddObject(m, "背驰分析", (PyObject *) &背驰分析_Type);

    return m;
}
