/*
 * MIT License — Copyright (c) 2024 YuYuKunKun
 *
 * _core.c — Python C API native extension for the chan2c99 缠论 library.
 *
 * Wraps every C struct as a proper Python type object with direct
 * field access (PyGetSetDef), methods (PyMethodDef), and sequence
 * protocol support.  Python C API 原生扩展，零 FFI 开销。
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
static PyTypeObject MACD_Type;
static PyTypeObject RSI_Type;
static PyTypeObject KDJ_Type;
static PyTypeObject DashLine_Type;
static PyTypeObject Stroke_Type;
static PyTypeObject Segment_Type;
static PyTypeObject SegFeature_Type;
static PyTypeObject Hub_Type;
static PyTypeObject DynArray_Type;
static PyTypeObject ChanConfig_Type;
static PyTypeObject Observer_Type;
static PyTypeObject 背驰分析_Type;
static PyTypeObject KLineSynth_Type;
/* Cached Python 相对方向 enum class — set by _设置相对方向类 */
static PyObject *相对方向_类 = NULL;

/* ================================================================
 *  ChanObject — base type for all C object wrappers
 * ================================================================ */

typedef struct {
    PyObject_HEAD
    void *ptr; /* C object pointer */
    int owns; /* 1 = owned (call 解引用 in tp_dealloc) */
} ChanObject;

/* DynArrayObject — Python wrapper around C 动态数组.
   arr is a pointer: owned (owns_elements=1) or borrowed from observer. */
typedef struct {
    PyObject_HEAD
    动态数组 *arr;
    bool owns_elements;
    PyTypeObject *item_type; /* NULL for untyped, set for typed __getitem__ */
} DynArrayObject;

/* Forward declarations — cross-type classmethod references (used in DashLine method table) */
static PyObject *Py_笔_创建笔(PyObject *py_类, PyObject *args, PyObject *kw);

static PyObject *Py_线段_创建线段(PyObject *py_类, PyObject *py_参数);

static void Py_Chan对象_释放(ChanObject *self) {
    /* 弱引用模型：pool 对象由 释放全局内存池 统一清理，不单独 解引用 */
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *Py_Chan对象_引用(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    if (self->ptr) {
        引用(self->ptr);
    }
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
    if (op == Py_EQ) {
        return PyBool_FromLong(eq);
    }
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
    if (!ptr) {
        Py_RETURN_NONE;
    }
    ChanObject *py_对象 = (ChanObject *) type->tp_alloc(type, 0);
    if (!py_对象) {
        return NULL;
    }
    py_对象->ptr = ptr;
    py_对象->owns = 1;
    return (PyObject *) py_对象;
}

static PyObject *Py_制作_借用(PyTypeObject *type, void *ptr) {
    if (!ptr) {
        Py_RETURN_NONE;
    }
    ChanObject *py_对象 = (ChanObject *) type->tp_alloc(type, 0);
    if (!py_对象) {
        return NULL;
    }
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
    if (custom) {
        return PyObject_CallOneArg(custom, self);
    }
    return NULL;
}

static PyObject *_try_custom_str(PyObject *self) {
    PyObject *custom = PyDict_GetItemString(Py_TYPE(self)->tp_dict, CUSTOM_STR_KEY);
    if (custom) {
        return PyObject_CallOneArg(custom, self);
    }
    return NULL;
}

/* Shared tp_str for all types — checks __str__ callback then falls back to tp_repr */
static PyObject *Py_通用_str(PyObject *self) {
    PyObject *custom = _try_custom_str(self);
    if (custom) {
        return custom;
    }
    reprfunc repr = Py_TYPE(self)->tp_repr;
    if (repr) {
        return repr(self);
    }
    return PyUnicode_FromFormat("<%s at %p>", Py_TYPE(self)->tp_name, self);
}

/* ================================================================
 *  动态数组 (Dynamic Array) type — Python py_列表-like wrapper
 * ================================================================ */

static void Py_动态数组_释放(DynArrayObject *self) {
    if (self->owns_elements && self->arr) {
        弱引用_数组清除(self->arr);
        free(self->arr);
    }
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *Py_动态数组_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
    DynArrayObject *self = (DynArrayObject *) type->tp_alloc(type, 0);
    if (!self) {
        return NULL;
    }
    self->arr = (动态数组 *) calloc(1, sizeof(动态数组));
    if (!self->arr) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    动态数组_初始化(self->arr, 4);
    self->owns_elements = true;
    self->item_type = NULL;
    return (PyObject *) self;
}

static Py_ssize_t Py_动态数组_长度(DynArrayObject *self) {
    return (Py_ssize_t) self->arr->长度;
}

static PyObject *Py_动态数组_获取元素(DynArrayObject *self, Py_ssize_t i) {
    if (i < 0) {
        i += (Py_ssize_t) self->arr->长度;
    }
    if (i < 0 || (size_t) i >= self->arr->长度) {
        PyErr_SetString(PyExc_IndexError, "动态数组 index out of range");
        return NULL;
    }
    void *ptr = 动态数组_获取(self->arr, (size_t) i);
    if (!ptr) {
        Py_RETURN_NONE;
    }
    if (self->item_type) {
        return Py_制作_借用(self->item_type, ptr);
    }
    Py_RETURN_NONE;
}

static PyObject *Py_动态数组_下标(DynArrayObject *self, PyObject *key) {
    Py_ssize_t n = (Py_ssize_t) self->arr->长度;
    if (PyIndex_Check(key)) {
        Py_ssize_t i = PyNumber_AsSsize_t(key, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred()) {
            return NULL;
        }
        if (i < 0) {
            i += n;
        }
        if (i < 0 || i >= n) {
            PyErr_SetString(PyExc_IndexError, "动态数组 index out of range");
            return NULL;
        }
        void *ptr = 动态数组_获取(self->arr, (size_t) i);
        if (!ptr) {
            Py_RETURN_NONE;
        }
        if (self->item_type) {
            return Py_制作_借用(self->item_type, ptr);
        }
        Py_RETURN_NONE;
    }
    if (PySlice_Check(key)) {
        Py_ssize_t start, stop, step;
        if (PySlice_Unpack(key, &start, &stop, &step) < 0) {
            return NULL;
        }
        if (step != 1) {
            PyErr_SetString(PyExc_ValueError, "slice step must be 1");
            return NULL;
        }
        if (start < 0) {
            start += n;
        }
        if (stop < 0) {
            stop += n;
        }
        if (start < 0) {
            start = 0;
        }
        if (stop > n) {
            stop = n;
        }
        if (start >= stop) {
            return PyList_New(0);
        }
        PyObject *list = PyList_New(stop - start);
        if (!list) {
            return NULL;
        }
        for (Py_ssize_t k = start; k < stop; k++) {
            void *ptr = 动态数组_获取(self->arr, (size_t) k);
            PyObject *item = ptr
                             ? (self->item_type ? Py_制作_借用(self->item_type, ptr) : Py_NewRef(Py_None))
                             : Py_NewRef(Py_None);
            PyList_SET_ITEM(list, k - start, item);
        }
        return list;
    }
    PyErr_SetString(PyExc_TypeError, "index must be integer or slice");
    return NULL;
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
    弱引用_数组追加(self->arr, ptr);
    Py_RETURN_NONE;
}

/* pop() — returns owned wrapper, caller must release */
static PyObject *Py_动态数组_弹出(DynArrayObject *self, PyObject *Py_UNUSED(py_忽略)) {
    if (self->arr->长度 == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty 动态数组");
        return NULL;
    }
    void *ptr = 弱引用_数组弹出(self->arr);
    if (!ptr) {
        Py_RETURN_NONE;
    }
    /* Return as an owned ChanObject base wrapper.
       The caller can inspect _chan_type attribute to determine the exact type. */
    return Py_制作_拥有(&ChanObject_Type, ptr);
}

/* pop_typed(type, pytype) — pop and py_包装 with a specific type */
static PyObject *Py_动态数组_弹出类型(DynArrayObject *self, PyObject *args) {
    PyTypeObject *pytype = NULL;
    if (!PyArg_ParseTuple(args, "O!", &PyType_Type, &pytype)) {
        return NULL;
    }
    if (self->arr->长度 == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty 动态数组");
        return NULL;
    }
    void *ptr = 弱引用_数组弹出(self->arr);
    if (!ptr) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(pytype, ptr);
}

/* clear() */
static PyObject *Py_动态数组_清空(DynArrayObject *self, PyObject *Py_UNUSED(py_忽略)) {
    弱引用_数组清除(self->arr);
    动态数组_初始化(self->arr, 4);
    Py_RETURN_NONE;
}

static PyObject *Py_动态数组_index(DynArrayObject *self, PyObject *args) {
    PyObject *value;
    Py_ssize_t start = 0, stop = -1;
    if (!PyArg_ParseTuple(args, "O|nn", &value, &start, &stop)) {
        return NULL;
    }

    Py_ssize_t n = (Py_ssize_t) self->arr->长度;
    if (stop < 0 || stop > n) {
        stop = n;
    }
    if (start < 0) {
        start += n;
    }
    if (start < 0) {
        start = 0;
    }

    for (Py_ssize_t i = start; i < stop; i++) {
        void *ptr = 动态数组_获取(self->arr, (size_t) i);
        if (!ptr) {
            continue;
        }
        PyObject *item = self->item_type
                         ? Py_制作_借用(self->item_type, ptr)
                         : Py_制作_借用(&ChanObject_Type, ptr);
        int cmp = PyObject_RichCompareBool(value, item, Py_EQ);
        Py_DECREF(item);
        if (cmp == 1) {
            return PyLong_FromSsize_t(i);
        }
        if (cmp == -1) {
            return NULL;
        }
    }
    PyErr_Format(PyExc_ValueError, "%R is not in 动态数组", value);
    return NULL;
}

static int Py_动态数组_包含(DynArrayObject *self, PyObject *value) {
    for (size_t i = 0; i < self->arr->长度; i++) {
        void *ptr = 动态数组_获取(self->arr, i);
        if (!ptr) {
            if (value == Py_None) {
                return 1;
            }
            continue;
        }
        PyObject *item = self->item_type
                         ? Py_制作_借用(self->item_type, ptr)
                         : Py_制作_借用(&ChanObject_Type, ptr);
        int cmp = PyObject_RichCompareBool(value, item, Py_EQ);
        Py_DECREF(item);
        if (cmp == 1) {
            return 1;
        }
        if (cmp == -1) {
            return -1;
        }
    }
    return 0;
}

static PySequenceMethods DynArray_as_seq = {
    .sq_length = (lenfunc) Py_动态数组_长度,
    .sq_item = (ssizeargfunc) Py_动态数组_获取元素,
    .sq_contains = (objobjproc) Py_动态数组_包含,
};

static PyMappingMethods DynArray_as_mapping = {
    .mp_length = (lenfunc) Py_动态数组_长度,
    .mp_subscript = (binaryfunc) Py_动态数组_下标,
};

static PyMethodDef DynArray_methods[] = {
    {
        "append", (PyCFunction) Py_动态数组_追加, METH_O,
        "Append a chan object (increments weak reference count)."
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
    {
        "index", (PyCFunction) Py_动态数组_index, METH_VARARGS,
        "index(value, start=0, stop=len) -> int\n\n"
        "返回 value 在数组中首次出现的索引。"
    },
    {NULL}
};

static PyObject *Py_动态数组_repr(DynArrayObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
    char 缓冲[128];
    snprintf(缓冲, sizeof(缓冲), "<动态数组 当前长度=%zu 容量=%zu>",
             self->arr->长度, self->arr->容量);
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
    .tp_as_mapping = &DynArray_as_mapping,
    .tp_methods = DynArray_methods,
    .tp_repr = (reprfunc) Py_动态数组_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "动态数组 — Python 列表风格的 C 动态数组包装。\n\n"
    "通过弱引用计数追踪元素。\n"
    "使用 append() 添加缠论对象（增加弱引用计数）。\n"
    "使用 pop() 移除元素（返回拥有包装）。\n"
    "Supports len() and [] indexing.",
};

/* ================================================================
 *  Enum → name helpers (shared by getters/setters below)
 * ================================================================ */

static const char *相对方向_到名称(相对方向 d) {
    switch (d) {
        case 相对方向_向上:
            return "向上";
        case 相对方向_向下:
            return "向下";
        case 相对方向_向上缺口:
            return "向上缺口";
        case 相对方向_向下缺口:
            return "向下缺口";
        case 相对方向_衔接向上:
            return "衔接向上";
        case 相对方向_衔接向下:
            return "衔接向下";
        case 相对方向_顺:
            return "顺";
        case 相对方向_逆:
            return "逆";
        case 相对方向_同:
            return "同";
        default:
            return "未知";
    }
}

static const char *分型结构_到名称(分型结构 s) {
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
        if (x == -1 && PyErr_Occurred()) {
            return 0;
        }
        *out = (相对方向) x;
        return 1;
    }
    if (PyUnicode_Check(v)) {
        const char *s = PyUnicode_AsUTF8(v);
        if (!s) {
            return 0;
        }
        return 相对方向_从名称(s, out);
    }
    PyErr_SetString(PyExc_TypeError, "方向 must be int or str");
    return 0;
}

static int 解析分型结构(PyObject *v, 分型结构 *out) {
    if (PyLong_Check(v)) {
        long x = PyLong_AsLong(v);
        if (x == -1 && PyErr_Occurred()) {
            return 0;
        }
        *out = (分型结构) x;
        return 1;
    }
    if (PyUnicode_Check(v)) {
        const char *s = PyUnicode_AsUTF8(v);
        if (!s) {
            return 0;
        }
        return 分型结构_从名称(s, out);
    }
    PyErr_SetString(PyExc_TypeError, "分型结构 must be int or str");
    return 0;
}


/* Helper: create a 相对方向 Python enum instance from C int value */
static PyObject *Py_制作_相对方向(int val) {
    if (!相对方向_类) {
        return PyUnicode_FromString(相对方向_到名称((相对方向) val));
    }
    return PyObject_CallFunction(相对方向_类, "i", val);
}

/* _设置相对方向类(类) — cache the Python 相对方向 enum class */
static PyObject *Py__设置相对方向类(PyObject *m, PyObject *args) {
    PyObject *cls;
    if (!PyArg_ParseTuple(args, "O:设置相对方向类", &cls)) {
        return NULL;
    }
    if (!PyType_Check(cls)) {
        PyErr_SetString(PyExc_TypeError, "argument must be a class");
        return NULL;
    }
    Py_XSETREF(相对方向_类, Py_NewRef(cls));
    Py_RETURN_NONE;
}

static PyObject *Py__设置自定义strrepr(PyObject *m, PyObject *args) {
    PyTypeObject *type;
    const char *name;
    PyObject *func;
    if (!PyArg_ParseTuple(args, "OsO:设置自定义strrepr", &type, &name, &func)) {
        return NULL;
    }
    if (strcmp(name, "__repr__") != 0 && strcmp(name, "__str__") != 0) {
        PyErr_SetString(PyExc_ValueError, "name must be '__repr__' or '__str__'");
        return NULL;
    }
    /* Translate API names to internal keys that won't clash with CPython's
     * slot wrapper descriptors in tp_dict. */
    const char *key = (strcmp(name, "__repr__") == 0) ? CUSTOM_REPR_KEY : CUSTOM_STR_KEY;
    if (func == Py_None) {
        int r = PyDict_DelItemString(type->tp_dict, key);
        if (r < 0) {
            PyErr_Clear();    /* 键不存在，忽略 */
        }
    } else if (PyCallable_Check(func)) {
        PyDict_SetItemString(type->tp_dict, key, func);
    } else {
        PyErr_SetString(PyExc_TypeError, "callback must be callable or None");
        return NULL;
    }
    Py_RETURN_NONE;
}


/* ================================================================
 *  K线 classmethods (matching chan.py K线) — use 动态数组
 * ================================================================ */

/* Helper: extract K线** array from 动态数组 or Python list */
static bool Py_K线_提取数组(PyObject *py_序列, K线 ***out, size_t *out_len, bool *need_free) {
    if (PyObject_TypeCheck(py_序列, &DynArray_Type)) {
        DynArrayObject *da = (DynArrayObject *) py_序列;
        *out = (K线 **) da->arr->数据;
        *out_len = da->arr->长度;
        *need_free = false;
        return true;
    }
    if (PyList_Check(py_序列)) {
        *out_len = (size_t) PyList_Size(py_序列);
        *out = (K线 **) malloc(*out_len * sizeof(K线 *));
        if (!*out) {
            return false;
        }
        *need_free = true;
        for (size_t i = 0; i < *out_len; i++) {
            PyObject *item = PyList_GetItem(py_序列, (Py_ssize_t) i);
            if (!PyObject_TypeCheck(item, &KLine_Type)) {
                free(*out);
                PyErr_SetString(PyExc_TypeError, "K线序列 elements must be K线");
                return false;
            }
            (*out)[i] = (K线 *) ((ChanObject *) item)->ptr;
        }
        return true;
    }
    PyErr_SetString(PyExc_TypeError, "K线序列 must be 动态数组 or list of K线");
    return false;
}

/* K线.读取大端字节数组(字节组, 周期=60, 标识='Bar') */
static PyObject *Py_K线_读取大端字节数组(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"字节组", "周期", "标识", NULL};
    const char *字节组;
    Py_ssize_t 字节长度;
    int 周期 = 60;
    const char *标识 = "Bar";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "y#|is", kwnames,
                                     &字节组, &字节长度, &周期, &标识)) {
        return NULL;
    }
    if (字节长度 < 48) {
        PyErr_SetString(PyExc_ValueError, "字节组 too short (need 48 bytes for 6 doubles)");
        return NULL;
    }
    K线 *ptr = K线_读取大端字节数组((const uint8_t *) 字节组, 周期, 标识);
    if (!ptr) {
        return PyErr_NoMemory();
    }
    return Py_制作_拥有(&KLine_Type, ptr);
}

/* K线.保存到DAT文件(路径, K线序列) */
static PyObject *Py_K线_保存到DAT文件(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"路径", "K线序列", NULL};
    const char *路径;
    PyObject *py_序列;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "sO", kwnames, &路径, &py_序列)) {
        return NULL;
    }
    K线 **kline_array = NULL;
    size_t 长度 = 0;
    bool need_free = false;
    if (!Py_K线_提取数组(py_序列, &kline_array, &长度, &need_free)) {
        return NULL;
    }
    K线_保存到DAT文件(路径, kline_array, 长度);
    if (need_free) {
        free(kline_array);
    }
    Py_RETURN_NONE;
}

/* K线.获取MACD(K线序列, 始, 终) -> dict */
static PyObject *Py_K线_获取MACD(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"K线序列", "始", "终", NULL};
    PyObject *py_序列, *py_始, *py_终;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OO!O!", kwnames,
                                     &py_序列,
                                     &KLine_Type, &py_始,
                                     &KLine_Type, &py_终)) {
        return NULL;
    }
    K线 **kline_array = NULL;
    size_t 长度 = 0;
    bool need_free = false;
    if (!Py_K线_提取数组(py_序列, &kline_array, &长度, &need_free)) {
        return NULL;
    }
    K线 *始 = (K线 *) ((ChanObject *) py_始)->ptr;
    K线 *终 = (K线 *) ((ChanObject *) py_终)->ptr;
    double 阳, 阴, 合, 总;
    K线_获取MACD(kline_array, 长度, 始, 终, &阳, &阴, &合, &总);
    if (need_free) {
        free(kline_array);
    }
    return Py_BuildValue("{s:d,s:d,s:d,s:d}", "阳", 阳, "阴", 阴, "合", 合, "总", 总);
}

/* K线.截取(序列, 始, 终) */
static PyObject *Py_K线_截取(PyObject *cls, PyObject *args) {
    PyObject *序列, *始, *终;
    if (!PyArg_ParseTuple(args, "OOO", &序列, &始, &终)) {
        return NULL;
    }
    Py_ssize_t 始_idx = PySequence_Index(序列, 始);
    if (始_idx < 0) {
        return NULL;
    }
    Py_ssize_t 终_idx = PySequence_Index(序列, 终);
    if (终_idx < 0) {
        return NULL;
    }
    return PySequence_GetSlice(序列, 始_idx, 终_idx + 1);
}

/* K线.__bytes__() — pack as 6 big-endian doubles (matching chan.py __bytes__) */
static PyObject *Py_K线_bytes(ChanObject *self) {
    K线 *k = (K线 *) self->ptr;
    double vals[6] = {
        (double) k->时间戳, k->开盘价, k->高, k->低, k->收盘价, k->成交量
    };
    uint8_t buf[48];
    for (int i = 0; i < 6; i++) {
        uint64_t raw;
        memcpy(&raw, &vals[i], 8);
        for (int j = 7; j >= 0; j--) {
            buf[i * 8 + j] = raw & 0xFF;
            raw >>= 8;
        }
    }
    return PyBytes_FromStringAndSize((const char *) buf, 48);
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
    return Py_制作_相对方向((int) K线_方向((K线 *) self->ptr));
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
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->高 = v;
    return 0;
}

static int Py_K线_设置_低(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->低 = v;
    return 0;
}

static int Py_K线_设置_开盘价(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->开盘价 = v;
    return 0;
}

static int Py_K线_设置_收盘价(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->收盘价 = v;
    return 0;
}

static int Py_K线_设置_成交量(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->成交量 = v;
    return 0;
}

static int Py_K线_设置_序号(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_K线_设置_周期(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->周期 = (int) v;
    return 0;
}

static int Py_K线_设置_时间戳(ChanObject *self, PyObject *value, void *c) {
    long long v = PyLong_AsLongLong(value);
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((K线 *) self->ptr)->时间戳 = (time_t) v;
    return 0;
}

static int Py_K线_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) {
        return -1;
    }
    strncpy(((K线 *) self->ptr)->标识, s, 63);
    ((K线 *) self->ptr)->标识[63] = '\0';
    return 0;
}

static PyObject *Py_K线_获取_macd(ChanObject *self, void *c) {
    (void) c;
    平滑异同移动平均线 *m = ((K线 *) self->ptr)->macd;
    return m ? Py_制作_借用(&MACD_Type, m) : Py_NewRef(Py_None);
}

static PyObject *Py_K线_获取_rsi(ChanObject *self, void *c) {
    (void) c;
    相对强弱指数 *r = ((K线 *) self->ptr)->rsi;
    return r ? Py_制作_借用(&RSI_Type, r) : Py_NewRef(Py_None);
}

static PyObject *Py_K线_获取_kdj(ChanObject *self, void *c) {
    (void) c;
    随机指标 *k = ((K线 *) self->ptr)->kdj;
    return k ? Py_制作_借用(&KDJ_Type, k) : Py_NewRef(Py_None);
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
    {"macd", (getter) Py_K线_获取_macd, NULL, "MACD indicator (read-only)", NULL},
    {"rsi", (getter) Py_K线_获取_rsi, NULL, "RSI indicator (read-only)", NULL},
    {"kdj", (getter) Py_K线_获取_kdj, NULL, "KDJ indicator (read-only)", NULL},
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
                                     &id, &ts, &py_开盘, &py_最高, &py_最低, &py_收盘, &py_量, &index, &period)) {
        return NULL;
    }

    void *ptr = K线_创建普K(id, (time_t) ts, py_开盘, py_最高, py_最低, py_收盘, py_量, index, period);
    if (!ptr) {
        return PyErr_NoMemory();
    }
    return Py_制作_拥有(&KLine_Type, ptr);
}

static PyMethodDef KLine_methods[] = {
    {
        "创建普K", (PyCFunction) Py_K线_创建普K,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "K线.创建普K(标识, 时间戳, 开盘价, 高, 低, 收盘价, 成交量, 序号, 周期=0)\n\n"
        "创建一个新的原始K线对象。\n\n"
        "参数:\n"
        "  标识 (str) — 交易对标识\n"
        "  时间戳 (int) — Unix 时间戳\n"
        "  开盘价 (float)\n"
        "  高 (float)\n"
        "  低 (float)\n"
        "  收盘价 (float)\n"
        "  成交量 (float)\n"
        "  序号 (int)\n"
        "  周期 (int) — K线周期，单位秒，可选"
    },
    {
        "读取大端字节数组", (PyCFunction) Py_K线_读取大端字节数组,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "K线.读取大端字节数组(字节组, 周期=60, 标识='Bar')\n\n"
        "从大端序字节数组解析一根K线。\n\n"
        "参数:\n"
        "  字节组 (bytes) — 48 字节大端序数据（6 个 double）\n"
        "  周期 (int) — K线周期，默认 60 秒\n"
        "  标识 (str) — 交易对标识，默认 'Bar'"
    },
    {
        "保存到DAT文件", (PyCFunction) Py_K线_保存到DAT文件,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "K线.保存到DAT文件(路径, K线序列)\n\n"
        "将 K 线序列保存为二进制 .nb 文件（大端序）。\n\n"
        "参数:\n"
        "  路径 (str) — 输出文件路径\n"
        "  K线序列 (动态数组|list[K线]) — 要保存的 K 线"
    },
    {
        "获取MACD", (PyCFunction) Py_K线_获取MACD,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "K线.获取MACD(K线序列, 始, 终) -> dict\n\n"
        "计算指定区间内的 MACD 柱聚合值。\n\n"
        "参数:\n"
        "  K线序列 (动态数组|list[K线]) — 含 MACD 指标的 K 线序列\n"
        "  始 (K线) — 起始 K 线\n"
        "  终 (K线) — 结束 K 线\n\n"
        "返回:\n"
        "  dict — {'阳': float, '阴': float, '合': float, '总': float}"
    },
    {
        "截取", (PyCFunction) Py_K线_截取,
        METH_STATIC | METH_VARARGS,
        "K线.截取(序列, 始, 终) -> list\n\n"
        "截取序列中从 始 到 终（含）的子序列。\n\n"
        "参数:\n"
        "  序列 (list) — K 线列表\n"
        "  始 (K线) — 起始 K 线\n"
        "  终 (K线) — 结束 K 线"
    },
    {
        "__bytes__", (PyCFunction) Py_K线_bytes, METH_NOARGS,
        "bytes(K线) — 转换为 48 字节大端序二进制（与 .nb 格式兼容）。"
    },
    {NULL}
};

static PyObject *Py_K线_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
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
    .tp_doc = "K线 — 原始K线，含 OHLCV 数据和元信息。\n\n"
    "类方法:\n"
    "  K线.创建普K(标识, 时间戳, 开盘价, 高, 低, 收盘价, 成交量, 序号, 周期=0) → K线\n"
    "  K线.读取大端字节数组(字节组, 周期=60, 标识='Bar') → K线\n"
    "  K线.保存到DAT文件(路径, K线序列)\n"
    "  K线.获取MACD(K线序列, 始, 终) → dict\n\n"
    "静态方法:\n"
    "  K线.截取(序列, 始, 终) → list\n\n"
    "属性:\n"
    "  macd — MACD 指标（只读，None 表示未计算）\n"
    "  rsi — RSI 指标（只读，None 表示未计算）\n"
    "  kdj — KDJ 指标（只读，None 表示未计算）",
};


/* ================================================================
 *  缠论K线 classmethods (matching chan.py 缠论K线)
 * ================================================================ */

/* 缠论K线.创建缠K(时间戳, 高, 低, 方向, 结构, 原始序号, 普K, 之前=None) */
static PyObject *Py_缠论K线_创建缠K(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"时间戳", "高", "低", "方向", "结构", "原始序号", "普K", "之前", NULL};
    long long ts;
    double 高, 低;
    PyObject *py_方向, *py_结构;
    int 原始序号;
    PyObject *py_普K, *py_之前 = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "LddOOiO!|O!", kwnames,
                                     &ts, &高, &低, &py_方向, &py_结构,
                                     &原始序号,
                                     &KLine_Type, &py_普K,
                                     &ChanKLine_Type, &py_之前)) {
        return NULL;
    }

    相对方向 方向;
    if (!解析方向(py_方向, &方向)) {
        return NULL;
    }

    分型结构 结构;
    if (!解析分型结构(py_结构, &结构)) {
        return NULL;
    }

    K线 *普K = (K线 *) ((ChanObject *) py_普K)->ptr;
    缠论K线 *之前 = (py_之前 != Py_None) ? (缠论K线 *) ((ChanObject *) py_之前)->ptr : NULL;

    缠论K线 *ptr = 缠论K线_创建缠K((time_t) ts, 高, 低, 方向, 结构, 原始序号, 普K, 之前);
    if (!ptr) {
        return PyErr_NoMemory();
    }
    return Py_制作_拥有(&ChanKLine_Type, ptr);
}

/* 缠论K线.时间戳对齐(基线, K线) -> int */
static PyObject *Py_缠论K线_时间戳对齐(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"基线", "K线", NULL};
    PyObject *py_基线, *py_K线;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OO!", kwnames,
                                     &py_基线,
                                     &ChanKLine_Type, &py_K线)) {
        return NULL;
    }

    缠论K线 *k线 = (缠论K线 *) ((ChanObject *) py_K线)->ptr;

    缠论K线 **基线数组 = NULL;
    size_t 基线长 = 0;
    bool need_free = false;

    if (PyObject_TypeCheck(py_基线, &DynArray_Type)) {
        DynArrayObject *da = (DynArrayObject *) py_基线;
        基线数组 = (缠论K线 **) da->arr->数据;
        基线长 = da->arr->长度;
    } else if (PyList_Check(py_基线)) {
        基线长 = (size_t) PyList_Size(py_基线);
        基线数组 = (缠论K线 **) malloc(基线长 * sizeof(缠论K线 *));
        if (!基线数组) {
            return PyErr_NoMemory();
        }
        need_free = true;
        for (size_t i = 0; i < 基线长; i++) {
            PyObject *item = PyList_GetItem(py_基线, (Py_ssize_t) i);
            if (!PyObject_TypeCheck(item, &ChanKLine_Type)) {
                free(基线数组);
                PyErr_SetString(PyExc_TypeError, "基线 elements must be 缠论K线");
                return NULL;
            }
            基线数组[i] = (缠论K线 *) ((ChanObject *) item)->ptr;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "基线 must be 动态数组 or list of 缠论K线");
        return NULL;
    }

    time_t result = 缠论K线_时间戳对齐(基线数组, 基线长, k线);
    if (need_free) {
        free(基线数组);
    }
    return PyLong_FromLongLong((long long) result);
}

/* 缠论K线.兼并(之前缠K, 当前缠K, 当前普K, 配置) -> (缠论K线|None, str|None) */
static PyObject *Py_缠论K线_兼并(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"之前缠K", "当前缠K", "当前普K", "配置", NULL};
    PyObject *py_之前, *py_当前, *py_普K, *py_配置;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OO!O!O!", kwnames,
                                     &py_之前,
                                     &ChanKLine_Type, &py_当前,
                                     &KLine_Type, &py_普K,
                                     &ChanConfig_Type, &py_配置)) {
        return NULL;
    }

    缠论K线 *之前 = NULL;
    if (py_之前 != Py_None) {
        if (!PyObject_TypeCheck(py_之前, &ChanKLine_Type)) {
            PyErr_SetString(PyExc_TypeError, "之前缠K must be 缠论K线 or None");
            return NULL;
        }
        之前 = (缠论K线 *) ((ChanObject *) py_之前)->ptr;
    }
    缠论K线 *当前 = (缠论K线 *) ((ChanObject *) py_当前)->ptr;
    K线 *普K = (K线 *) ((ChanObject *) py_普K)->ptr;
    缠论配置 *配置 = (缠论配置 *) ((ChanObject *) py_配置)->ptr;

    const char *模式 = NULL;
    缠论K线 *结果 = 缠论K线_兼并(之前, 当前, 普K, 配置, &模式);

    PyObject *py_结果 = 结果 ? Py_制作_借用(&ChanKLine_Type, 结果) : Py_None;
    Py_INCREF(py_结果);
    PyObject *py_模式 = 模式 ? PyUnicode_FromString(模式) : Py_None;
    Py_INCREF(py_模式);

    PyObject *tup = PyTuple_Pack(2, py_结果, py_模式);
    Py_DECREF(py_结果);
    Py_DECREF(py_模式);
    return tup;
}

/* 缠论K线.分析(当前K线, 缠K序列, 普K序列, 配置) -> (str, 分型|None) */
static PyObject *Py_缠论K线_分析(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"当前K线", "缠K序列", "普K序列", "配置", NULL};
    PyObject *py_K线, *py_缠K序列, *py_普K序列, *py_配置;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!O!", kwnames,
                                     &KLine_Type, &py_K线,
                                     &DynArray_Type, &py_缠K序列,
                                     &DynArray_Type, &py_普K序列,
                                     &ChanConfig_Type, &py_配置)) {
        return NULL;
    }

    K线 *k线 = (K线 *) ((ChanObject *) py_K线)->ptr;
    缠论配置 *配置 = (缠论配置 *) ((ChanObject *) py_配置)->ptr;

    const char *状态 = NULL;
    分型 *形态 = NULL;
    缠论K线_分析(k线,
                      ((DynArrayObject *) py_缠K序列)->arr,
                      ((DynArrayObject *) py_普K序列)->arr,
                      配置, &状态, &形态);

    PyObject *py_状态 = 状态 ? PyUnicode_FromString(状态) : Py_None;
    Py_INCREF(py_状态);
    PyObject *py_形态 = 形态 ? Py_制作_借用(&Fractal_Type, 形态) : Py_None;
    Py_INCREF(py_形态);

    PyObject *tup = PyTuple_Pack(2, py_状态, py_形态);
    Py_DECREF(py_状态);
    Py_DECREF(py_形态);
    return tup;
}

/* 缠论K线.截取(序列, 始, 终) — same generic logic as K线.截取 */
static PyObject *Py_缠论K线_截取(PyObject *cls, PyObject *args) {
    PyObject *序列, *始, *终;
    if (!PyArg_ParseTuple(args, "OOO", &序列, &始, &终)) {
        return NULL;
    }
    Py_ssize_t 始_idx = PySequence_Index(序列, 始);
    if (始_idx < 0) {
        return NULL;
    }
    Py_ssize_t 终_idx = PySequence_Index(序列, 终);
    if (终_idx < 0) {
        return NULL;
    }
    return PySequence_GetSlice(序列, 始_idx, 终_idx + 1);
}


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
    return Py_制作_相对方向((int) ((缠论K线 *) self->ptr)->方向);
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
    if (!ref) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&KLine_Type, ref);
}

/* --- 缠论K线 setters --- */
static int Py_缠论K线_设置_序号(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->序号 = (int) x;
    return 0;
}

static int Py_缠论K线_设置_时间戳(ChanObject *self, PyObject *v, void *c) {
    long long x = PyLong_AsLongLong(v);
    if (x == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->时间戳 = (time_t) x;
    return 0;
}

static int Py_缠论K线_设置_高(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->高 = x;
    return 0;
}

static int Py_缠论K线_设置_低(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->低 = x;
    return 0;
}

static int Py_缠论K线_设置_方向(ChanObject *self, PyObject *v, void *c) {
    相对方向 x;
    if (!解析方向(v, &x)) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->方向 = x;
    return 0;
}

static int Py_缠论K线_设置_分型(ChanObject *self, PyObject *v, void *c) {
    分型结构 x;
    if (!解析分型结构(v, &x)) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->分型 = x;
    return 0;
}

static int Py_缠论K线_设置_分型特征值(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->分型特征值 = x;
    return 0;
}

static int Py_缠论K线_设置_周期(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->周期 = (int) x;
    return 0;
}

static int Py_缠论K线_设置_标识(ChanObject *self, PyObject *v, void *c) {
    const char *s = PyUnicode_AsUTF8(v);
    if (!s) {
        return -1;
    }
    strncpy(((缠论K线 *) self->ptr)->标识, s, 63);
    ((缠论K线 *) self->ptr)->标识[63] = '\0';
    return 0;
}

static int Py_缠论K线_设置_原始起始序号(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->原始起始序号 = (int) x;
    return 0;
}

static int Py_缠论K线_设置_原始结束序号(ChanObject *self, PyObject *v, void *c) {
    long x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((缠论K线 *) self->ptr)->原始结束序号 = (int) x;
    return 0;
}

static PyObject *Py_缠论K线_获取_镜像(ChanObject *self, void *c) {
    缠论K线 *mirror = 缠论K线_镜像((缠论K线 *) self->ptr);
    if (!mirror) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&ChanKLine_Type, mirror);
}

static PyObject *Py_缠论K线_获取_与MACD柱子匹配(ChanObject *self, void *c) {
    (void)c;
    return PyBool_FromLong(缠论K线_与MACD柱子匹配((缠论K线 *) self->ptr));
}

static PyObject *Py_缠论K线_获取_与RSI匹配(ChanObject *self, void *c) {
    (void)c;
    return PyBool_FromLong(缠论K线_与RSI匹配((缠论K线 *) self->ptr));
}

static PyObject *Py_缠论K线_获取_与KDJ匹配(ChanObject *self, void *c) {
    (void)c;
    return PyBool_FromLong(缠论K线_与KDJ匹配((缠论K线 *) self->ptr));
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
    {"镜像", (getter) Py_缠论K线_获取_镜像, NULL, "Deep copy of this Chan K-line (read-only)", NULL},
    {"与MACD柱子匹配", (getter) Py_缠论K线_获取_与MACD柱子匹配, NULL, "MACD histogram direction matches fractal type", NULL},
    {"与RSI匹配", (getter) Py_缠论K线_获取_与RSI匹配, NULL, "RSI vs SMA direction matches fractal type", NULL},
    {"与KDJ匹配", (getter) Py_缠论K线_获取_与KDJ匹配, NULL, "KDJ K vs D direction matches fractal type", NULL},
    {NULL}
};

static PyObject *Py_缠论K线_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
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

static PyMethodDef ChanKLine_methods[] = {
    {
        "创建缠K", (PyCFunction) Py_缠论K线_创建缠K,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "缠论K线.创建缠K(时间戳, 高, 低, 方向, 结构, 原始序号, 普K, 之前=None)\n\n"
        "创建一个新的缠论K线。\n\n"
        "参数:\n"
        "  时间戳 (int) — Unix 时间戳\n"
        "  高 (float)\n"
        "  低 (float)\n"
        "  方向 (str|int) — 相对方向，如 '向上'/'向下'\n"
        "  结构 (str|int) — 分型结构，如 '顶'/'底'/'散'\n"
        "  原始序号 (int)\n"
        "  普K (K线) — 原始K线\n"
        "  之前 (缠论K线|None) — 前一根缠论K线，可选"
    },
    {
        "时间戳对齐", (PyCFunction) Py_缠论K线_时间戳对齐,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "缠论K线.时间戳对齐(基线, K线) -> int\n\n"
        "在基线序列中查找与给定 K 线时间戳对齐的时间戳。\n\n"
        "参数:\n"
        "  基线 (动态数组|list[缠论K线]) — 参考缠论K线序列\n"
        "  K线 (缠论K线) — 待对齐的缠论K线"
    },
    {
        "兼并", (PyCFunction) Py_缠论K线_兼并,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "缠论K线.兼并(之前缠K, 当前缠K, 当前普K, 配置) -> (缠论K线|None, str|None)\n\n"
        "尝试合并两根缠论K线，返回 (新缠K, 模式)。模式为 '添加'/'替换'/None。\n\n"
        "参数:\n"
        "  之前缠K (缠论K线|None) — 前一根缠论K线，可为 None\n"
        "  当前缠K (缠论K线) — 当前缠论K线\n"
        "  当前普K (K线) — 当前原始K线\n"
        "  配置 (缠论配置) — 分析配置"
    },
    {
        "分析", (PyCFunction) Py_缠论K线_分析,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "缠论K线.分析(当前K线, 缠K序列, 普K序列, 配置) -> (str, 分型|None)\n\n"
        "处理一根原始K线，更新缠论K线序列。返回 (状态, 分型)。\n\n"
        "参数:\n"
        "  当前K线 (K线) — 当前原始K线\n"
        "  缠K序列 (动态数组) — 缠论K线序列（会被修改）\n"
        "  普K序列 (动态数组) — 原始K线序列（会被修改）\n"
        "  配置 (缠论配置) — 分析配置\n\n"
        "状态: '创建' / '替换' / '兼并' / '新建'"
    },
    {
        "截取", (PyCFunction) Py_缠论K线_截取,
        METH_STATIC | METH_VARARGS,
        "缠论K线.截取(序列, 始, 终) -> list\n\n"
        "截取序列中从 始 到 终（含）的子序列。\n\n"
        "参数:\n"
        "  序列 (动态数组|list[缠论K线]) — 缠论K线序列\n"
        "  始 (缠论K线) — 起始元素\n"
        "  终 (缠论K线) — 结束元素"
    },
    {NULL}
};

static PyTypeObject ChanKLine_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.缠论K线",
    .tp_basicsize = sizeof(ChanKLineObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getset = ChanKLine_getset,
    .tp_methods = ChanKLine_methods,
    .tp_repr = (reprfunc) Py_缠论K线_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "缠论K线 — 由原始K线合并而来，含方向和分型信息。\n\n"
    "类方法:\n"
    "  缠论K线.创建缠K(时间戳, 高, 低, 方向, 结构, 原始序号, 普K, 之前=None) → 缠论K线\n"
    "  缠论K线.时间戳对齐(基线, K线) → int\n"
    "  缠论K线.兼并(之前缠K, 当前缠K, 当前普K, 配置) → (缠论K线|None, str|None)\n"
    "  缠论K线.分析(当前K线, 缠K序列, 普K序列, 配置) → (str, 分型|None)\n"
    "静态方法:\n"
    "  缠论K线.截取(序列, 始, 终) → list\n\n"
    "属性:\n"
    "  镜像 — 深拷贝当前缠论K线\n"
    "  与MACD柱子匹配 — MACD柱方向是否与分型匹配\n"
    "  与RSI匹配 — RSI 与 SMA 的关系是否与分型匹配\n"
    "  与KDJ匹配 — KDJ 的 K/D 关系是否与分型匹配",
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
    if (x == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((缺口 *) self->ptr)->高 = x;
    return 0;
}

static int Py_缺口_设置_低(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((缺口 *) self->ptr)->低 = x;
    return 0;
}

static PyGetSetDef Gap_getset[] = {
    {"高", (getter) Py_缺口_获取_高, (setter) Py_缺口_设置_高, NULL, NULL},
    {"低", (getter) Py_缺口_获取_低, (setter) Py_缺口_设置_低, NULL, NULL},
    {NULL}
};

static int Py_缺口_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"高", "低", NULL};
    double 高, 低;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "dd", kwnames, &高, &低)) {
        return -1;
    }
    GapObject *gobj = (GapObject *) self;
    void *ptr = 缺口_新建(高, 低);
    if (!ptr) {
        PyErr_NoMemory();
        return -1;
    }
    gobj->base.ptr = ptr;
    gobj->base.owns = 1;
    return 0;
}

static PyObject *Py_缺口_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
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
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = Py_缺口_初始化,
    .tp_getset = Gap_getset,
    .tp_repr = (reprfunc) Py_缺口_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "缺口(高, 低) — 两个价格区间之间的跳空。\n\n"
    "参数:\n"
    "  高 (float) — 缺口上沿价格\n"
    "  低 (float) — 缺口下沿价格",
};

/* ================================================================
 *  平滑异同移动平均线 (MACD) type — thin read-only wrapper
 * ================================================================ */

typedef struct {
    ChanObject base;
} MACDObject;

#define MACD_GETTER(name, field) \
    static PyObject *Py_MACD_获取_##name(ChanObject *self, void *c) { \
        (void)c; return PyFloat_FromDouble(((平滑异同移动平均线 *)self->ptr)->field); \
    }

MACD_GETTER(dif, DIF)
MACD_GETTER(dea, DEA)
MACD_GETTER(macd柱, MACD柱)
MACD_GETTER(快线ema, 快线EMA)
MACD_GETTER(慢线ema, 慢线EMA)
MACD_GETTER(dea_ema, DEA_EMA)

static PyObject *Py_MACD_获取_快线周期(ChanObject *self, void *c) {
    (void) c;
    return PyLong_FromLong(((平滑异同移动平均线 *) self->ptr)->快线周期);
}

static PyObject *Py_MACD_获取_慢线周期(ChanObject *self, void *c) {
    (void) c;
    return PyLong_FromLong(((平滑异同移动平均线 *) self->ptr)->慢线周期);
}

static PyObject *Py_MACD_获取_信号周期(ChanObject *self, void *c) {
    (void) c;
    return PyLong_FromLong(((平滑异同移动平均线 *) self->ptr)->信号周期);
}

static PyGetSetDef MACD_getset[] = {
    {"DIF", (getter) Py_MACD_获取_dif, NULL, NULL, NULL},
    {"DEA", (getter) Py_MACD_获取_dea, NULL, NULL, NULL},
    {"MACD柱", (getter) Py_MACD_获取_macd柱, NULL, NULL, NULL},
    {"快线EMA", (getter) Py_MACD_获取_快线ema, NULL, NULL, NULL},
    {"慢线EMA", (getter) Py_MACD_获取_慢线ema, NULL, NULL, NULL},
    {"DEA_EMA", (getter) Py_MACD_获取_dea_ema, NULL, NULL, NULL},
    {"快线周期", (getter) Py_MACD_获取_快线周期, NULL, NULL, NULL},
    {"慢线周期", (getter) Py_MACD_获取_慢线周期, NULL, NULL, NULL},
    {"信号周期", (getter) Py_MACD_获取_信号周期, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_MACD_repr(ChanObject *self) {
    平滑异同移动平均线 *m = (平滑异同移动平均线 *) self->ptr;
    char buf[128];
    snprintf(buf, sizeof(buf), "MACD(DIF=%.6f, DEA=%.6f, MACD柱=%.6f)", m->DIF, m->DEA, m->MACD柱);
    return PyUnicode_FromString(buf);
}

static PyTypeObject MACD_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.MACD",
    .tp_basicsize = sizeof(MACDObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = MACD_getset,
    .tp_repr = (reprfunc) Py_MACD_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "平滑异同移动平均线 (MACD) 指标。只读。",
};

/* ================================================================
 *  相对强弱指数 (RSI) type — thin read-only wrapper
 * ================================================================ */

typedef struct {
    ChanObject base;
} RSIObject;

static PyObject *Py_RSI_获取_RSI(ChanObject *self, void *c) {
    (void) c;
    return PyFloat_FromDouble(((相对强弱指数 *) self->ptr)->RSI);
}

static PyObject *Py_RSI_获取_RSI_SMA(ChanObject *self, void *c) {
    (void) c;
    return PyFloat_FromDouble(((相对强弱指数 *) self->ptr)->RSI_SMA);
}

static PyObject *Py_RSI_获取_周期(ChanObject *self, void *c) {
    (void) c;
    return PyLong_FromLong(((相对强弱指数 *) self->ptr)->周期);
}

static PyGetSetDef RSI_getset[] = {
    {"RSI", (getter) Py_RSI_获取_RSI, NULL, NULL, NULL},
    {"RSI_SMA", (getter) Py_RSI_获取_RSI_SMA, NULL, NULL, NULL},
    {"周期", (getter) Py_RSI_获取_周期, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_RSI_repr(ChanObject *self) {
    相对强弱指数 *r = (相对强弱指数 *) self->ptr;
    char buf[128];
    snprintf(buf, sizeof(buf), "RSI(RSI=%.6f, RSI_SMA=%.6f)", r->RSI, r->RSI_SMA);
    return PyUnicode_FromString(buf);
}

static PyTypeObject RSI_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.RSI",
    .tp_basicsize = sizeof(RSIObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = RSI_getset,
    .tp_repr = (reprfunc) Py_RSI_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "相对强弱指数 (RSI) 指标。只读。",
};

/* ================================================================
 *  随机指标 (KDJ) type — thin read-only wrapper
 * ================================================================ */

typedef struct {
    ChanObject base;
} KDJObject;

static PyObject *Py_KDJ_获取_K(ChanObject *self, void *c) {
    (void) c;
    return PyFloat_FromDouble(((随机指标 *) self->ptr)->K);
}

static PyObject *Py_KDJ_获取_D(ChanObject *self, void *c) {
    (void) c;
    return PyFloat_FromDouble(((随机指标 *) self->ptr)->D);
}

static PyObject *Py_KDJ_获取_J(ChanObject *self, void *c) {
    (void) c;
    return PyFloat_FromDouble(((随机指标 *) self->ptr)->J);
}

static PyGetSetDef KDJ_getset[] = {
    {"K", (getter) Py_KDJ_获取_K, NULL, NULL, NULL},
    {"D", (getter) Py_KDJ_获取_D, NULL, NULL, NULL},
    {"J", (getter) Py_KDJ_获取_J, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_KDJ_repr(ChanObject *self) {
    随机指标 *k = (随机指标 *) self->ptr;
    char buf[128];
    snprintf(buf, sizeof(buf), "KDJ(K=%.6f, D=%.6f, J=%.6f)", k->K, k->D, k->J);
    return PyUnicode_FromString(buf);
}

static PyTypeObject KDJ_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.KDJ",
    .tp_basicsize = sizeof(KDJObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = KDJ_getset,
    .tp_repr = (reprfunc) Py_KDJ_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "随机指标 (KDJ) 指标。只读。",
};

/* ================================================================
 *  分型 class/static methods (matching chan.py 分型)
 * ================================================================ */

static PyObject *Py_分型_判断分型(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"左", "右", "模式", NULL};
    PyObject *py_左, *py_右;
    const char *模式 = "中";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|s", kwnames,
                                     &Fractal_Type, &py_左, &Fractal_Type, &py_右, &模式)) {
        return NULL;
    }
    分型 *左 = (分型 *) ((ChanObject *) py_左)->ptr;
    分型 *右 = (分型 *) ((ChanObject *) py_右)->ptr;
    bool r = 分型_判断分型(左, 右, 模式);
    return PyBool_FromLong(r);
}

/* helper: extract 缠论K线** array from 动态数组 or Python list */
static bool Py_提取_缠论K线数组(PyObject *py_序列, 缠论K线 ***out, size_t *out_len, bool *need_free) {
    if (PyObject_TypeCheck(py_序列, &DynArray_Type)) {
        DynArrayObject *da = (DynArrayObject *) py_序列;
        *out = (缠论K线 **) da->arr->数据;
        *out_len = da->arr->长度;
        *need_free = false;
        return true;
    }
    if (PyList_Check(py_序列)) {
        *out_len = (size_t) PyList_Size(py_序列);
        *out = (缠论K线 **) malloc(*out_len * sizeof(缠论K线 *));
        if (!*out) {
            return false;
        }
        *need_free = true;
        for (size_t i = 0; i < *out_len; i++) {
            PyObject *item = PyList_GetItem(py_序列, (Py_ssize_t) i);
            if (!PyObject_TypeCheck(item, &ChanKLine_Type)) {
                free(*out);
                PyErr_SetString(PyExc_TypeError, "K线序列 elements must be 缠论K线");
                return false;
            }
            (*out)[i] = (缠论K线 *) ((ChanObject *) item)->ptr;
        }
        return true;
    }
    PyErr_SetString(PyExc_TypeError, "K线序列 must be 动态数组 or list of 缠论K线");
    return false;
}

static PyObject *Py_分型_从缠K序列中获取分型(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"K线序列", "中", NULL};
    PyObject *py_序列;
    PyObject *py_中;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OO!", kwnames,
                                     &py_序列, &ChanKLine_Type, &py_中)) {
        return NULL;
    }
    缠论K线 *中 = (缠论K线 *) ((ChanObject *) py_中)->ptr;
    缠论K线 **arr = NULL;
    size_t 长度 = 0;
    bool need_free = false;
    if (!Py_提取_缠论K线数组(py_序列, &arr, &长度, &need_free)) {
        return NULL;
    }
    分型 *r = 分型_从缠K序列中获取分型(arr, 长度, 中);
    if (need_free) {
        free(arr);
    }
    if (!r) {
        PyErr_SetString(PyExc_ValueError, "不能从序列中找到中间K线");
        return NULL;
    }
    return Py_制作_拥有(&Fractal_Type, r);
}

static PyObject *Py_分型_向序列中添加(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"分型序列", "当前分型", NULL};
    PyObject *py_序列;
    PyObject *py_当前分型;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!", kwnames,
                                     &DynArray_Type, &py_序列, &Fractal_Type, &py_当前分型)) {
        return NULL;
    }
    动态数组 *分型序列 = ((DynArrayObject *) py_序列)->arr;
    分型 *当前分型 = (分型 *) ((ChanObject *) py_当前分型)->ptr;
    分型_向序列中添加(分型序列, 当前分型);
    Py_RETURN_NONE;
}


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

/* --- 分型 __init__ --- */

static int Py_分型_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    static char *kwlist[] = {"左", "中", "右", NULL};
    PyObject *py_左 = Py_None, *py_中 = NULL, *py_右 = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|OOO", kwlist,
                                     &py_左, &py_中, &py_右)) {
        return -1;
    }
    if (!py_中) {
        PyErr_SetString(PyExc_TypeError, "分型.__init__ 缺少必选参数 '中'");
        return -1;
    }
    if (!PyObject_TypeCheck(py_中, &ChanKLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "'中' 必须是缠论K线");
        return -1;
    }
    缠论K线 *左 = NULL, *中 = (缠论K线 *) ((ChanObject *) py_中)->ptr, *右 = NULL;
    if (py_左 != Py_None) {
        if (!PyObject_TypeCheck(py_左, &ChanKLine_Type)) {
            PyErr_SetString(PyExc_TypeError, "'左' 必须是缠论K线或None");
            return -1;
        }
        左 = (缠论K线 *) ((ChanObject *) py_左)->ptr;
    }
    if (py_右 != Py_None) {
        if (!PyObject_TypeCheck(py_右, &ChanKLine_Type)) {
            PyErr_SetString(PyExc_TypeError, "'右' 必须是缠论K线或None");
            return -1;
        }
        右 = (缠论K线 *) ((ChanObject *) py_右)->ptr;
    }
    void *ptr = 分型_新建(左, 中, 右);
    if (!ptr) {
        PyErr_NoMemory();
        return -1;
    }
    ((ChanObject *) self)->ptr = ptr;
    ((ChanObject *) self)->owns = 1;
    return 0;
}

static int Py_分型_设置_结构(ChanObject *self, PyObject *v, void *c) {
    分型结构 x;
    if (!解析分型结构(v, &x)) {
        return -1;
    }
    ((分型 *) self->ptr)->结构 = x;
    return 0;
}

static int Py_分型_设置_时间戳(ChanObject *self, PyObject *v, void *c) {
    long long x = PyLong_AsLongLong(v);
    if (x == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((分型 *) self->ptr)->时间戳 = (time_t) x;
    return 0;
}

static int Py_分型_设置_分型特征值(ChanObject *self, PyObject *v, void *c) {
    double x = PyFloat_AsDouble(v);
    if (x == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((分型 *) self->ptr)->分型特征值 = x;
    return 0;
}

static PyObject *Py_分型_获取_强度(ChanObject *self, void *c) {
    (void) c;
    return PyUnicode_FromString(分型_强度((分型 *) self->ptr));
}

static PyObject *Py_分型_获取_关系组(ChanObject *self, void *c) {
    (void) c;
    分型 *f = (分型 *) self->ptr;
    相对方向 左中, 中右, 左右;
    if (!分型_关系组(f, &左中, &中右, &左右)) {
        Py_RETURN_NONE;
    }
    return PyTuple_Pack(3,
                        Py_制作_相对方向((int) 左中),
                        Py_制作_相对方向((int) 中右),
                        Py_制作_相对方向((int) 左右));
}

static PyObject *Py_分型_获取_与MACD柱子分型匹配(ChanObject *self, void *c) {
    (void)c;
    return PyBool_FromLong(分型_与MACD柱子分型匹配((分型 *) self->ptr));
}

static PyGetSetDef Fractal_getset[] = {
    {"左", (getter) Py_分型_获取_左, NULL, "Left Chan K-line (read-only)", NULL},
    {"中", (getter) Py_分型_获取_中, NULL, "Mid Chan K-line (read-only)", NULL},
    {"右", (getter) Py_分型_获取_右, NULL, "Right Chan K-line (read-only)", NULL},
    {"结构", (getter) Py_分型_获取_结构, (setter) Py_分型_设置_结构, NULL, NULL},
    {"时间戳", (getter) Py_分型_获取_时间戳, (setter) Py_分型_设置_时间戳, NULL, NULL},
    {"分型特征值", (getter) Py_分型_获取_分型特征值, (setter) Py_分型_设置_分型特征值, NULL, NULL},
    {"强度", (getter) Py_分型_获取_强度, NULL, "Fractal strength: 强/中/弱", NULL},
    {"与MACD柱子分型匹配", (getter) Py_分型_获取_与MACD柱子分型匹配, NULL, "MACD histogram aligns with fractal", NULL},
    {"关系组", (getter) Py_分型_获取_关系组, NULL, "Relative direction tuple (左中, 中右, 左右)", NULL},
    {NULL}
};

static PyMethodDef Fractal_methods[] = {
    {
        "判断分型", (PyCFunction) Py_分型_判断分型,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "分型.判断分型(左, 右, 模式='中') -> bool\n\n"
        "判断两个分型是否相同。\n\n"
        "参数:\n"
        "  左 (分型) — 左分型\n"
        "  右 (分型) — 右分型\n"
        "  模式 (str) — 匹配模式，默认 '中'"
    },
    {
        "从缠K序列中获取分型", (PyCFunction) Py_分型_从缠K序列中获取分型,
        METH_STATIC | METH_VARARGS | METH_KEYWORDS,
        "分型.从缠K序列中获取分型(K线序列, 中) -> 分型\n\n"
        "从缠论K线序列中，根据中间K线创建分型。\n\n"
        "参数:\n"
        "  K线序列 (动态数组|list[缠论K线]) — 缠论K线序列\n"
        "  中 (缠论K线) — 中间K线"
    },
    {
        "向序列中添加", (PyCFunction) Py_分型_向序列中添加,
        METH_STATIC | METH_VARARGS | METH_KEYWORDS,
        "分型.向序列中添加(分型序列, 当前分型)\n\n"
        "向分型序列中添加一个新分型，验证连续分型不能同向。\n\n"
        "参数:\n"
        "  分型序列 (动态数组) — 分型序列（会被修改）\n"
        "  当前分型 (分型) — 要添加的分型"
    },
    {NULL}
};

static PyObject *Py_分型_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
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
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc) Py_分型_初始化,
    .tp_getset = Fractal_getset,
    .tp_methods = Fractal_methods,
    .tp_repr = (reprfunc) Py_分型_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "分型 — 三根连续缠论K线形成的顶/底形态。\n\n"
    "类方法:\n"
    "  分型.判断分型(左, 右, 模式='中') → bool\n"
    "静态方法:\n"
    "  分型.从缠K序列中获取分型(K线序列, 中) → 分型\n"
    "  分型.向序列中添加(分型序列, 当前分型)\n\n"
    "属性:\n"
    "  强度 — 分型强弱：强/中/弱/未知\n"
    "  与MACD柱子分型匹配 — MACD柱方向是否与分型一致",
};


/* ================================================================
 *  虚线 (Dash Line) type — base for 笔 and 线段
 * ================================================================ */

typedef struct {
    ChanObject base;
} DashLineObject;

/* Direction helper */
static PyObject *Py_虚线_获取_方向(ChanObject *self, void *c) {
    return Py_制作_相对方向((int) 虚线_方向((虚线 *) self->ptr));
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
    if (!ck) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_虚线_获取_前一缺口(ChanObject *self, void *c) {
    缺口 *g = ((虚线 *) self->ptr)->前一缺口;
    if (!g) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&Gap_Type, g);
}

static PyObject *Py_虚线_获取_前一结束位置(ChanObject *self, void *c) {
    虚线 *d = ((虚线 *) self->ptr)->前一结束位置;
    if (!d) {
        Py_RETURN_NONE;
    }
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
    if (!py_列表) {
        return NULL;
    }
    for (size_t i = 0; i < d->基础序列.长度; i++) {
        void *基础序列 = 动态数组_获取(&d->基础序列, i);
        PyObject *py_包装 = Py_制作_借用(&DashLine_Type, 基础序列);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

static PyObject *Py_虚线_获取_笔序列(ChanObject *self, void *c) {
    return Py_虚线_获取_基础序列(self, c);
}

static PyObject *Py_虚线_获取_图表标题(ChanObject *self, void *c) {
    (void) c;
    虚线 *d = (虚线 *) self->ptr;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s:%d:%s:%d",
             d->文->中->标识, d->文->中->周期, d->标识, d->序号);
    return PyUnicode_FromString(buf);
}

#define DASH_SEQ_GETTER(cname, pyname, item_type) \
    static PyObject *Py_虚线_获取_##cname(ChanObject *self, void *c) { \
        虚线 *d = (虚线*)self->ptr; \
        size_t n = d->pyname.长度; \
        PyObject *py_列表 = PyList_New((Py_ssize_t)n); \
        if (!py_列表) return NULL; \
        for (size_t i = 0; i < n; i++) { \
            void *基础序列 = 动态数组_获取(&d->pyname, i); \
            PyObject *py_包装 = Py_制作_借用(item_type, 基础序列); \
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
    if (!其他对象) {
        return NULL;
    }
    return PyBool_FromLong(虚线_之前是((虚线 *) self->ptr, (虚线 *) 其他对象));
}

static PyObject *Py_虚线_之后是(ChanObject *self, PyObject *py_其他) {
    void *其他对象 = Py_解包(py_其他, &DashLine_Type);
    if (!其他对象) {
        return NULL;
    }
    return PyBool_FromLong(虚线_之后是((虚线 *) self->ptr, (虚线 *) 其他对象));
}

static PyObject *Py_虚线_获取普K序列(ChanObject *self, PyObject *py_观察者) {
    /* py_观察者 must be an 观察者 */
    void *观察者指针 = Py_解包(py_观察者, &Observer_Type);
    if (!观察者指针) {
        return NULL;
    }
    K线 **py_输出 = NULL;
    size_t 输出长度 = 0;
    虚线_获取普K序列((虚线 *) self->ptr, (观察者 *) 观察者指针, &py_输出, &输出长度);
    if (!py_输出) {
        Py_RETURN_NONE;
    }
    PyObject *py_列表 = PyList_New((Py_ssize_t) 输出长度);
    if (!py_列表) {
        return NULL;
    }
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
    if (!观察者指针) {
        return NULL;
    }
    缠论K线 **py_输出 = NULL;
    size_t 输出长度 = 0;
    虚线_获取缠K序列((虚线 *) self->ptr, (观察者 *) 观察者指针, &py_输出, &输出长度);
    if (!py_输出) {
        Py_RETURN_NONE;
    }
    PyObject *py_列表 = PyList_New((Py_ssize_t) 输出长度);
    if (!py_列表) {
        return NULL;
    }
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
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((虚线 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_虚线_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) {
        return -1;
    }
    strncpy(((虚线 *) self->ptr)->标识, s, 63);
    ((虚线 *) self->ptr)->标识[63] = '\0';
    return 0;
}

static int Py_虚线_设置_级别(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((虚线 *) self->ptr)->级别 = (int) v;
    return 0;
}

static int Py_虚线_设置_高(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((虚线 *) self->ptr)->高 = v;
    return 0;
}

static int Py_虚线_设置_低(ChanObject *self, PyObject *value, void *c) {
    double v = PyFloat_AsDouble(value);
    if (v == -1.0 && PyErr_Occurred()) {
        return -1;
    }
    ((虚线 *) self->ptr)->低 = v;
    return 0;
}

static int Py_虚线_设置_有效性(ChanObject *self, PyObject *value, void *c) {
    ((虚线 *) self->ptr)->有效性 = PyObject_IsTrue(value) ? true : false;
    return 0;
}

static int Py_虚线_设置_模式(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) {
        return -1;
    }
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
    {"笔序列", (getter) Py_虚线_获取_笔序列, NULL, "Alias for 基础序列", NULL},
    {"图表标题", (getter) Py_虚线_获取_图表标题, NULL, "Chart title string", NULL},
    {NULL}
};

static PyObject *Py_虚线_获取数据文本(ChanObject *self, PyObject *unused) {
    (void) unused;
    虚线 *d = (虚线 *) self->ptr;
    char 缓冲[131072];
    虚线_获取数据文本(d, 缓冲, sizeof(缓冲));
    return PyUnicode_FromString(缓冲);
}

static PyMethodDef DashLine_methods[] = {
    {"之前是", (PyCFunction) Py_虚线_之前是, METH_O, "之前是(之前: 虚线) -> bool"},
    {"之后是", (PyCFunction) Py_虚线_之后是, METH_O, "之后是(之后: 虚线) -> bool"},
    {"获取普K序列", (PyCFunction) Py_虚线_获取普K序列, METH_O, "获取普K序列(观察员: 观察者) -> 动态数组[K线]"},
    {"获取缠K序列", (PyCFunction) Py_虚线_获取缠K序列, METH_O, "获取缠K序列(观察员: 观察者) -> 动态数组[缠论K线]"},
    {"获取数据文本", (PyCFunction) Py_虚线_获取数据文本, METH_NOARGS, "获取数据文本() -> str"},
    {
        "创建笔", (PyCFunction) Py_笔_创建笔,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "虚线.创建笔(文, 武, 有效性=True)\n\n"
        "从两个分型创建一笔。\n\n"
        "参数:\n"
        "  文 (分型) — 起始分型\n"
        "  武 (分型) — 结束分型\n"
        "  有效性 (bool) — 是否有效笔，默认 True"
    },
    {
        "创建线段", (PyCFunction) Py_线段_创建线段,
        METH_CLASS | METH_O,
        "虚线.创建线段(虚线序列: 动态数组[虚线])"
    },
    {NULL}
};

static PyObject *Py_虚线_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
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
    .tp_doc = "虚线 — 笔和线段的抽象基类。\n\n"
    "类方法:\n"
    "  虚线.创建笔(文, 武, 有效性=True) → 虚线\n"
    "  虚线.创建线段(虚线序列) → 虚线\n\n"
    "实例方法:\n"
    "  虚线.之前是(之前) → bool\n"
    "  虚线.之后是(之后) → bool\n"
    "  虚线.获取普K序列(观察员) → 动态数组[K线]\n"
    "  虚线.获取缠K序列(观察员) → 动态数组[缠论K线]\n\n"
    "属性:\n"
    "  笔序列 — 基础序列别名\n"
    "  图表标题 — 格式化标题字符串",
};


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
                                     &ChanConfig_Type, &py_配置)) {
        return NULL;
    }

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
               ((DynArrayObject *) frac_seq_obj)->arr,
               ((DynArrayObject *) py_笔序列)->arr,
               ((DynArrayObject *) py_缠K序列)->arr,
               ((DynArrayObject *) k_seq_obj)->arr,
               配置);

    /* C function populates 笔序列 without calling 引用() */
    ((DynArrayObject *) py_笔序列)->owns_elements = false;

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
                                     &ChanConfig_Type, &py_配置)) {
        return NULL;
    }
    int n = 笔_获取缠K数量(((DynArrayObject *) py_缠K序列)->arr,
                                 ((DynArrayObject *) py_笔序列)->arr,
                                 (缠论配置 *) ((ChanObject *) py_配置)->ptr);
    return PyLong_FromLong(n);
}

static PyObject *Py_笔_次高(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点)) {
        return NULL;
    }
    缠论K线 *ck = 笔_次高(((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_次低(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点)) {
        return NULL;
    }
    缠论K线 *ck = 笔_次低(((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_实际高点(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点)) {
        return NULL;
    }
    缠论K线 *ck = 笔_实际高点(((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_实际低点(PyObject *py_类, PyObject *args) {
    PyObject *py_缠K序列;
    int 相同终点 = 0;
    if (!PyArg_ParseTuple(args, "O!p", &DynArray_Type, &py_缠K序列, &相同终点)) {
        return NULL;
    }
    缠论K线 *ck = 笔_实际低点(((DynArrayObject *) py_缠K序列)->arr, (bool) 相同终点);
    if (!ck) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&ChanKLine_Type, ck);
}

static PyObject *Py_笔_以文会友(PyObject *py_类, PyObject *args) {
    PyObject *py_笔序列, *分型_obj;
    if (!PyArg_ParseTuple(args, "O!O!", &DynArray_Type, &py_笔序列,
                          &Fractal_Type, &分型_obj)) {
        return NULL;
    }
    虚线 *s = 笔_以文会友(((DynArrayObject *) py_笔序列)->arr,
                                 (分型 *) ((ChanObject *) 分型_obj)->ptr);
    if (!s) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&Stroke_Type, s);
}

static PyObject *Py_笔_以武会友(PyObject *py_类, PyObject *args) {
    PyObject *py_笔序列, *分型_obj;
    if (!PyArg_ParseTuple(args, "O!O!", &DynArray_Type, &py_笔序列,
                          &Fractal_Type, &分型_obj)) {
        return NULL;
    }
    虚线 *s = 笔_以武会友(((DynArrayObject *) py_笔序列)->arr,
                                 (分型 *) ((ChanObject *) 分型_obj)->ptr);
    if (!s) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&Stroke_Type, s);
}

static PyObject *Py_笔_根据缠K找笔(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"笔序列", "缠K", "偏移", NULL};
    PyObject *py_笔序列, *py_缠K;
    int 偏移量 = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|i", kwnames,
                                     &DynArray_Type, &py_笔序列,
                                     &ChanKLine_Type, &py_缠K,
                                     &偏移量)) {
        return NULL;
    }
    虚线 *s = 笔_根据缠K找笔(((DynArrayObject *) py_笔序列)->arr,
                                     (缠论K线 *) ((ChanObject *) py_缠K)->ptr, 偏移量);
    if (!s) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&Stroke_Type, s);
}


/* ================================================================
 *  笔 (Stroke) type — uses 虚线 struct
 * ================================================================ */

typedef struct {
    ChanObject base;
} StrokeObject;

static PyObject *Py_笔_创建笔(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"文", "武", "有效性", NULL};
    PyObject *py_文, *py_武;
    int 有效性 = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|p", kwnames,
                                     &Fractal_Type, &py_文, &Fractal_Type, &py_武, &有效性)) {
        return NULL;
    }
    void *ptr = 虚线_创建笔(
                    (分型 *) ((ChanObject *) py_文)->ptr,
                    (分型 *) ((ChanObject *) py_武)->ptr,
                    (bool) 有效性);
    if (!ptr) {
        return PyErr_NoMemory();
    }
    return Py_制作_拥有(&DashLine_Type, ptr);
}

static PyObject *Py_笔_相对关系(PyObject *cls, PyObject *args) {
    PyObject *py_笔, *py_配置;
    if (!PyArg_ParseTuple(args, "O!O!", &DashLine_Type, &py_笔, &ChanConfig_Type, &py_配置)) {
        return NULL;
    }
    return PyBool_FromLong(笔_相对关系(
                               (虚线 *) ((ChanObject *) py_笔)->ptr,
                               (缠论配置 *) ((ChanObject *) py_配置)->ptr));
}

static PyMethodDef Stroke_methods[] = {
    {
        "创建笔", (PyCFunction) Py_笔_创建笔,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "Create a stroke (笔) from two fractals."
    },
    {
        "相对关系", (PyCFunction) Py_笔_相对关系, METH_CLASS | METH_VARARGS,
        "笔.相对关系(笔, 配置) — 判断笔的方向关系（向上/向下）。\n\n"
        "参数:\n"
        "  笔 (虚线) — 笔对象\n"
        "  配置 (缠论配置) — 分析配置"
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

static PyTypeObject Stroke_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.笔",
    .tp_basicsize = sizeof(StrokeObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_methods = Stroke_methods,
    .tp_doc = "笔 — 工厂类（仅类方法）。操作数据均为虚线。\n\n"
    "类方法:\n"
    "  笔.创建笔(文, 武, 有效性=True) → 虚线\n"
    "  笔.分析(初始分型, 分型序列, 笔序列, 缠K序列, 普K序列, 配置)\n"
    "  笔.获取缠K数量(缠K序列, 笔序列, 配置) → int\n"
    "  笔.次高(缠K序列, 笔内相同终点取舍) → 缠论K线\n"
    "  笔.次低(缠K序列, 笔内相同终点取舍) → 缠论K线\n"
    "  笔.实际高点(缠K序列, 笔内相同终点取舍) → 缠论K线\n"
    "  笔.实际低点(缠K序列, 笔内相同终点取舍) → 缠论K线\n"
    "  笔.以文会友(笔序列, 文) → 虚线\n"
    "  笔.以武会友(笔序列, 武) → 虚线\n"
    "  笔.根据缠K找笔(笔序列, 缠K, 偏移=0) → 虚线\n"
    "  笔.相对关系(笔, 配置) → bool",
};


/* 线段特征.静态分析(虚线序列, 方向, 四象, 是否忽视, 结果) */
static PyObject *Py_线段特征_静态分析(PyObject *py_类, PyObject *args) {
    PyObject *py_虚线序列;
    int direction, ignore;
    const char *sixiang;
    PyObject *result_obj;

    if (!PyArg_ParseTuple(args, "OispO!",
                          &py_虚线序列, &direction, &sixiang, &ignore,
                          &DynArray_Type, &result_obj)) {
        return NULL;
    }

    if (!PyObject_TypeCheck(py_虚线序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError,
                        "静态分析 arg1 requires a 动态数组 of dashes");
        return NULL;
    }

    线段特征_静态分析(
        ((DynArrayObject *) py_虚线序列)->arr,
        (相对方向) direction, sixiang, (bool) ignore,
        ((DynArrayObject *) result_obj)->arr);
    /* C function populated py_结果 without calling 引用() */
    ((DynArrayObject *) result_obj)->owns_elements = false;
    Py_RETURN_NONE;
}

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
    PyObject *py_列表 = PyList_New((Py_ssize_t) sf->基础序列.长度);
    if (!py_列表) {
        return NULL;
    }
    for (size_t i = 0; i < sf->基础序列.长度; i++) {
        void *基础序列 = 动态数组_获取(&sf->基础序列, i);
        PyObject *py_包装 = Py_制作_借用(&DashLine_Type, 基础序列);
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
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((线段特征 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_线段特征_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) {
        return -1;
    }
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
    {"基础序列", (getter) Py_线段特征_获取_元素, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_线段特征_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
    线段特征 *sf = (线段特征 *) self->ptr;
    char 缓冲[384];
    snprintf(缓冲, sizeof(缓冲),
             "%s<%s, %s, %s, %zu>",
             sf->标识,
             相对方向_到名称(线段特征_方向(sf)),
             线段特征_文(sf) ? 分型结构_到名称(线段特征_文(sf)->结构) : "?",
             线段特征_武(sf) ? 分型结构_到名称(线段特征_武(sf)->结构) : "?",
             sf->基础序列.长度);
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


/* 线段.创建线段(动态数组) -> 线段 */
static PyObject *Py_线段_创建线段(PyObject *py_类, PyObject *py_参数) {
    if (!PyObject_TypeCheck(py_参数, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError,
                        "创建线段 requires a 动态数组 of strokes");
        return NULL;
    }
    DynArrayObject *da = (DynArrayObject *) py_参数;
    虚线 *ptr = 虚线_创建线段(da->arr);
    if (!ptr) {
        return PyErr_NoMemory();
    }
    /* C function took ownership of elements; prevent double-free */
    da->owns_elements = false;
    return Py_制作_拥有(&DashLine_Type, ptr);
}

static PyObject *Py_线段_分析(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {
        "笔序列", "线段序列", "配置", NULL
    };
    PyObject *py_笔序列, *seg_seq_obj, *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!O!", kwnames,
                                     &DynArray_Type, &py_笔序列,
                                     &DynArray_Type, &seg_seq_obj,
                                     &ChanConfig_Type, &py_配置)) {
        return NULL;
    }

    缠论配置 *配置 = (缠论配置 *) ((ChanObject *) py_配置)->ptr;

    线段_分析(((DynArrayObject *) py_笔序列)->arr,
                  ((DynArrayObject *) seg_seq_obj)->arr,
                  配置);

    ((DynArrayObject *) seg_seq_obj)->owns_elements = false;
    Py_RETURN_NONE;
}

/* ================================================================
 *  线段 (Segment) type — uses 虚线 struct
 * ================================================================ */

typedef struct {
    ChanObject base;
} SegmentObject;

static PyObject *Py_线段_获取_四象(PyObject *cls, PyObject *py_段) {
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    return PyUnicode_FromString(线段_四象((虚线 *) ((ChanObject *) py_段)->ptr));
}

static PyObject *Py_线段_获取_特征分型终结(PyObject *cls, PyObject *py_段) {
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    return PyBool_FromLong(线段_特征分型终结((虚线 *) ((ChanObject *) py_段)->ptr));
}

static PyObject *Py_线段_获取_特征序列状态(PyObject *cls, PyObject *py_段) {
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    bool left = false, mid = false, right = false;
    线段_特征序列状态((虚线 *) ((ChanObject *) py_段)->ptr, &left, &mid, &right);
    return Py_BuildValue("(ppp)", left, mid, right);
}

static PyObject *Py_线段_获取_缺口(PyObject *cls, PyObject *py_段) {
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    缺口 *g = 线段_获取缺口((虚线 *) ((ChanObject *) py_段)->ptr);
    if (!g) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&Gap_Type, g);
}

static PyObject *Py_线段_查找贯穿伤(PyObject *cls, PyObject *py_段) {
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    虚线 *d = 线段_查找贯穿伤((虚线 *) ((ChanObject *) py_段)->ptr);
    if (!d) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&DashLine_Type, d);
}

static PyObject *Py_线段_添加虚线(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_筆;
    if (!PyArg_ParseTuple(args, "OO", &py_段, &py_筆)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_筆, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    虚线 *段 = (虚线 *) ((ChanObject *) py_段)->ptr;
    虚线 *筆 = (虚线 *) ((ChanObject *) py_筆)->ptr;
    if (!线段_添加虚线(段, 筆)) {
        PyErr_SetString(PyExc_ValueError, "添加虚线失败：不连续或标识不符");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *Py_线段_武斗(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_武;
    int 行号;
    if (!PyArg_ParseTuple(args, "OOi", &py_段, &py_武, &行号)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_武, &Fractal_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要分型参数");
        return NULL;
    }
    线段_武斗((虚线 *) ((ChanObject *) py_段)->ptr, (分型 *) ((ChanObject *) py_武)->ptr, 行号);
    Py_RETURN_NONE;
}

static PyObject *Py_线段_设置特征序列(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_序列;
    int 行号;
    if (!PyArg_ParseTuple(args, "OOi", &py_段, &py_序列, &行号)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyList_Check(py_序列) || PyList_Size(py_序列) != 3) {
        PyErr_SetString(PyExc_TypeError, "序列需要长度为3的列表");
        return NULL;
    }
    虚线 *段 = (虚线 *) ((ChanObject *) py_段)->ptr;
    线段特征 *seq[3] = {NULL, NULL, NULL};
    for (int i = 0; i < 3; i++) {
        PyObject *item = PyList_GetItem(py_序列, i);
        if (item == Py_None) {
            continue;
        }
        if (!PyObject_TypeCheck(item, &SegFeature_Type)) {
            PyErr_SetString(PyExc_TypeError, "序列元素需为线段特征或None");
            return NULL;
        }
        seq[i] = (线段特征 *) ((ChanObject *) item)->ptr;
    }
    线段_设置特征序列(段, seq, 行号);
    Py_RETURN_NONE;
}

static PyObject *Py_线段_刷新特征序列(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_配置;
    if (!PyArg_ParseTuple(args, "OO", &py_段, &py_配置)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_配置, &ChanConfig_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要配置参数");
        return NULL;
    }
    线段_刷新特征序列((虚线 *) ((ChanObject *) py_段)->ptr, (缠论配置 *) ((ChanObject *) py_配置)->ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_线段_分割序列(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwlist[] = {"段", "所属中枢", NULL};
    PyObject *py_段, *py_所属中枢 = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O|O", kwlist, &py_段, &py_所属中枢)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    中枢 *所属中枢 = NULL;
    if (py_所属中枢 != Py_None) {
        if (!PyObject_TypeCheck(py_所属中枢, &Hub_Type)) {
            PyErr_SetString(PyExc_TypeError, "所属中枢需为中枢类型");
            return NULL;
        }
        所属中枢 = (中枢 *) ((ChanObject *) py_所属中枢)->ptr;
    }
    虚线 *段 = (虚线 *) ((ChanObject *) py_段)->ptr;
    动态数组 前, 后, 第三;
    动态数组_初始化(&前, 4);
    动态数组_初始化(&后, 4);
    动态数组_初始化(&第三, 4);
    虚线 *贯穿伤 = NULL;
    线段_分割序列(段, 所属中枢, &前, &后, &第三, &贯穿伤);

    /* Convert C dynamic arrays to Python lists, then clear C arrays */
    PyObject *py_前 = PyList_New(前.长度);
    for (size_t i = 0; i < 前.长度; i++) {
        PyList_SetItem(py_前, i, Py_制作_借用(&DashLine_Type, 动态数组_获取(&前, i)));
    }
    PyObject *py_后 = PyList_New(后.长度);
    for (size_t i = 0; i < 后.长度; i++) {
        PyList_SetItem(py_后, i, Py_制作_借用(&DashLine_Type, 动态数组_获取(&后, i)));
    }
    PyObject *py_第三 = PyList_New(第三.长度);
    for (size_t i = 0; i < 第三.长度; i++) {
        PyList_SetItem(py_第三, i, Py_制作_借用(&DashLine_Type, 动态数组_获取(&第三, i)));
    }
    PyObject *py_贯穿伤 = 贯穿伤 ? Py_制作_借用(&DashLine_Type, 贯穿伤) : Py_NewRef(Py_None);

    /* Decrement weak refs and free C array buffers */
    弱引用_数组清除(&前);
    弱引用_数组清除(&后);
    弱引用_数组清除(&第三);

    return Py_BuildValue("(NNNN)", py_前, py_后, py_第三, py_贯穿伤);
}

static PyObject *Py_线段_刷新(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_配置;
    if (!PyArg_ParseTuple(args, "OO", &py_段, &py_配置)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_配置, &ChanConfig_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要配置参数");
        return NULL;
    }
    线段_刷新((虚线 *) ((ChanObject *) py_段)->ptr, (缠论配置 *) ((ChanObject *) py_配置)->ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_线段_序列重置(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_序列;
    if (!PyArg_ParseTuple(args, "OO", &py_段, &py_序列)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    线段_序列重置((虚线 *) ((ChanObject *) py_段)->ptr, ((DynArrayObject *) py_序列)->arr);
    Py_RETURN_NONE;
}

/* Helper: convert C dynamic array of hubs to Python list */
static PyObject *Py_中枢数组_转列表(动态数组 *arr) {
    PyObject *list = PyList_New(arr->长度);
    for (size_t i = 0; i < arr->长度; i++) {
        PyList_SetItem(list, i, Py_制作_借用(&Hub_Type, 动态数组_获取(arr, i)));
    }
    return list;
}

static PyObject *Py_线段_获取内部中枢序列(PyObject *cls, PyObject *args) {
    PyObject *py_段, *py_配置;
    if (!PyArg_ParseTuple(args, "OO", &py_段, &py_配置)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_段, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_配置, &ChanConfig_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要配置参数");
        return NULL;
    }
    虚线 *段 = (虚线 *) ((ChanObject *) py_段)->ptr;
    缠论配置 *配置 = (缠论配置 *) ((ChanObject *) py_配置)->ptr;
    动态数组 虚_out, 实_out, 合_out;
    线段_获取内部中枢序列(段, 配置, &虚_out, &实_out, &合_out);
    PyObject *py_虚 = Py_中枢数组_转列表(&虚_out);
    PyObject *py_实 = Py_中枢数组_转列表(&实_out);
    PyObject *py_合 = Py_中枢数组_转列表(&合_out);
    return Py_BuildValue("(NNN)", py_虚, py_实, py_合);
}

static PyObject *Py_线段_基础判断(PyObject *cls, PyObject *args) {
    PyObject *py_左, *py_中, *py_右, *py_关系序列;
    if (!PyArg_ParseTuple(args, "OOOO", &py_左, &py_中, &py_右, &py_关系序列)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_左, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_中, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_右, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyList_Check(py_关系序列)) {
        PyErr_SetString(PyExc_TypeError, "关系序列需为列表");
        return NULL;
    }
    Py_ssize_t n = PyList_Size(py_关系序列);
    if (n == 0 || n > 16) {
        PyErr_SetString(PyExc_ValueError, "关系序列长度需在1-16之间");
        return NULL;
    }
    相对方向 rels[16];
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyList_GetItem(py_关系序列, i);
        rels[i] = (相对方向) PyLong_AsLong(item);
    }
    bool ok = 线段_基础判断(
                  (虚线 *) ((ChanObject *) py_左)->ptr,
                  (虚线 *) ((ChanObject *) py_中)->ptr,
                  (虚线 *) ((ChanObject *) py_右)->ptr,
                  rels, (size_t) n);
    return PyBool_FromLong(ok);
}

static PyMethodDef Segment_methods[] = {
    {
        "添加虚线", (PyCFunction) Py_线段_添加虚线, METH_VARARGS | METH_CLASS,
        "线段.添加虚线(段, 筆) — 向线段基础序列中追加一笔。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  筆 (虚线) — 待添加的笔"
    },
    {
        "武斗", (PyCFunction) Py_线段_武斗, METH_VARARGS | METH_CLASS,
        "线段.武斗(段, 武, 行号) — 设置线段的武（终点）分型。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  武 (分型) — 终点分型\n"
        "  行号 (int) — 调用行号"
    },
    {
        "四象", (PyCFunction) Py_线段_获取_四象, METH_CLASS | METH_O,
        "线段.四象(段) — 获取线段的四象分类（老阳/老阴/少阴/小阳）。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象"
    },
    {
        "特征分型终结", (PyCFunction) Py_线段_获取_特征分型终结, METH_CLASS | METH_O,
        "线段.特征分型终结(段) — 检查线段是否有特征分型终结。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象"
    },
    {
        "特征序列状态", (PyCFunction) Py_线段_获取_特征序列状态, METH_CLASS | METH_O,
        "线段.特征序列状态(段) — 返回 (左, 中, 右) 特征序列状态元组。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象"
    },
    {
        "获取缺口", (PyCFunction) Py_线段_获取_缺口, METH_CLASS | METH_O,
        "线段.获取缺口(段) — 获取线段前的缺口（或 None）。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象"
    },
    {
        "设置特征序列", (PyCFunction) Py_线段_设置特征序列, METH_VARARGS | METH_CLASS,
        "线段.设置特征序列(段, 序列, 行号) — 设置线段的特征序列。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  序列 (list) — 长度为3的列表（线段特征或None）\n"
        "  行号 (int) — 调用行号"
    },
    {
        "刷新特征序列", (PyCFunction) Py_线段_刷新特征序列, METH_VARARGS | METH_CLASS,
        "线段.刷新特征序列(段, 配置) — 根据配置刷新线段的特征序列。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  配置 (缠论配置) — 分析配置"
    },
    {
        "分割序列", (PyCFunction) Py_线段_分割序列,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "线段.分割序列(段, 所属中枢=None) -> (前, 后, 第三买卖线, 贯穿伤)\n\n"
        "分割线段的基础序列为前段和后段。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  所属中枢 (中枢|None) — 可选，所属的中枢"
    },
    {
        "刷新", (PyCFunction) Py_线段_刷新, METH_VARARGS | METH_CLASS,
        "线段.刷新(段, 配置) — 刷新线段状态（特征序列+武斗+内部中枢）。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  配置 (缠论配置) — 分析配置"
    },
    {
        "序列重置", (PyCFunction) Py_线段_序列重置, METH_VARARGS | METH_CLASS,
        "线段.序列重置(段, 序列) — 根据笔序列重置线段的基础序列。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  序列 (动态数组) — 笔序列"
    },
    {
        "查找贯穿伤", (PyCFunction) Py_线段_查找贯穿伤, METH_CLASS | METH_O,
        "线段.查找贯穿伤(段) — 查找贯穿该线段的虚线（或 None）。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象"
    },
    {
        "获取内部中枢序列", (PyCFunction) Py_线段_获取内部中枢序列, METH_VARARGS | METH_CLASS,
        "线段.获取内部中枢序列(段, 配置) -> (虚中枢, 实中枢, 合中枢)\n\n"
        "获取线段内部的三个中枢序列。\n\n"
        "参数:\n"
        "  段 (虚线) — 线段对象\n"
        "  配置 (缠论配置) — 分析配置"
    },
    {
        "基础判断", (PyCFunction) Py_线段_基础判断, METH_VARARGS | METH_CLASS,
        "线段.基础判断(左, 中, 右, 关系序列) -> bool\n\n"
        "判断三笔是否满足线段基本要求（连续、重叠、方向正确）。\n\n"
        "参数:\n"
        "  左 (虚线) — 左侧笔\n"
        "  中 (虚线) — 中间笔\n"
        "  右 (虚线) — 右侧笔\n"
        "  关系序列 (list[相对方向]) — 允许的相对方向列表"
    },
    {
        "分析", (PyCFunction) Py_线段_分析,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "线段.分析(笔序列, 线段序列, 配置)\n\n"
        "在笔序列上运行线段分析。\n\n"
        "参数:\n"
        "  笔序列 (动态数组) — 笔对象的动态数组\n"
        "  线段序列 (动态数组) — 输出线段将追加到此数组\n"
        "  配置 (缠论配置) — 分析配置"
    },
    {NULL}
};

static PyTypeObject Segment_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.线段",
    .tp_basicsize = sizeof(SegmentObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_Chan对象_释放,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_methods = Segment_methods,
    .tp_doc = "线段 — 工厂类（仅类方法）。操作数据均为虚线。\n\n"
    "类方法:\n"
    "  线段.添加虚线(段, 筆)\n"
    "  线段.武斗(段, 武, 行号)\n"
    "  线段.四象(段) → str\n"
    "  线段.特征分型终结(段) → bool\n"
    "  线段.特征序列状态(段) → (bool, bool, bool)\n"
    "  线段.获取缺口(段) → 缺口|None\n"
    "  线段.设置特征序列(段, 序列, 行号)\n"
    "  线段.刷新特征序列(段, 配置)\n"
    "  线段.分割序列(段, 所属中枢=None) -> (前, 后, 第三买卖线, 贯穿伤)\n"
    "  线段.刷新(段, 配置)\n"
    "  线段.序列重置(段, 序列)\n"
    "  线段.查找贯穿伤(段) → 虚线|None\n"
    "  线段.获取内部中枢序列(段, 配置) -> (虚, 实, 合)\n"
    "  线段.基础判断(左, 中, 右, 关系序列) -> bool\n"
    "  线段.分析(笔序列, 线段序列, 配置)",
};


/* 中枢.获取序列() -> 动态数组 */
static PyObject *Py_中枢_获取序列(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    DynArrayObject *py_结果 = (DynArrayObject *) Py_动态数组_new(&DynArray_Type, NULL, NULL);
    if (!py_结果) {
        return NULL;
    }
    中枢_获取序列((中枢 *) self->ptr, py_结果->arr);
    py_结果->owns_elements = false; /* C function populated without 引用() */
    return (PyObject *) py_结果;
}

static PyObject *Py_中枢_分析(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {
        "虚线序列", "中枢序列", "跳过首部", "标识", NULL
    };
    PyObject *py_虚线序列, *中枢_seq_obj;
    int 跳过首个 = 1;
    const char *id_str = "hub";

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|ps", kwnames,
                                     &DynArray_Type, &py_虚线序列,
                                     &DynArray_Type, &中枢_seq_obj,
                                     &跳过首个, &id_str)) {
        return NULL;
    }

    中枢_分析(((DynArrayObject *) py_虚线序列)->arr,
                  ((DynArrayObject *) 中枢_seq_obj)->arr,
                  (bool) 跳过首个, id_str);

    ((DynArrayObject *) 中枢_seq_obj)->owns_elements = false;
    Py_RETURN_NONE;
}

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

/* 中枢.完整性(虚实='合') -> bool */
static PyObject *Py_中枢_完整性(ChanObject *self, PyObject *args) {
    const char *虚实 = "合";
    if (!PyArg_ParseTuple(args, "|s", &虚实)) {
        return NULL;
    }
    return PyBool_FromLong(中枢_完整性((中枢 *) self->ptr, 虚实));
}

static PyObject *Py_中枢_获取_第三买卖线(ChanObject *self, void *c) {
    虚线 *d = ((中枢 *) self->ptr)->第三买卖线;
    if (!d) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&DashLine_Type, d);
}

static PyObject *Py_中枢_获取_本级_第三买卖线(ChanObject *self, void *c) {
    虚线 *d = ((中枢 *) self->ptr)->本级_第三买卖线;
    if (!d) {
        Py_RETURN_NONE;
    }
    return Py_制作_借用(&DashLine_Type, d);
}

static PyObject *Py_中枢_获取_元素(ChanObject *self, void *c) {
    中枢 *h = (中枢 *) self->ptr;
    PyObject *py_列表 = PyList_New((Py_ssize_t) h->基础序列.长度);
    if (!py_列表) {
        return NULL;
    }
    for (size_t i = 0; i < h->基础序列.长度; i++) {
        void *基础序列 = 动态数组_获取(&h->基础序列, i);
        PyObject *py_包装 = Py_制作_借用(&DashLine_Type, 基础序列);
        if (!py_包装) {
            Py_DECREF(py_列表);
            return NULL;
        }
        PyList_SET_ITEM(py_列表, (Py_ssize_t) i, py_包装);
    }
    return py_列表;
}

/* 中枢.添加虚线(实线) */
static PyObject *Py_中枢_添加虚线(PyObject *py_self, PyObject *py_线) {
    if (!PyObject_TypeCheck(py_self, &Hub_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要中枢实例");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_线, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    中枢_添加虚线((中枢 *) ((ChanObject *) py_self)->ptr,
                        (虚线 *) ((ChanObject *) py_线)->ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_中枢_当前状态(ChanObject *self, PyObject *Py_UNUSED(py_忽略)) {
    return PyUnicode_FromString(中枢_当前状态((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_文(ChanObject *self, void *c) {
    return Py_制作_借用(&Fractal_Type, 中枢_文((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_武(ChanObject *self, void *c) {
    return Py_制作_借用(&Fractal_Type, 中枢_武((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_离开段(ChanObject *self, void *c) {
    return Py_制作_借用(&DashLine_Type, 中枢_离开段((中枢 *) self->ptr));
}

static PyObject *Py_中枢_获取_图表标题(ChanObject *self, void *c) {
    中枢 *h = (中枢 *) self->ptr;
    分型 *文 = 中枢_文(h);
    return PyUnicode_FromFormat("%s:%d:%s:%d",
                                文->中->标识, 文->中->周期, h->标识, h->序号);
}

static PyObject *Py_中枢_获取扩展中枢(PyObject *py_self, PyObject *args) {
    PyObject *py_扩展中枢, *py_配置;
    if (!PyArg_ParseTuple(args, "OO", &py_扩展中枢, &py_配置)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_self, &Hub_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要中枢实例");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_扩展中枢, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_配置, &ChanConfig_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要配置参数");
        return NULL;
    }
    中枢_获取扩展中枢((中枢 *) ((ChanObject *) py_self)->ptr,
                              ((DynArrayObject *) py_扩展中枢)->arr,
                              (缠论配置 *) ((ChanObject *) py_配置)->ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_中枢_校验合法性(PyObject *py_self, PyObject *args) {
    PyObject *py_序列, *py_中枢序列;
    if (!PyArg_ParseTuple(args, "OO", &py_序列, &py_中枢序列)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_self, &Hub_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要中枢实例");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_中枢序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    return PyBool_FromLong(中枢_校验合法性(
                               (中枢 *) ((ChanObject *) py_self)->ptr,
                               ((DynArrayObject *) py_序列)->arr,
                               ((DynArrayObject *) py_中枢序列)->arr));
}

static PyObject *Py_中枢_设置第三买卖线(PyObject *py_self, PyObject *py_线) {
    if (!PyObject_TypeCheck(py_self, &Hub_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要中枢实例");
        return NULL;
    }
    虚线 *线 = NULL;
    if (py_线 != Py_None) {
        if (!PyObject_TypeCheck(py_线, &DashLine_Type)) {
            PyErr_SetString(PyExc_TypeError, "需要虚线或None");
            return NULL;
        }
        线 = (虚线 *) ((ChanObject *) py_线)->ptr;
    }
    中枢_设置第三买卖线((中枢 *) ((ChanObject *) py_self)->ptr, 线);
    Py_RETURN_NONE;
}

/* --- 中枢 classmethods --- */

static PyObject *Py_中枢_基础检查(PyObject *cls, PyObject *args) {
    PyObject *py_左, *py_中, *py_右;
    if (!PyArg_ParseTuple(args, "OOO", &py_左, &py_中, &py_右)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_左, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_中, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_右, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    return PyBool_FromLong(中枢_基础检查(
                               (虚线 *) ((ChanObject *) py_左)->ptr,
                               (虚线 *) ((ChanObject *) py_中)->ptr,
                               (虚线 *) ((ChanObject *) py_右)->ptr));
}

static PyObject *Py_中枢_创建(PyObject *cls, PyObject *args, PyObject *kw) {
    static char *kwlist[] = {"左", "中", "右", "级别", "标识", NULL};
    PyObject *py_左, *py_中, *py_右;
    int 级别;
    const char *标识 = "";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OOOi|s", kwlist,
                                     &py_左, &py_中, &py_右, &级别, &标识)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_左, &DashLine_Type) ||
            !PyObject_TypeCheck(py_中, &DashLine_Type) ||
            !PyObject_TypeCheck(py_右, &DashLine_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要虚线参数");
        return NULL;
    }
    中枢 *z = 中枢_创建(
                    (虚线 *) ((ChanObject *) py_左)->ptr,
                    (虚线 *) ((ChanObject *) py_中)->ptr,
                    (虚线 *) ((ChanObject *) py_右)->ptr, 级别, 标识);
    if (!z) {
        PyErr_SetString(PyExc_ValueError, "基础检查失败");
        return NULL;
    }
    return Py_制作_拥有(&Hub_Type, z);
}

static PyObject *Py_中枢_从序列中获取中枢(PyObject *cls, PyObject *args) {
    PyObject *py_虚线序列, *py_起始方向;
    const char *标识;
    if (!PyArg_ParseTuple(args, "OOs", &py_虚线序列, &py_起始方向, &标识)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_虚线序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    相对方向 dir = (相对方向) PyLong_AsLong(py_起始方向);
    中枢 *z = 中枢_从序列中获取中枢(((DynArrayObject *) py_虚线序列)->arr, dir, 标识);
    if (!z) {
        Py_RETURN_NONE;
    }
    return Py_制作_拥有(&Hub_Type, z);
}

static PyObject *Py_中枢_向中枢序列尾部添加(PyObject *cls, PyObject *args) {
    PyObject *py_中枢序列, *py_待添加;
    if (!PyArg_ParseTuple(args, "OO", &py_中枢序列, &py_待添加)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_中枢序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_待添加, &Hub_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要中枢参数");
        return NULL;
    }
    中枢_向中枢序列尾部添加(
        ((DynArrayObject *) py_中枢序列)->arr,
        (中枢 *) ((ChanObject *) py_待添加)->ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_中枢_从中枢序列尾部弹出(PyObject *cls, PyObject *args) {
    PyObject *py_中枢序列, *py_待弹出;
    if (!PyArg_ParseTuple(args, "OO", &py_中枢序列, &py_待弹出)) {
        return NULL;
    }
    if (!PyObject_TypeCheck(py_中枢序列, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要动态数组参数");
        return NULL;
    }
    if (!PyObject_TypeCheck(py_待弹出, &Hub_Type)) {
        PyErr_SetString(PyExc_TypeError, "需要中枢参数");
        return NULL;
    }
    中枢 *result = 中枢_从中枢序列尾部弹出(
                         ((DynArrayObject *) py_中枢序列)->arr,
                         (中枢 *) ((ChanObject *) py_待弹出)->ptr);
    if (!result) {
        Py_RETURN_NONE;
    }
    return Py_制作_拥有(&Hub_Type, result);
}

/* --- 中枢 setters --- */
static int Py_中枢_设置_序号(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((中枢 *) self->ptr)->序号 = (int) v;
    return 0;
}

static int Py_中枢_设置_标识(ChanObject *self, PyObject *value, void *c) {
    const char *s = PyUnicode_AsUTF8(value);
    if (!s) {
        return -1;
    }
    strncpy(((中枢 *) self->ptr)->标识, s, 127);
    ((中枢 *) self->ptr)->标识[127] = '\0';
    return 0;
}

static int Py_中枢_设置_级别(ChanObject *self, PyObject *value, void *c) {
    long v = PyLong_AsLong(value);
    if (v == -1 && PyErr_Occurred()) {
        return -1;
    }
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
    {"文", (getter) Py_中枢_获取_文, NULL, NULL, NULL},
    {"武", (getter) Py_中枢_获取_武, NULL, NULL, NULL},
    {"离开段", (getter) Py_中枢_获取_离开段, NULL, NULL, NULL},
    {"图表标题", (getter) Py_中枢_获取_图表标题, NULL, NULL, NULL},
    {"第三买卖线", (getter) Py_中枢_获取_第三买卖线, NULL, NULL, NULL},
    {"本级_第三买卖线", (getter) Py_中枢_获取_本级_第三买卖线, NULL, NULL, NULL},
    {"基础序列", (getter) Py_中枢_获取_元素, NULL, NULL, NULL},
    {NULL}
};

static PyObject *Py_中枢_获取数据文本(ChanObject *self, PyObject *unused) {
    (void) unused;
    中枢 *h = (中枢 *) self->ptr;
    char 缓冲[3072];
    中枢_获取数据文本(h, 缓冲, sizeof(缓冲));
    return PyUnicode_FromString(缓冲);
}

static PyMethodDef Hub_methods[] = {
    {
        "完整性", (PyCFunction) Py_中枢_完整性, METH_VARARGS,
        "中枢.完整性(虚实='合') -> bool\n\n"
        "检查中枢是否完整。\n\n"
        "详情见 教你炒股票 43：有关背驰的补习课\n"
        "不完整时下一个中枢大概率会与当前中枢发生扩展！\n\n"
        "参数:\n"
        "  虚实 (str) — '合'（默认）或'实'，线段中枢时选择使用合序列或实序列"
    },
    {
        "添加虚线", (PyCFunction) Py_中枢_添加虚线, METH_O,
        "中枢.添加虚线(实线)\n\n"
        "向中枢的基础序列追加一条虚线，并清除第三买卖线。\n\n"
        "参数:\n"
        "  实线 (虚线) — 待添加的虚线"
    },
    {
        "当前状态", (PyCFunction) Py_中枢_当前状态, METH_NOARGS,
        "中枢.当前状态() -> str\n\n"
        "获取当前中枢的状态（中枢之中/之上/之下）。"
    },
    {
        "获取序列", (PyCFunction) Py_中枢_获取序列, METH_NOARGS,
        "中枢.获取序列() -> list[虚线]\n\n"
        "获取中枢的元素序列（含第三买卖线）。"
    },
    {
        "获取扩展中枢", (PyCFunction) Py_中枢_获取扩展中枢, METH_VARARGS,
        "中枢.获取扩展中枢(扩展中枢, 配置)\n\n"
        "获取中枢的扩展中枢。\n\n"
        "参数:\n"
        "  扩展中枢 (动态数组) — 输出扩展中枢将追加到此数组\n"
        "  配置 (缠论配置) — 分析配置"
    },
    {
        "校验合法性", (PyCFunction) Py_中枢_校验合法性, METH_VARARGS,
        "中枢.校验合法性(序列, 中枢序列) -> bool\n\n"
        "校验中枢在序列中的合法性。\n\n"
        "参数:\n"
        "  序列 (动态数组) — 虚线序列\n"
        "  中枢序列 (动态数组) — 中枢序列"
    },
    {
        "设置第三买卖线", (PyCFunction) Py_中枢_设置第三买卖线, METH_O,
        "中枢.设置第三买卖线(线)\n\n"
        "设置中枢的第三买卖线（None则清除）。\n\n"
        "参数:\n"
        "  线 (虚线|None) — 第三买卖线"
    },
    {
        "基础检查", (PyCFunction) Py_中枢_基础检查, METH_VARARGS | METH_CLASS,
        "中枢.基础检查(左, 中, 右) -> bool\n\n"
        "检查三根虚线是否满足中枢基本要求。\n\n"
        "参数:\n"
        "  左 (虚线) — 左侧虚线\n"
        "  中 (虚线) — 中间虚线\n"
        "  右 (虚线) — 右侧虚线"
    },
    {
        "创建", (PyCFunction) Py_中枢_创建, METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "中枢.创建(左, 中, 右, 级别, 标识='') -> 中枢\n\n"
        "从三根虚线创建中枢。\n\n"
        "参数:\n"
        "  左 (虚线) — 左侧虚线\n"
        "  中 (虚线) — 中间虚线\n"
        "  右 (虚线) — 右侧虚线\n"
        "  级别 (int) — 中枢级别\n"
        "  标识 (str) — 中枢标识前缀"
    },
    {
        "从序列中获取中枢", (PyCFunction) Py_中枢_从序列中获取中枢, METH_VARARGS | METH_CLASS,
        "中枢.从序列中获取中枢(虚线序列, 起始方向, 标识) -> 中枢|None\n\n"
        "从虚线序列中寻找中枢。\n\n"
        "参数:\n"
        "  虚线序列 (动态数组) — 虚线序列\n"
        "  起始方向 (相对方向) — 起始方向\n"
        "  标识 (str) — 中枢标识前缀"
    },
    {
        "分析", (PyCFunction) Py_中枢_分析,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "中枢.分析(虚线序列, 中枢序列, 跳过首部, 标识)\n\n"
        "在虚线序列上运行中枢分析。\n\n"
        "参数:\n"
        "  虚线序列 (动态数组) — 笔或线段的动态数组\n"
        "  中枢序列 (动态数组) — 输出中枢将追加到此数组\n"
        "  跳过首部 (bool) — 是否跳过第一个虚线\n"
        "  标识 (str) — 中枢标识前缀"
    },
    {
        "向中枢序列尾部添加", (PyCFunction) Py_中枢_向中枢序列尾部添加,
        METH_CLASS | METH_VARARGS,
        "中枢.向中枢序列尾部添加(中枢序列, 待添加中枢)\n\n"
        "向中枢序列尾部添加中枢（含序号自动分配和单调性校验）。\n\n"
        "参数:\n"
        "  中枢序列 (动态数组) — 中枢序列\n"
        "  待添加中枢 (中枢) — 待添加的中枢"
    },
    {
        "从中枢序列尾部弹出", (PyCFunction) Py_中枢_从中枢序列尾部弹出,
        METH_CLASS | METH_VARARGS,
        "中枢.从中枢序列尾部弹出(中枢序列, 待弹出中枢) -> 中枢|None\n\n"
        "如果中枢序列末尾为待弹出中枢，则弹出并返回。\n\n"
        "参数:\n"
        "  中枢序列 (动态数组) — 中枢序列\n"
        "  待弹出中枢 (中枢) — 待弹出的中枢"
    },
    {
        "获取数据文本", (PyCFunction) Py_中枢_获取数据文本, METH_NOARGS,
        "中枢.获取数据文本() -> str"
    },
    {NULL}
};

static PyObject *Py_中枢_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
    中枢 *h = (中枢 *) self->ptr;
    char 缓冲[5120];
    中枢_到字符(h, 缓冲, sizeof(缓冲));
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
    .tp_doc = "中枢 — 三个连续笔/线段的重叠区域。\n\n"
    "属性:\n"
    "  序号, 标识, 级别, 方向, 高, 低, 高高, 低低\n"
    "  文, 武, 离开段, 图表标题\n"
    "  第三买卖线, 本级_第三买卖线, 基础序列\n\n"
    "实例方法:\n"
    "  中枢.添加虚线(实线)\n"
    "  中枢.完整性(虚实='合') → bool\n"
    "  中枢.当前状态() → str\n"
    "  中枢.获取序列() → list[虚线]\n"
    "  中枢.获取扩展中枢(扩展中枢, 配置)\n"
    "  中枢.校验合法性(序列, 中枢序列) → bool\n"
    "  中枢.设置第三买卖线(线)\n\n"
    "类方法:\n"
    "  中枢.基础检查(左, 中, 右) → bool\n"
    "  中枢.创建(左, 中, 右, 级别, 标识='') → 中枢\n"
    "  中枢.从序列中获取中枢(虚线序列, 起始方向, 标识) → 中枢|None\n"
    "  中枢.分析(虚线序列, 中枢序列, 跳过首部, 标识)\n"
    "  中枢.向中枢序列尾部添加(中枢序列, 待添加中枢)\n"
    "  中枢.从中枢序列尾部弹出(中枢序列, 待弹出中枢) → 中枢|None",
};


/* ================================================================
 *  缠论配置 (Config) type
 * ================================================================ */

/* Field name tables for string-keyed dispatch */

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
    } else if (strcmp(field, "缠K合并替换") == 0) {
        c->缠K合并替换 = value;
    }
    else if (strcmp(field, "笔内相同终点取舍") == 0) {
        c->笔内相同终点取舍 = value;
    }
    else if (strcmp(field, "笔内起始分型包含整笔") == 0) {
        c->笔内起始分型包含整笔 = value;
    }
    else if (strcmp(field, "笔内起始分型包含整笔_包括右") == 0) {
        c->笔内起始分型包含整笔_包括右 = value;
    }
    else if (strcmp(field, "笔内原始K线包含整笔") == 0) {
        c->笔内原始K线包含整笔 = value;
    }
    else if (strcmp(field, "笔次级成笔") == 0) {
        c->笔次级成笔 = value;
    }
    else if (strcmp(field, "笔弱化") == 0) {
        c->笔弱化 = value;
    }
    else if (strcmp(field, "线段_非缺口下穿刺") == 0) {
        c->线段_非缺口下穿刺 = value;
    }
    else if (strcmp(field, "线段_特征序列忽视老阴老阳") == 0) {
        c->线段_特征序列忽视老阴老阳 = value;
    }
    else if (strcmp(field, "线段_缺口后紧急修正") == 0) {
        c->线段_缺口后紧急修正 = value;
    }
    else if (strcmp(field, "线段_修正") == 0) {
        c->线段_修正 = value;
    }
    else if (strcmp(field, "线段内部中枢图显") == 0) {
        c->线段内部中枢图显 = value;
    }
    else if (strcmp(field, "扩展线段_当下分析") == 0) {
        c->扩展线段_当下分析 = value;
    }
    else if (strcmp(field, "分析笔") == 0) {
        c->分析笔 = value;
    }
    else if (strcmp(field, "分析线段") == 0) {
        c->分析线段 = value;
    }
    else if (strcmp(field, "分析扩展线段") == 0) {
        c->分析扩展线段 = value;
    }
    else if (strcmp(field, "分析笔中枢") == 0) {
        c->分析笔中枢 = value;
    }
    else if (strcmp(field, "分析线段中枢") == 0) {
        c->分析线段中枢 = value;
    }
    else if (strcmp(field, "计算指标") == 0) {
        c->计算指标 = value;
    }
    else if (strcmp(field, "图表展示") == 0) {
        c->图表展示 = value;
    }
    else if (strcmp(field, "推送K线") == 0) {
        c->推送K线 = value;
    }
    else if (strcmp(field, "推送笔") == 0) {
        c->推送笔 = value;
    }
    else if (strcmp(field, "推送线段") == 0) {
        c->推送线段 = value;
    }
    else if (strcmp(field, "推送中枢") == 0) {
        c->推送中枢 = value;
    }
    else if (strcmp(field, "图表展示_笔") == 0) {
        c->图表展示_笔 = value;
    }
    else if (strcmp(field, "图表展示_线段") == 0) {
        c->图表展示_线段 = value;
    }
    else if (strcmp(field, "图表展示_扩展线段") == 0) {
        c->图表展示_扩展线段 = value;
    }
    else if (strcmp(field, "图表展示_扩展线段_线段") == 0) {
        c->图表展示_扩展线段_线段 = value;
    }
    else if (strcmp(field, "图表展示_线段_线段") == 0) {
        c->图表展示_线段_线段 = value;
    }
    else if (strcmp(field, "图表展示_中枢_笔") == 0) {
        c->图表展示_中枢_笔 = value;
    }
    else if (strcmp(field, "图表展示_中枢_线段") == 0) {
        c->图表展示_中枢_线段 = value;
    }
    else if (strcmp(field, "图表展示_中枢_扩展线段") == 0) {
        c->图表展示_中枢_扩展线段 = value;
    }
    else if (strcmp(field, "图表展示_中枢_扩展线段_线段") == 0) {
        c->图表展示_中枢_扩展线段_线段 = value;
    }
    else if (strcmp(field, "图表展示_中枢_线段_线段") == 0) {
        c->图表展示_中枢_线段_线段 = value;
    }
    else if (strcmp(field, "图表展示_中枢_线段内部") == 0) {
        c->图表展示_中枢_线段内部 = value;
    }
    else if (strcmp(field, "买卖点激进识别") == 0) {
        c->买卖点激进识别 = value;
    }
    else if (strcmp(field, "买卖点与MACD柱强相关") == 0) {
        c->买卖点与MACD柱强相关 = value;
    }
    else if (strcmp(field, "买卖点_指标匹配_MACD") == 0) {
        c->买卖点_指标匹配_MACD = value;
    }
    else if (strcmp(field, "买卖点_指标匹配_KDJ") == 0) {
        c->买卖点_指标匹配_KDJ = value;
    }
    else if (strcmp(field, "买卖点_指标匹配_RSI") == 0) {
        c->买卖点_指标匹配_RSI = value;
    }
    else if (strcmp(field, "买卖点_峰值条件") == 0) {
        c->买卖点_峰值条件 = value;
    }
    else if (strcmp(field, "买卖点_计算线段BSP1") == 0) {
        c->买卖点_计算线段BSP1 = value;
    }
    else if (strcmp(field, "买卖点_处理BSP2") == 0) {
        c->买卖点_处理BSP2 = value;
    }
    else if (strcmp(field, "买卖点_计算线段BSP3") == 0) {
        c->买卖点_计算线段BSP3 = value;
    }
    else if (strcmp(field, "买卖点_依赖T1") == 0) {
        c->买卖点_依赖T1 = value;
    }
    else if (strcmp(field, "买卖点_调试输出") == 0) {
        c->买卖点_调试输出 = value;
    }
    else if (strcmp(field, "线段内部背驰_MACD") == 0) {
        c->线段内部背驰_MACD = value;
    }
    else if (strcmp(field, "线段内部背驰_斜率") == 0) {
        c->线段内部背驰_斜率 = value;
    }
    else if (strcmp(field, "线段内部背驰_测度") == 0) {
        c->线段内部背驰_测度 = value;
    }
    else {
        return false;
    }
    return true;
}

static bool Py_配置_获取_bool字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "缠K合并替换") == 0) {
        return c->缠K合并替换;
    }
    else if (strcmp(field, "笔内相同终点取舍") == 0) {
        return c->笔内相同终点取舍;
    }
    else if (strcmp(field, "笔内起始分型包含整笔") == 0) {
        return c->笔内起始分型包含整笔;
    }
    else if (strcmp(field, "笔内起始分型包含整笔_包括右") == 0) {
        return c->笔内起始分型包含整笔_包括右;
    }
    else if (strcmp(field, "笔内原始K线包含整笔") == 0) {
        return c->笔内原始K线包含整笔;
    }
    else if (strcmp(field, "笔次级成笔") == 0) {
        return c->笔次级成笔;
    }
    else if (strcmp(field, "笔弱化") == 0) {
        return c->笔弱化;
    }
    else if (strcmp(field, "线段_非缺口下穿刺") == 0) {
        return c->线段_非缺口下穿刺;
    }
    else if (strcmp(field, "线段_特征序列忽视老阴老阳") == 0) {
        return c->线段_特征序列忽视老阴老阳;
    }
    else if (strcmp(field, "线段_缺口后紧急修正") == 0) {
        return c->线段_缺口后紧急修正;
    }
    else if (strcmp(field, "线段_修正") == 0) {
        return c->线段_修正;
    }
    else if (strcmp(field, "线段内部中枢图显") == 0) {
        return c->线段内部中枢图显;
    }
    else if (strcmp(field, "扩展线段_当下分析") == 0) {
        return c->扩展线段_当下分析;
    }
    else if (strcmp(field, "分析笔") == 0) {
        return c->分析笔;
    }
    else if (strcmp(field, "分析线段") == 0) {
        return c->分析线段;
    }
    else if (strcmp(field, "分析扩展线段") == 0) {
        return c->分析扩展线段;
    }
    else if (strcmp(field, "分析笔中枢") == 0) {
        return c->分析笔中枢;
    }
    else if (strcmp(field, "分析线段中枢") == 0) {
        return c->分析线段中枢;
    }
    else if (strcmp(field, "计算指标") == 0) {
        return c->计算指标;
    }
    else if (strcmp(field, "图表展示") == 0) {
        return c->图表展示;
    }
    else if (strcmp(field, "推送K线") == 0) {
        return c->推送K线;
    }
    else if (strcmp(field, "推送笔") == 0) {
        return c->推送笔;
    }
    else if (strcmp(field, "推送线段") == 0) {
        return c->推送线段;
    }
    else if (strcmp(field, "推送中枢") == 0) {
        return c->推送中枢;
    }
    else if (strcmp(field, "图表展示_笔") == 0) {
        return c->图表展示_笔;
    }
    else if (strcmp(field, "图表展示_线段") == 0) {
        return c->图表展示_线段;
    }
    else if (strcmp(field, "图表展示_扩展线段") == 0) {
        return c->图表展示_扩展线段;
    }
    else if (strcmp(field, "图表展示_扩展线段_线段") == 0) {
        return c->图表展示_扩展线段_线段;
    }
    else if (strcmp(field, "图表展示_线段_线段") == 0) {
        return c->图表展示_线段_线段;
    }
    else if (strcmp(field, "图表展示_中枢_笔") == 0) {
        return c->图表展示_中枢_笔;
    }
    else if (strcmp(field, "图表展示_中枢_线段") == 0) {
        return c->图表展示_中枢_线段;
    }
    else if (strcmp(field, "图表展示_中枢_扩展线段") == 0) {
        return c->图表展示_中枢_扩展线段;
    }
    else if (strcmp(field, "图表展示_中枢_扩展线段_线段") == 0) {
        return c->图表展示_中枢_扩展线段_线段;
    }
    else if (strcmp(field, "图表展示_中枢_线段_线段") == 0) {
        return c->图表展示_中枢_线段_线段;
    }
    else if (strcmp(field, "图表展示_中枢_线段内部") == 0) {
        return c->图表展示_中枢_线段内部;
    }
    else if (strcmp(field, "买卖点激进识别") == 0) {
        return c->买卖点激进识别;
    }
    else if (strcmp(field, "买卖点与MACD柱强相关") == 0) {
        return c->买卖点与MACD柱强相关;
    }
    else if (strcmp(field, "买卖点_指标匹配_MACD") == 0) {
        return c->买卖点_指标匹配_MACD;
    }
    else if (strcmp(field, "买卖点_指标匹配_KDJ") == 0) {
        return c->买卖点_指标匹配_KDJ;
    }
    else if (strcmp(field, "买卖点_指标匹配_RSI") == 0) {
        return c->买卖点_指标匹配_RSI;
    }
    else if (strcmp(field, "买卖点_峰值条件") == 0) {
        return c->买卖点_峰值条件;
    }
    else if (strcmp(field, "买卖点_计算线段BSP1") == 0) {
        return c->买卖点_计算线段BSP1;
    }
    else if (strcmp(field, "买卖点_处理BSP2") == 0) {
        return c->买卖点_处理BSP2;
    }
    else if (strcmp(field, "买卖点_计算线段BSP3") == 0) {
        return c->买卖点_计算线段BSP3;
    }
    else if (strcmp(field, "买卖点_依赖T1") == 0) {
        return c->买卖点_依赖T1;
    }
    else if (strcmp(field, "买卖点_调试输出") == 0) {
        return c->买卖点_调试输出;
    }
    else if (strcmp(field, "线段内部背驰_MACD") == 0) {
        return c->线段内部背驰_MACD;
    }
    else if (strcmp(field, "线段内部背驰_斜率") == 0) {
        return c->线段内部背驰_斜率;
    }
    else if (strcmp(field, "线段内部背驰_测度") == 0) {
        return c->线段内部背驰_测度;
    }
    else {
        return false;
    }
}

static bool Py_配置_设置_int字段(缠论配置 *c, const char *field, int value) {
    if (0) {
    } else if (strcmp(field, "笔内元素数量") == 0) {
        c->笔内元素数量 = value;
    }
    else if (strcmp(field, "笔弱化_原始数量") == 0) {
        c->笔弱化_原始数量 = value;
    }
    else if (strcmp(field, "买卖点偏移") == 0) {
        c->买卖点偏移 = value;
    }
    else if (strcmp(field, "买卖点_T2S_最大层级") == 0) {
        c->买卖点_T2S_最大层级 = value;
    }
    else if (strcmp(field, "平滑异同移动平均线_快线周期") == 0) {
        c->平滑异同移动平均线_快线周期 = value;
    }
    else if (strcmp(field, "平滑异同移动平均线_慢线周期") == 0) {
        c->平滑异同移动平均线_慢线周期 = value;
    }
    else if (strcmp(field, "平滑异同移动平均线_信号周期") == 0) {
        c->平滑异同移动平均线_信号周期 = value;
    }
    else if (strcmp(field, "相对强弱指数_周期") == 0) {
        c->相对强弱指数_周期 = value;
    }
    else if (strcmp(field, "相对强弱指数_移动平均线周期") == 0) {
        c->相对强弱指数_移动平均线周期 = value;
    }
    else if (strcmp(field, "随机指标_RSV周期") == 0) {
        c->随机指标_RSV周期 = value;
    }
    else if (strcmp(field, "随机指标_K值平滑周期") == 0) {
        c->随机指标_K值平滑周期 = value;
    }
    else if (strcmp(field, "随机指标_D值平滑周期") == 0) {
        c->随机指标_D值平滑周期 = value;
    }
    else {
        return false;
    }
    return true;
}

static int Py_配置_获取int字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "笔内元素数量") == 0) {
        return c->笔内元素数量;
    }
    else if (strcmp(field, "笔弱化_原始数量") == 0) {
        return c->笔弱化_原始数量;
    }
    else if (strcmp(field, "买卖点偏移") == 0) {
        return c->买卖点偏移;
    }
    else if (strcmp(field, "买卖点_T2S_最大层级") == 0) {
        return c->买卖点_T2S_最大层级;
    }
    else if (strcmp(field, "平滑异同移动平均线_快线周期") == 0) {
        return c->平滑异同移动平均线_快线周期;
    }
    else if (strcmp(field, "平滑异同移动平均线_慢线周期") == 0) {
        return c->平滑异同移动平均线_慢线周期;
    }
    else if (strcmp(field, "平滑异同移动平均线_信号周期") == 0) {
        return c->平滑异同移动平均线_信号周期;
    }
    else if (strcmp(field, "相对强弱指数_周期") == 0) {
        return c->相对强弱指数_周期;
    }
    else if (strcmp(field, "相对强弱指数_移动平均线周期") == 0) {
        return c->相对强弱指数_移动平均线周期;
    }
    else if (strcmp(field, "随机指标_RSV周期") == 0) {
        return c->随机指标_RSV周期;
    }
    else if (strcmp(field, "随机指标_K值平滑周期") == 0) {
        return c->随机指标_K值平滑周期;
    }
    else if (strcmp(field, "随机指标_D值平滑周期") == 0) {
        return c->随机指标_D值平滑周期;
    }
    else {
        return 0;
    }
}

static bool Py_配置_设置_double字段(缠论配置 *c, const char *field, double value) {
    if (0) {
    } else if (strcmp(field, "相对强弱指数_超买阈值") == 0) {
        c->相对强弱指数_超买阈值 = value;
    }
    else if (strcmp(field, "相对强弱指数_超卖阈值") == 0) {
        c->相对强弱指数_超卖阈值 = value;
    }
    else if (strcmp(field, "随机指标_超买阈值") == 0) {
        c->随机指标_超买阈值 = value;
    }
    else if (strcmp(field, "随机指标_超卖阈值") == 0) {
        c->随机指标_超卖阈值 = value;
    }
    else if (strcmp(field, "买卖点错过误差值") == 0) {
        c->买卖点错过误差值 = value;
    }
    else if (strcmp(field, "买卖点_背离率") == 0) {
        c->买卖点_背离率 = value;
    }
    else if (strcmp(field, "买卖点_T2_回调阈值") == 0) {
        c->买卖点_T2_回调阈值 = value;
    }
    else {
        return false;
    }
    return true;
}

static double Py_配置_获取_double字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "相对强弱指数_超买阈值") == 0) {
        return c->相对强弱指数_超买阈值;
    }
    else if (strcmp(field, "相对强弱指数_超卖阈值") == 0) {
        return c->相对强弱指数_超卖阈值;
    }
    else if (strcmp(field, "随机指标_超买阈值") == 0) {
        return c->随机指标_超买阈值;
    }
    else if (strcmp(field, "随机指标_超卖阈值") == 0) {
        return c->随机指标_超卖阈值;
    }
    else if (strcmp(field, "买卖点错过误差值") == 0) {
        return c->买卖点错过误差值;
    }
    else if (strcmp(field, "买卖点_背离率") == 0) {
        return c->买卖点_背离率;
    }
    else if (strcmp(field, "买卖点_T2_回调阈值") == 0) {
        return c->买卖点_T2_回调阈值;
    }
    else {
        return 0.0;
    }
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
    } else {
        return false;
    }
    return true;
}

static const char *Py_配置_获取_string字段(缠论配置 *c, const char *field) {
    if (0) {
    } else if (strcmp(field, "手动终止") == 0) {
        return c->手动终止;
    }
    else if (strcmp(field, "指标计算方式") == 0) {
        return c->指标计算方式;
    }
    else if (strcmp(field, "买卖点_指标模式") == 0) {
        return c->买卖点_指标模式;
    }
    else if (strcmp(field, "买卖点_计算方式") == 0) {
        return c->买卖点_计算方式;
    }
    else if (strcmp(field, "买卖点_中枢来源") == 0) {
        return c->买卖点_中枢来源;
    }
    else if (strcmp(field, "线段内部背驰_模式") == 0) {
        return c->线段内部背驰_模式;
    }
    else if (strcmp(field, "标识") == 0) {
        return c->标识;
    }
    else if (strcmp(field, "加载文件路径") == 0) {
        return c->加载文件路径;
    }
    else {
        return "";
    }
}

/* -- Config type -- */

static int Py_缠论配置_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    ChanObject *py_对象 = (ChanObject *) self;
    static char *kwnames[] = {"no_push", NULL};
    int no_push = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|p", kwnames, &no_push)) {
        return -1;
    }

    if (no_push) {
        py_对象->ptr = 缠论配置_不推送();
    }
    else {
        py_对象->ptr = 缠论配置_新建();
    }

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
    if (!field) {
        return NULL;
    }

    /* Check each category */
    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyBool_FromLong(Py_配置_获取_bool字段(c, field));
        }
    }
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyLong_FromLong(Py_配置_获取int字段(c, field));
        }
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyFloat_FromDouble(Py_配置_获取_double字段(c, field));
        }
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyUnicode_FromString(Py_配置_获取_string字段(c, field));
        }
    }
    PyErr_Format(PyExc_KeyError, "unknown config field: %s", field);
    return NULL;
}

static int Py_缠论配置_设置元素(PyObject *self, PyObject *key, PyObject *value) {
    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    const char *field = PyUnicode_AsUTF8(key);
    if (!field) {
        return -1;
    }

    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            int v = PyObject_IsTrue(value);
            if (v < 0) {
                return -1;
            }
            Py_配置_设置_bool字段(c, field, (bool) v);
            return 0;
        }
    }
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            long v = PyLong_AsLong(value);
            if (v == -1 && PyErr_Occurred()) {
                return -1;
            }
            Py_配置_设置_int字段(c, field, (int) v);
            return 0;
        }
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            double v = PyFloat_AsDouble(value);
            if (v == -1.0 && PyErr_Occurred()) {
                return -1;
            }
            Py_配置_设置_double字段(c, field, v);
            return 0;
        }
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            const char *v = PyUnicode_AsUTF8(value);
            if (!v) {
                return -1;
            }
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
    if (!field) {
        return NULL;
    }

    /* Check if it's a 有效性 config field */
    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyBool_FromLong(Py_配置_获取_bool字段(c, field));
        }
    }
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyLong_FromLong(Py_配置_获取int字段(c, field));
        }
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyFloat_FromDouble(Py_配置_获取_double字段(c, field));
        }
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            return PyUnicode_FromString(Py_配置_获取_string字段(c, field));
        }
    }
    /* Fall back to standard attribute lookup */
    return PyObject_GenericGetAttr(self, name);
}

static int Py_缠论配置_设置属性(PyObject *self, PyObject *name, PyObject *value) {
    const char *field = PyUnicode_AsUTF8(name);
    if (!field) {
        return -1;
    }
    if (field[0] == '_') {
        return PyObject_GenericSetAttr(self, name, value);
    }

    缠论配置 *c = (缠论配置 *) ((ChanObject *) self)->ptr;
    for (const char **p = CONFIG_BOOL_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            int v = PyObject_IsTrue(value);
            if (v < 0) {
                return -1;
            }
            Py_配置_设置_bool字段(c, field, (bool) v);
            return 0;
        }
    }
    /* Try int, double, string field tables... */
    for (const char **p = CONFIG_INT_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            long v = PyLong_AsLong(value);
            if (v == -1 && PyErr_Occurred()) {
                return -1;
            }
            Py_配置_设置_int字段(c, field, (int) v);
            return 0;
        }
    }
    for (const char **p = CONFIG_DOUBLE_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            double v = PyFloat_AsDouble(value);
            if (v == -1.0 && PyErr_Occurred()) {
                return -1;
            }
            Py_配置_设置_double字段(c, field, v);
            return 0;
        }
    }
    for (const char **p = CONFIG_STRING_FIELDS; *p; p++) {
        if (strcmp(field, *p) == 0) {
            const char *v = PyUnicode_AsUTF8(value);
            if (!v) {
                return -1;
            }
            Py_配置_设置_string字段(c, field, v);
            return 0;
        }
    }
    return PyObject_GenericSetAttr(self, name, value);
}

static PyObject *Py_缠论配置_repr(ChanObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
    char 缓冲[64];
    snprintf(缓冲, sizeof(缓冲), "<缠论配置 at %p>", self->ptr);
    return PyUnicode_FromString(缓冲);
}

static PyObject *Py_缠论配置_不推送(PyObject *py_类, PyObject *Py_UNUSED(py_忽略)) {
    void *ptr = 缠论配置_不推送();
    if (!ptr) {
        return PyErr_NoMemory();
    }
    return Py_制作_拥有(&ChanConfig_Type, ptr);
}

static PyObject *Py_缠论配置___dir__(PyObject *self, PyObject *Py_UNUSED(py_忽略)) {
    /* Build a list of all attributes: standard ones + config field names */
    PyObject *list = PyList_New(0);
    if (!list) {
        return NULL;
    }

    /* Standard attributes: methods, getsets, members from the type */
    PyTypeObject *tp = Py_TYPE(self);
    for (PyMethodDef *m = tp->tp_methods; m && m->ml_name; m++) {
        PyList_Append(list, PyUnicode_FromString(m->ml_name));
    }
    for (PyGetSetDef *g = tp->tp_getset; g && g->name; g++) {
        PyList_Append(list, PyUnicode_FromString(g->name));
    }
    for (PyMemberDef *mb = tp->tp_members; mb && mb->name; mb++) {
        PyList_Append(list, PyUnicode_FromString(mb->name));
    }

    /* Config field names from the four category arrays */
    static const char **all_fields[] = {
        CONFIG_BOOL_FIELDS, CONFIG_INT_FIELDS,
        CONFIG_DOUBLE_FIELDS, CONFIG_STRING_FIELDS, NULL
    };
    for (const char ***cat = all_fields; *cat; cat++)
        for (const char **p = *cat; *p; p++) {
            PyList_Append(list, PyUnicode_FromString(*p));
        }

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
    .tp_doc = "缠论配置(no_push=False)\n\n"
    "缠论分析流水线的配置，支持字典式读写。\n\n"
    "参数:\n"
    "  no_push (bool) — True 时关闭所有推送开关，默认 False\n\n"
    "属性访问（配置项较多，完整列表见 chan.h 缠论配置 结构体）：\n"
    "  config['分析笔'] = True              # 布尔项\n"
    "  config['笔内元素数量'] = 5            # 整数项\n"
    "  config['超买阈值'] = 80.0            # 浮点项\n"
    "  config['标识'] = 'my_kline'          # 字符串项\n\n"
    "典型用法:\n"
    "    config = 缠论配置(no_push=True)\n"
    "    config['分析笔'] = True\n"
    "    obs = 观察者.读取数据文件('data.nb', config)",
};


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
    *py_输出 = (K线 **) da->arr->数据;
    *输出长度 = da->arr->长度;
    return true;
}

static PyObject *Py_背驰分析_斜率背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", NULL};
    PyObject *进入对象, *离开对象;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象)) {
        return NULL;
    }
    bool r = 背驰分析_斜率背驰((虚线 *) ((ChanObject *) 进入对象)->ptr,
                                       (虚线 *) ((ChanObject *) 离开对象)->ptr);
    return PyBool_FromLong(r);
}

static PyObject *Py_背驰分析_测度背驰(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"进入段", "离开段", NULL};
    PyObject *进入对象, *离开对象;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!", kwnames,
                                     &DashLine_Type, &进入对象,
                                     &DashLine_Type, &离开对象)) {
        return NULL;
    }
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
                                     &method)) {
        return NULL;
    }
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) {
        return NULL;
    }
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
                                     &DynArray_Type, &py_普K序列)) {
        return NULL;
    }
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) {
        return NULL;
    }
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
                                     &DynArray_Type, &py_普K序列)) {
        return NULL;
    }
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) {
        return NULL;
    }
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
                                     &ChanConfig_Type, &py_配置)) {
        return NULL;
    }
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) {
        return NULL;
    }
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
                                     &DynArray_Type, &py_普K序列)) {
        return NULL;
    }
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) {
        return NULL;
    }
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
                                     &模式字符串)) {
        return NULL;
    }
    K线 **klines;
    size_t k线长度;
    if (!Py_背驰分析_提取K线(py_普K序列, &klines, &k线长度)) {
        return NULL;
    }
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

/* ================================================================
 *  K线合成器 (KLineSynthesizer) type
 * ================================================================ */

typedef struct {
    ChanObject base;
    PyObject *py_回调; /* strong ref to Python callable */
} KLineSynthesizerObject;

static void _合成器_回调桥接(void *上下文, const char *信号类型, const char *标识,
                                    int 周期, K线 *完成K线) {
    PyObject *cb = (PyObject *)上下文;
    if (!cb || cb == Py_None) {
        return;
    }

    PyObject *py_kline = Py_制作_借用(&KLine_Type, 完成K线);
    if (!py_kline) {
        return;
    }

    PyObject *kwargs = PyDict_New();
    if (!kwargs) {
        Py_DECREF(py_kline);
        return;
    }
    PyDict_SetItemString(kwargs, "信号类型", PyUnicode_FromString(信号类型));
    PyDict_SetItemString(kwargs, "标识", PyUnicode_FromString(标识));
    PyDict_SetItemString(kwargs, "周期", PyLong_FromLong(周期));
    PyDict_SetItemString(kwargs, "完成K线", py_kline);

    PyObject *result = PyObject_Call(cb, PyTuple_New(0), kwargs);
    Py_XDECREF(result);
    Py_DECREF(kwargs);
    Py_DECREF(py_kline);
}

static void Py_合成器_释放内存(KLineSynthesizerObject *self) {
    Py_XDECREF(self->py_回调);
    self->py_回调 = NULL;
    if (self->base.owns && self->base.ptr) {
        释放K线合成器((K线合成器 *)self->base.ptr);
        self->base.ptr = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static int Py_合成器_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"标识", "周期组", "事件回调", NULL};
    const char *标识;
    PyObject *py_周期组;
    PyObject *py_回调 = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "sO|O", kwnames, &标识, &py_周期组, &py_回调)) {
        return -1;
    }

    if (py_回调 != Py_None && !PyCallable_Check(py_回调)) {
        PyErr_SetString(PyExc_TypeError, "事件回调 必须为可调用对象或 None");
        return -1;
    }

    /* 提取周期组为 int 数组 */
    Py_ssize_t n = PyObject_Length(py_周期组);
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        PyErr_SetString(PyExc_ValueError, "周期组 不能为空");
        return -1;
    }

    int *周期组 = PyMem_Malloc(n * sizeof(int));
    if (!周期组) {
        PyErr_NoMemory();
        return -1;
    }

    PyObject *iter = PyObject_GetIter(py_周期组);
    if (!iter) {
        PyMem_Free(周期组);
        return -1;
    }

    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyIter_Next(iter);
        if (!item) {
            Py_DECREF(iter);
            PyMem_Free(周期组);
            return -1;
        }
        周期组[i] = (int)PyLong_AsLong(item);
        Py_DECREF(item);
        if (PyErr_Occurred()) {
            Py_DECREF(iter);
            PyMem_Free(周期组);
            return -1;
        }
    }
    Py_DECREF(iter);

    KLineSynthesizerObject *syn_obj = (KLineSynthesizerObject *)self;
    Py_XDECREF(syn_obj->py_回调);
    syn_obj->py_回调 = py_回调;
    Py_INCREF(py_回调);

    K线合成器 *c_syn = K线合成器_新建(标识, 周期组, (size_t)n,
                           py_回调 != Py_None ? _合成器_回调桥接 : NULL,
                           py_回调 != Py_None ? (void *)py_回调 : NULL);
    PyMem_Free(周期组);

    if (!c_syn) {
        Py_DECREF(syn_obj->py_回调);
        syn_obj->py_回调 = NULL;
        return -1;
    }

    syn_obj->base.ptr = c_syn;
    syn_obj->base.owns = 1;
    return 0;
}

static PyObject *Py_合成器_设置事件回调(KLineSynthesizerObject *self, PyObject *py_回调) {
    if (py_回调 != Py_None && !PyCallable_Check(py_回调)) {
        PyErr_SetString(PyExc_TypeError, "事件回调 必须为可调用对象或 None");
        return NULL;
    }
    Py_XDECREF(self->py_回调);
    self->py_回调 = py_回调;
    Py_INCREF(py_回调);

    K线合成器 *c_syn = (K线合成器 *)self->base.ptr;
    c_syn->事件回调 = py_回调 != Py_None ? _合成器_回调桥接 : NULL;
    c_syn->回调上下文 = py_回调 != Py_None ? (void *)py_回调 : NULL;
    Py_RETURN_NONE;
}

static PyObject *Py_合成器_投喂K线(KLineSynthesizerObject *self, PyObject *py_普K) {
    K线 *c_kline = (K线 *)Py_解包(py_普K, &KLine_Type);
    if (!c_kline) {
        return NULL;
    }
    K线合成器_投喂K线((K线合成器 *)self->base.ptr, c_kline);
    Py_RETURN_NONE;
}

static PyObject *Py_合成器_投喂(KLineSynthesizerObject *self, PyObject *args) {
    long long 时间戳;
    double 开, 高, 低, 收, 量;
    if (!PyArg_ParseTuple(args, "Lddddd", &时间戳, &开, &高, &低, &收, &量)) {
        return NULL;
    }
    K线合成器 *c_syn = (K线合成器 *)self->base.ptr;
    K线 *普K = K线_创建普K(c_syn->标识, (time_t)时间戳, 开, 高, 低, 收, 量, 0, 0);
    if (!普K) {
        return PyErr_NoMemory();
    }
    K线合成器_投喂K线(c_syn, 普K);
    Py_RETURN_NONE;
}

static PyObject *Py_合成器_获取当前K线(KLineSynthesizerObject *self, PyObject *py_周期) {
    int 周期 = (int)PyLong_AsLong(py_周期);
    if (周期 == -1 && PyErr_Occurred()) {
        return NULL;
    }
    K线合成器 *c_syn = (K线合成器 *)self->base.ptr;
    K线 *k = K线合成器_获取当前K线(c_syn, 周期);
    return Py_制作_借用(&KLine_Type, k);
}

static PyObject *Py_合成器_获取_标识(KLineSynthesizerObject *self, void *c) {
    (void)c;
    K线合成器 *syn = (K线合成器 *)self->base.ptr;
    return PyUnicode_FromString(syn->标识);
}

static PyObject *Py_合成器_获取_周期组(KLineSynthesizerObject *self, void *c) {
    (void)c;
    K线合成器 *syn = (K线合成器 *)self->base.ptr;
    PyObject *list = PyList_New(syn->周期数量);
    if (!list) {
        return NULL;
    }
    for (size_t i = 0; i < syn->周期数量; i++) {
        PyList_SET_ITEM(list, i, PyLong_FromLong(syn->周期组[i]));
    }
    return list;
}

static PyObject *Py_合成器_repr(KLineSynthesizerObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *)self);
    if (custom) {
        return custom;
    }
    K线合成器 *syn = (K线合成器 *)self->base.ptr;
    if (!syn) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromFormat("<KLineSynthesizer '%s' periods=%zu>", syn->标识, syn->周期数量);
}

static PyGetSetDef KLineSynth_getset[] = {
    {"标识", (getter)Py_合成器_获取_标识, NULL, "合成器标识 (read-only)", NULL},
    {"周期组", (getter)Py_合成器_获取_周期组, NULL, "已排序的周期列表 (read-only)", NULL},
    {NULL}
};

static PyMethodDef KLineSynth_methods[] = {
    {   "投喂", (PyCFunction)Py_合成器_投喂, METH_VARARGS,
        "投喂(时间戳, 开, 高, 低, 收, 量)\n\n"
        "用 OHLCV 原始数据投喂合成器，内部创建 K线 后调用投喂K线。"
    },
    {   "投喂K线", (PyCFunction)Py_合成器_投喂K线, METH_O,
        "投喂K线(K线)\n\n"
        "投喂一根 K线，对每个周期执行时间戳对齐与合成。"
    },
    {   "设置事件回调", (PyCFunction)Py_合成器_设置事件回调, METH_O,
        "设置事件回调(回调)\n\n"
        "设置当 K 线完成时触发的回调函数。"
    },
    {   "获取当前K线", (PyCFunction)Py_合成器_获取当前K线, METH_O,
        "获取当前K线(周期) -> K线 | None\n\n"
        "获取指定周期当前正在合成的 K 线。"
    },
    {NULL}
};

static PyTypeObject KLineSynth_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.K线合成器",
    .tp_basicsize = sizeof(KLineSynthesizerObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor)Py_合成器_释放内存,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = Py_合成器_初始化,
    .tp_getset = KLineSynth_getset,
    .tp_methods = KLineSynth_methods,
    .tp_repr = (reprfunc)Py_合成器_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "K线合成器(标识, 周期组, 事件回调=None)\n\n"
    "将低周期 K 线合成为高周期 K 线。\n\n"
    "参数:\n"
    "  标识 (str) — 合成器标识，用于创建合成K线的标识字段\n"
    "  周期组 (list[int]) — 目标周期列表（秒），自动从小到大排序\n"
    "  事件回调 (callable|None) — 可选，K线完成时触发，签名:\n"
    "      回调(信号类型='K线完成', 标识=str, 周期=int, 完成K线=K线)\n\n"
    "实例方法:\n"
    "  合成器.投喂(时间戳, 开, 高, 低, 收, 量)\n"
    "  合成器.投喂K线(K线)\n"
    "  合成器.设置事件回调(回调)\n"
    "  合成器.获取当前K线(周期) -> K线 | None",
};

/* ================================================================
 *  立体分析器 (MultiLevelAnalyzer) type
 * ================================================================ */

typedef struct {
    ChanObject base;
    PyObject *py_配置;      /* keep-alive for 配置 */
    PyObject *py_配置组;    /* keep-alive for 配置组 dict */
} MLAObject;

static void Py_立体分析器_释放内存(MLAObject *self) {
    Py_XDECREF(self->py_配置);
    self->py_配置 = NULL;
    Py_XDECREF(self->py_配置组);
    self->py_配置组 = NULL;
    if (self->base.owns && self->base.ptr) {
        释放立体分析器((立体分析器 *) self->base.ptr);
        self->base.ptr = NULL;
        self->base.owns = 0;
    }
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static int Py_立体分析器_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"符号", "周期组", "配置", "配置组", NULL};
    const char *符号;
    PyObject *py_周期组;
    PyObject *py_配置 = NULL;
    PyObject *py_配置组 = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "sO|OO", kwnames,
                                     &符号, &py_周期组, &py_配置, &py_配置组)) {
        return -1;
    }

    Py_ssize_t n = PyObject_Length(py_周期组);
    if (n < 0) {
        return -1;
    }
    if (n < 2) {
        PyErr_SetString(PyExc_ValueError, "周期组至少需要2个周期");
        return -1;
    }
    if (n > K线合成器_最大周期数) {
        n = K线合成器_最大周期数;
    }

    int *周期组 = PyMem_Malloc(n * sizeof(int));
    if (!周期组) {
        PyErr_NoMemory();
        return -1;
    }

    PyObject *iter = PyObject_GetIter(py_周期组);
    if (!iter) {
        PyMem_Free(周期组);
        return -1;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyIter_Next(iter);
        if (!item) {
            Py_DECREF(iter);
            PyMem_Free(周期组);
            return -1;
        }
        周期组[i] = (int) PyLong_AsLong(item);
        Py_DECREF(item);
        if (PyErr_Occurred()) {
            Py_DECREF(iter);
            PyMem_Free(周期组);
            return -1;
        }
    }
    Py_DECREF(iter);

    缠论配置 *c_配置 = NULL;
    if (py_配置 && py_配置 != Py_None) {
        c_配置 = (缠论配置 *) Py_解包(py_配置, &ChanConfig_Type);
        if (!c_配置) {
            PyMem_Free(周期组);
            return -1;
        }
    }

    缠论配置 **c_配置组 = NULL;
    缠论配置 **配置组数组 = NULL;
    if (py_配置组 && py_配置组 != Py_None && PyDict_Check(py_配置组)) {
        配置组数组 = PyMem_Calloc(K线合成器_最大周期数, sizeof(缠论配置 *));
        if (!配置组数组) {
            PyMem_Free(周期组);
            PyErr_NoMemory();
            return -1;
        }
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(py_配置组, &pos, &key, &value)) {
            int 周期 = (int) PyLong_AsLong(key);
            if (周期 >= 0 && 周期 < K线合成器_最大周期数) {
                配置组数组[周期] = (缠论配置 *) Py_解包(value, &ChanConfig_Type);
            }
        }
        c_配置组 = 配置组数组;
    }

    立体分析器 *c_mla = 立体分析器_新建(符号, 周期组, (int) n, c_配置, c_配置组);
    PyMem_Free(周期组);
    if (配置组数组) {
        PyMem_Free(配置组数组);
    }

    if (!c_mla) {
        return -1;
    }

    MLAObject *mla = (MLAObject *) self;
    mla->base.ptr = c_mla;
    mla->base.owns = 1;

    if (py_配置) {
        Py_INCREF(py_配置);
        mla->py_配置 = py_配置;
    }
    if (py_配置组) {
        Py_INCREF(py_配置组);
        mla->py_配置组 = py_配置组;
    }

    return 0;
}

static PyObject *Py_立体分析器_投喂K线(MLAObject *self, PyObject *py_普K) {
    K线 *c_kline = (K线 *) Py_解包(py_普K, &KLine_Type);
    if (!c_kline) {
        return NULL;
    }
    立体分析器_投喂K线((立体分析器 *) self->base.ptr, c_kline);
    Py_RETURN_NONE;
}

static PyObject *Py_立体分析器_测试_保存数据(MLAObject *self, PyObject *Py_UNUSED(ignored)) {
    立体分析器_测试_保存数据((立体分析器 *) self->base.ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_立体分析器_获取_符号(MLAObject *self, void *c) {
    (void) c;
    立体分析器 *mla = (立体分析器 *) self->base.ptr;
    return PyUnicode_FromString(mla->符号);
}

static PyObject *Py_立体分析器_获取_周期组(MLAObject *self, void *c) {
    (void) c;
    立体分析器 *mla = (立体分析器 *) self->base.ptr;
    PyObject *list = PyList_New(mla->周期数量);
    if (!list) {
        return NULL;
    }
    for (int i = 0; i < mla->周期数量; i++) {
        PyList_SET_ITEM(list, i, PyLong_FromLong(mla->周期组[i]));
    }
    return list;
}

static PyObject *Py_立体分析器_repr(MLAObject *self) {
    PyObject *custom = _try_custom_repr((PyObject *) self);
    if (custom) {
        return custom;
    }
    立体分析器 *mla = (立体分析器 *) self->base.ptr;
    if (!mla) {
        Py_RETURN_NONE;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "<MultiLevelAnalyzer '%s' periods=%d>", mla->符号, mla->周期数量);
    return PyUnicode_FromString(buf);
}

static PyMethodDef MLA_methods[] = {
    {   "投喂K线", (PyCFunction) Py_立体分析器_投喂K线, METH_O,
        "投喂K线(普K)\n\n将输入周期的原始K线投喂到合成器，自动合成高周期并分发给各观察者。"
    },
    {   "测试_保存数据", (PyCFunction) Py_立体分析器_测试_保存数据, METH_NOARGS,
        "测试_保存数据()\n\n将各周期观察者的分析数据拆分保存到文件中。"
    },
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef MLA_getset[] = {
    {"符号", (getter) Py_立体分析器_获取_符号, NULL, "立体分析器的符号标识", NULL},
    {"周期组", (getter) Py_立体分析器_获取_周期组, NULL, "多周期列表", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyTypeObject MLA_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "chan_c99._core.立体分析器",
    .tp_basicsize = sizeof(MLAObject),
    .tp_base = &ChanObject_Type,
    .tp_dealloc = (destructor) Py_立体分析器_释放内存,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = Py_立体分析器_初始化,
    .tp_getset = MLA_getset,
    .tp_methods = MLA_methods,
    .tp_repr = (reprfunc) Py_立体分析器_repr,
    .tp_str = Py_通用_str,
    .tp_doc = "立体分析器(符号, 周期组, 配置=None, 配置组=None)\n\n"
    "多级别联立分析器。将输入周期K线自动合成到多周期，\n"
    "每个周期独立分析，各周期的缠论K线序列对齐到显示周期。\n\n"
    "参数:\n"
    "  符号 (str) — 交易对标识\n"
    "  周期组 (list[int]) — 从低到高的周期列表，第一个为输入周期\n"
    "  配置 (缠论配置|None) — 默认配置\n"
    "  配置组 (dict[int, 缠论配置]|None) — 按周期定制的配置字典",
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

/* ---- 动态数组视图工厂（非拥有，包装观察者内部数组） ---- */

static PyObject *Py_制作_动态数组视图(动态数组 *src, PyTypeObject *item_type) {
    DynArrayObject *da = (DynArrayObject *) Py_动态数组_new(&DynArray_Type, NULL, NULL);
    if (!da) {
        return NULL;
    }
    /* Release the default heap-allocated arr and use the borrowed pointer */
    弱引用_数组清除(da->arr);
    free(da->arr);
    da->arr = src;
    da->owns_elements = false;
    da->item_type = item_type;
    return (PyObject *) da;
}

/* ---- Observer sequence properties ---- */

#define OBS_SEQ_GETTER(cname, seq_field, item_type) \
    static PyObject *Py_观察者_获取_##cname(ObserverObject *self, void *c) { \
        (void)c; \
        观察者 *obs = (观察者 *) self->base.ptr; \
        return Py_制作_动态数组视图(&obs->seq_field, item_type); \
    }

OBS_SEQ_GETTER(raw_klines, 普通K线序列, &KLine_Type)
OBS_SEQ_GETTER(chan_klines, 缠论K线序列, &ChanKLine_Type)

static PyObject *Py_观察者_获取_base_chan_klines(ObserverObject *self, void *c) {
    (void)c;
    观察者 *obs = (观察者 *) self->base.ptr;
    return Py_制作_动态数组视图(obs->基础缠K序列, &ChanKLine_Type);
}

static int Py_观察者_设置_base_chan_klines(ObserverObject *self, PyObject *value, void *c) {
    (void)c;
    if (!PyObject_TypeCheck(value, &DynArray_Type)) {
        PyErr_SetString(PyExc_TypeError, "基础缠K序列 必须为动态数组");
        return -1;
    }
    观察者 *obs = (观察者 *) self->base.ptr;
    观察者_设置基础缠K序列(obs, ((DynArrayObject *)value)->arr);
    return 0;
}
OBS_SEQ_GETTER(fractals, 分型序列, &Fractal_Type)
OBS_SEQ_GETTER(strokes, 笔序列, &DashLine_Type)
OBS_SEQ_GETTER(stroke_hubs, 笔_中枢序列, &Hub_Type)
OBS_SEQ_GETTER(segments, 线段序列, &DashLine_Type)
OBS_SEQ_GETTER(hubs, 中枢序列, &Hub_Type)
OBS_SEQ_GETTER(ext_segments, 扩展线段序列, &DashLine_Type)
OBS_SEQ_GETTER(ext_hubs, 扩展中枢序列, &Hub_Type)
OBS_SEQ_GETTER(ext_segments_seg, 扩展线段序列_线段, &DashLine_Type)
OBS_SEQ_GETTER(ext_hubs_seg, 扩展中枢序列_线段, &Hub_Type)
OBS_SEQ_GETTER(seg_seg, 线段_线段序列, &DashLine_Type)
OBS_SEQ_GETTER(seg_hubs, 线段_中枢序列, &Hub_Type)
OBS_SEQ_GETTER(ext_seg_extseg, 扩展线段序列_扩展线段, &DashLine_Type)
OBS_SEQ_GETTER(ext_hubs_extseg, 扩展中枢序列_扩展线段, &Hub_Type)

#undef OBS_SEQ_GETTER
/* ---- Observer init / methods ---- */

static int Py_观察者_初始化(PyObject *self, PyObject *args, PyObject *kw) {
    ObserverObject *obs = (ObserverObject *) self;
    static char *kwnames[] = {"符号", "周期", "配置", NULL};
    const char *symbol;
    int period;
    PyObject *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "siO", kwnames,
                                     &symbol, &period, &py_配置)) {
        return -1;
    }

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
    if (!kptr) {
        return NULL;
    }
    观察者_增加原始K线((观察者 *) self->base.ptr, (K线 *) kptr);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_重置基础序列(ObserverObject *self, PyObject *Py_UNUSED(py_忽略)) {
    观察者_重置基础序列((观察者 *) self->base.ptr);
    Py_RETURN_NONE;
}

static PyObject *Py_观察者_读取数据文件(PyObject *py_类, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"文件路径", "配置", NULL};
    const char *path;
    PyObject *py_配置;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "sO", kwnames,
                                     &path, &py_配置)) {
        return NULL;
    }

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

static PyObject *Py_观察者_获取_符号(ObserverObject *self, void *c) {
    (void) c;
    return PyUnicode_FromString(((观察者 *) self->base.ptr)->符号);
}

static PyObject *Py_观察者_获取_周期(ObserverObject *self, void *c) {
    (void) c;
    return PyLong_FromLong(((观察者 *) self->base.ptr)->周期);
}

static PyObject *Py_观察者_获取_标识(ObserverObject *self, void *c) {
    (void) c;
    观察者 *obs = (观察者 *) self->base.ptr;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s:%d", obs->符号, obs->周期);
    return PyUnicode_FromString(buf);
}

static PyObject *Py_观察者_获取_配置(ObserverObject *self, void *c) {
    (void) c;
    Py_INCREF(self->config);
    return self->config;
}

static PyGetSetDef Observer_getset[] = {
    {"符号", (getter) Py_观察者_获取_符号, NULL, "交易对符号 (read-only)", NULL},
    {"周期", (getter) Py_观察者_获取_周期, NULL, "K线周期（秒）(read-only)", NULL},
    {"标识", (getter) Py_观察者_获取_标识, NULL, "Symbol:Period identifier", NULL},
    {"配置", (getter) Py_观察者_获取_配置, NULL, "缠论配置 instance (read-only)", NULL},
    {"普通K线序列", (getter) Py_观察者_获取_raw_klines, NULL, NULL, NULL},
    {"缠论K线序列", (getter) Py_观察者_获取_chan_klines, NULL, NULL, NULL},
    {"基础缠K序列", (getter) Py_观察者_获取_base_chan_klines, (setter) Py_观察者_设置_base_chan_klines, NULL, NULL},
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
    if (custom) {
        return custom;
    }
    if (!self->base.ptr) {
        return PyUnicode_FromString("<观察者 (released)>");
    }
    char 缓冲[64];
    snprintf(缓冲, sizeof(缓冲), "<观察者 at %p>", self->base.ptr);
    return PyUnicode_FromString(缓冲);
}

static PyObject *Py_观察者_测试_保存数据(ChanObject *self, PyObject *args, PyObject *kwargs) {
    const char *root = NULL;
    static char *kwlist[] = {"root", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", kwlist, &root)) {
        return NULL;
    }
    观察者_测试_保存数据((观察者 *) self->ptr, root);
    Py_RETURN_NONE;
}

static PyMethodDef Observer_methods[] = {
    {
        "增加原始K线", (PyCFunction) Py_观察者_增加原始K线, METH_O,
        "obs.增加原始K线(K线) — 喂入一根原始K线，触发增量分析流水线。"
    },
    {
        "测试_保存数据", (PyCFunction) Py_观察者_测试_保存数据,
        METH_VARARGS | METH_KEYWORDS,
        "obs.测试_保存数据(root=None) — 将所有序列数据按变量名导出为独立文本文件。\n\n"
        "参数:\n"
        "  root (str, 可选): 输出根目录，默认为当前工作目录。"
    },
    {
        "重置基础序列", (PyCFunction) Py_观察者_重置基础序列, METH_NOARGS,
        "obs.重置基础序列() — 清空所有序列数组，恢复到初始状态。"
    },
    {
        "读取数据文件", (PyCFunction) Py_观察者_读取数据文件,
        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
        "观察者.读取数据文件(文件路径, 配置) — 从 .nb 文件加载数据并运行完整分析。\n\n"
        "参数:\n"
        "  文件路径 (str) — .nb 二进制数据文件路径\n"
        "  配置 (缠论配置) — 分析流水线配置"
    },
    {
        "release", (PyCFunction) Py_观察者_释放方法, METH_NOARGS,
        "obs.release() — 显式释放 C 观察者及其管理的所有对象。"
    },
    {
        "__enter__", (PyCFunction) Py_观察者_进入, METH_NOARGS,
        "上下文管理器入口。"
    },
    {
        "__exit__", (PyCFunction) Py_观察者_退出, METH_VARARGS,
        "上下文管理器出口 — 释放 C 观察者。"
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
    .tp_doc = "观察者(符号, 周期, 配置)\n\n"
    "顶层协调器，持有所有序列数组，驱动缠论分析流水线。\n\n"
    "参数:\n"
    "  符号 (str) — 交易对标识，如 'btcusd'\n"
    "  周期 (int) — K线周期，单位秒，如 300\n"
    "  配置 (缠论配置) — 分析流水线配置\n\n"
    "实例方法:\n"
    "  观察者.增加原始K线(K线)\n"
    "  观察者.重置基础序列() — 清空所有序列数组\n"
    "类方法:\n"
    "  观察者.读取数据文件(文件路径, 配置) — 从 .nb 文件创建观察者\n\n"
    "属性:\n"
    "  标识 — 符号:周期\n"
    "  配置 — 缠论配置实例\n\n"
    "支持 with 语句自动释放:\n"
    "    with 观察者('btcusd', 300, config) as obs:\n"
    "        for k in klines:\n"
    "            obs.增加原始K线(k)\n"
    "        print(obs.笔序列)",
};

/* ================================================================
 *  Module-level utility functions (called from Python enums.py)
 * ================================================================ */

static PyObject *Py_相对方向_分析(PyObject *m, PyObject *args) {
    double 前高, 前低, 后高, 后低;
    if (!PyArg_ParseTuple(args, "dddd", &前高, &前低, &后高, &后低)) {
        return NULL;
    }
    return PyLong_FromLong(相对方向_分析(前高, 前低, 后高, 后低));
}

static PyObject *Py_相对方向_翻转(PyObject *m, PyObject *py_参数) {
    int 方向值 = PyLong_AsLong(py_参数);
    if (方向值 == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return PyLong_FromLong(相对方向_翻转((相对方向) 方向值));
}

static PyObject *Py_分型结构_分析(PyObject *m, PyObject *args, PyObject *kw) {
    static char *kwnames[] = {"左", "中", "右", "可以逆序包含", "忽视顺序包含", NULL};
    PyObject *左_obj, *中_obj, *右_obj;
    int 可以逆序包含 = 0, 忽视顺序包含 = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OOO|pp", kwnames,
                                     &左_obj, &中_obj, &右_obj,
                                     &可以逆序包含, &忽视顺序包含)) {
        return NULL;
    }

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
        if (PyErr_Occurred()) {
            return NULL;
        }
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
    if (释放全局内存池()) {
        Py_RETURN_TRUE;
    }
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
        "_设置相对方向类", Py__设置相对方向类, METH_VARARGS,
        "Cache the 相对方向 enum class for direction property getters. "
        "Called automatically by __init__.py."
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
    if (PyType_Ready(&ChanObject_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&KLine_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&ChanKLine_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Gap_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&MACD_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&RSI_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&KDJ_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Fractal_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&DashLine_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Stroke_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&SegFeature_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Segment_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Hub_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&DynArray_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&ChanConfig_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Observer_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&KLineSynth_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&MLA_Type) < 0) {
        return NULL;
    }
    if (PyType_Ready(&背驰分析_Type) < 0) {
        return NULL;
    }

    m = PyModule_Create(&_core_module);
    if (!m) {
        return NULL;
    }

    /* Add module-level functions */
    if (PyModule_AddFunctions(m, _core_functions) < 0) {
        return NULL;
    }

    /* Add types to module */
    Py_INCREF(&KLine_Type);
    PyModule_AddObject(m, "K线", (PyObject *) &KLine_Type);

    Py_INCREF(&ChanKLine_Type);
    PyModule_AddObject(m, "缠论K线", (PyObject *) &ChanKLine_Type);

    Py_INCREF(&Gap_Type);
    PyModule_AddObject(m, "缺口", (PyObject *) &Gap_Type);

    Py_INCREF(&MACD_Type);
    PyModule_AddObject(m, "MACD", (PyObject *) &MACD_Type);

    Py_INCREF(&RSI_Type);
    PyModule_AddObject(m, "RSI", (PyObject *) &RSI_Type);

    Py_INCREF(&KDJ_Type);
    PyModule_AddObject(m, "KDJ", (PyObject *) &KDJ_Type);

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

    Py_INCREF(&KLineSynth_Type);
    PyModule_AddObject(m, "K线合成器", (PyObject *) &KLineSynth_Type);

    Py_INCREF(&MLA_Type);
    PyModule_AddObject(m, "立体分析器", (PyObject *) &MLA_Type);

    Py_INCREF(&背驰分析_Type);
    PyModule_AddObject(m, "背驰分析", (PyObject *) &背驰分析_Type);

    return m;
}
