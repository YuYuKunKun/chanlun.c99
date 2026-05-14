"""
chan_c99._bindings — ctypes binding layer for the chan2c99 C library.

All C objects are managed via reference counting. Python wrapper objects
hold a C pointer and optionally own a reference:
  - Owned objects call chan_release() on __del__ (or explicit release())
  - Borrowed objects do NOT call chan_release() — the owning sequence holds the ref

Never call free() directly. Always use chan_release().

Class names follow chan.py Chinese naming:
  观察者, 缠论配置, K线, 缠论K线, 分型, 虚线, 笔, 线段, 线段特征, 中枢, 缺口
"""

import ctypes
import os
import sys
from ctypes import (
    c_void_p, c_char_p, c_size_t, c_int, c_double, c_long, c_bool,
    POINTER, byref, cast,
)
from typing import Optional, Type, TypeVar, Iterator, List

from .enums import 相对方向, 分型结构, ObjectType

# ================================================================
# Library loading
# ================================================================

_lib: Optional[ctypes.CDLL] = None
_lib_path: Optional[str] = None

T = TypeVar('T', bound='_ChanObject')


def _find_library() -> str:
    """Find the shared library. Checks in order:
    1. CHAN_C99_LIB environment variable
    2. libchan.so in the current directory and package directory
    3. chan_c99._core via setuptools (when installed via pip)
    """
    env = os.environ.get('CHAN_C99_LIB')
    if env:
        return env

    candidates = [
        os.path.join(os.path.dirname(__file__), '..', 'libchan.so'),
        os.path.join(os.path.dirname(__file__), '..', '_core.so'),
        os.path.join(os.getcwd(), 'libchan.so'),
    ]

    for path in candidates:
        if os.path.exists(path):
            return os.path.abspath(path)

    # Try setuptools-installed extension
    try:
        from . import _core
        return _core.__file__
    except ImportError:
        pass

    raise FileNotFoundError(
        "Cannot find chan_c99 shared library. "
        "Set CHAN_C99_LIB environment variable or build with: make -f Makefile"
    )


def load_library(path: Optional[str] = None) -> ctypes.CDLL:
    """Load the chan2c99 shared library and set up all function signatures."""
    global _lib, _lib_path

    if _lib is not None:
        return _lib

    lib_path = path or _find_library()
    _lib = ctypes.CDLL(lib_path)
    _lib_path = lib_path

    # --- OOM / fatal error ---
    _lib.chan_set_fatal_callback.argtypes = [c_void_p]

    # --- Memory debug ---
    _lib.chan_memory_summary.argtypes = []
    _lib.chan_memory_summary.restype = None
    _lib.chan_allocation_count.argtypes = [c_int]
    _lib.chan_allocation_count.restype = c_int
    _lib.chan_free_count.argtypes = [c_int]
    _lib.chan_free_count.restype = c_int
    _lib.chan_set_debug_mem.argtypes = [c_int]
    _lib.chan_set_debug_mem.restype = None

    # --- Config ---
    _lib.chan_config_new.argtypes = []
    _lib.chan_config_new.restype = c_void_p
    _lib.chan_config_new_no_push.argtypes = []
    _lib.chan_config_new_no_push.restype = c_void_p
    _lib.chan_config_release.argtypes = [c_void_p]
    _lib.chan_config_release.restype = None

    for _fn in ['chan_config_set_bool', 'chan_config_set_int',
                 'chan_config_set_double', 'chan_config_set_string']:
        _f = getattr(_lib, _fn); _f.argtypes = [c_void_p, c_char_p]; _f.restype = None
    for _fn in ['chan_config_set_bool', 'chan_config_set_int']:
        _f = getattr(_lib, _fn); _f.argtypes = [c_void_p, c_char_p, c_int]
    getattr(_lib, 'chan_config_set_double').argtypes = [c_void_p, c_char_p, c_double]
    getattr(_lib, 'chan_config_set_string').argtypes = [c_void_p, c_char_p, c_char_p]
    getattr(_lib, 'chan_config_set_bool').argtypes = [c_void_p, c_char_p, c_bool]

    for _fn in ['chan_config_get_bool', 'chan_config_get_int']:
        _f = getattr(_lib, _fn); _f.argtypes = [c_void_p, c_char_p]; _f.restype = c_int
    getattr(_lib, 'chan_config_get_double').argtypes = [c_void_p, c_char_p]
    getattr(_lib, 'chan_config_get_double').restype = c_double
    getattr(_lib, 'chan_config_get_string').argtypes = [c_void_p, c_char_p]
    getattr(_lib, 'chan_config_get_string').restype = c_char_p

    # --- K-line creation ---
    _lib.chan_kline_new.argtypes = [c_char_p, c_int, c_int, c_long,
                                     c_double, c_double, c_double, c_double, c_double]
    _lib.chan_kline_new.restype = c_void_p
    _lib.chan_kline_create_plain.argtypes = [c_char_p, c_long,
                                              c_double, c_double, c_double, c_double,
                                              c_double, c_int, c_int]
    _lib.chan_kline_create_plain.restype = c_void_p

    # --- Observer lifecycle ---
    _lib.chan_observer_new.argtypes = [c_char_p, c_int, c_void_p]
    _lib.chan_observer_new.restype = c_void_p
    _lib.chan_observer_feed_kline.argtypes = [c_void_p, c_void_p]
    _lib.chan_observer_feed_kline.restype = None
    _lib.chan_observer_read_file.argtypes = [c_char_p, c_void_p]
    _lib.chan_observer_read_file.restype = c_void_p
    _lib.chan_observer_release.argtypes = [c_void_p]
    _lib.chan_observer_release.restype = None

    # --- Sequence lengths ---
    _seq_names = [
        'raw_klines', 'chan_klines', 'base_chan_klines', 'fractals',
        'strokes', 'stroke_hubs', 'segments', 'hubs',
        'ext_segments', 'ext_hubs', 'ext_segments_seg', 'ext_hubs_seg',
        'seg_seg', 'seg_hubs', 'ext_seg_extseg', 'ext_hubs_extseg',
    ]
    for name in _seq_names:
        func = getattr(_lib, f'chan_observer_{name}_length')
        func.argtypes = [c_void_p]
        func.restype = c_size_t

    # --- Sequence element access ---
    for name in _seq_names:
        func = getattr(_lib, f'chan_observer_{name}_at')
        func.argtypes = [c_void_p, c_size_t]
        func.restype = c_void_p

    # --- Field accessors (set up lazily via _call / _call_ret) ---
    # We set up commonly-used ones explicitly for performance

    # Helper to set argtypes/restype via attribute access (triggers caching)
    def _sig(fn, at, rt):
        f = getattr(_lib, fn); f.argtypes = at; f.restype = rt

    # K-line
    for fn in ['chan_kline_id']: _sig(fn, [c_void_p], c_char_p)
    for fn in ['chan_kline_index', 'chan_kline_period', 'chan_kline_direction']:
        _sig(fn, [c_void_p], c_int)
    _sig('chan_kline_timestamp', [c_void_p], c_long)
    for fn in ['chan_kline_high', 'chan_kline_low', 'chan_kline_open',
               'chan_kline_close', 'chan_kline_volume']:
        _sig(fn, [c_void_p], c_double)

    # Chan K-line
    _sig('chan_ckline_id', [c_void_p], c_char_p)
    for fn in ['chan_ckline_index', 'chan_ckline_direction', 'chan_ckline_fractal_type',
               'chan_ckline_period', 'chan_ckline_orig_start', 'chan_ckline_orig_end']:
        _sig(fn, [c_void_p], c_int)
    _sig('chan_ckline_timestamp', [c_void_p], c_long)
    for fn in ['chan_ckline_high', 'chan_ckline_low', 'chan_ckline_fractal_value']:
        _sig(fn, [c_void_p], c_double)
    _sig('chan_ckline_ref_kline', [c_void_p], c_void_p)

    # Fractal
    for fn in ['chan_fractal_left', 'chan_fractal_mid', 'chan_fractal_right']:
        _sig(fn, [c_void_p], c_void_p)
    _sig('chan_fractal_structure', [c_void_p], c_int)
    _sig('chan_fractal_timestamp', [c_void_p], c_long)
    _sig('chan_fractal_feature_value', [c_void_p], c_double)

    # Dash line
    for fn in ['chan_dash_id', 'chan_dash_mode']: _sig(fn, [c_void_p], c_char_p)
    for fn in ['chan_dash_index', 'chan_dash_level', 'chan_dash_direction',
               'chan_dash_is_valid']:
        _sig(fn, [c_void_p], c_int)
    for fn in ['chan_dash_wen', 'chan_dash_wu']: _sig(fn, [c_void_p], c_void_p)
    for fn in ['chan_dash_high', 'chan_dash_low']: _sig(fn, [c_void_p], c_double)

    _sig('chan_dash_base_seq_length', [c_void_p], c_size_t)
    _sig('chan_dash_base_seq_at', [c_void_p, c_size_t], c_void_p)
    _sig('chan_dash_feature_seq_length', [c_void_p], c_size_t)
    _sig('chan_dash_feature_seq_at', [c_void_p, c_size_t], c_void_p)

    for kind in ['real', 'virtual', 'combined']:
        for op in ['length', 'at']:
            fn = f'chan_dash_{kind}_hub_seq_{op}'
            _sig(fn, [c_void_p] + ([c_size_t] if op == 'at' else []),
                 c_size_t if op == 'length' else c_void_p)

    # Hub
    for fn in ['chan_hub_id', 'chan_hub_status']: _sig(fn, [c_void_p], c_char_p)
    for fn in ['chan_hub_index', 'chan_hub_level', 'chan_hub_direction',
               'chan_hub_is_complete']:
        _sig(fn, [c_void_p], c_int)
    for fn in ['chan_hub_high', 'chan_hub_low', 'chan_hub_high_high', 'chan_hub_low_low']:
        _sig(fn, [c_void_p], c_double)
    for fn in ['chan_hub_third_buy_sell', 'chan_hub_local_third_buy_sell']:
        _sig(fn, [c_void_p], c_void_p)
    _sig('chan_hub_element_count', [c_void_p], c_size_t)
    _sig('chan_hub_element_at', [c_void_p, c_size_t], c_void_p)

    # Segment feature
    _sig('chan_segfeat_id', [c_void_p], c_char_p)
    for fn in ['chan_segfeat_index', 'chan_segfeat_direction']:
        _sig(fn, [c_void_p], c_int)
    for fn in ['chan_segfeat_wen', 'chan_segfeat_wu']: _sig(fn, [c_void_p], c_void_p)
    for fn in ['chan_segfeat_high', 'chan_segfeat_low']: _sig(fn, [c_void_p], c_double)
    _sig('chan_segfeat_element_count', [c_void_p], c_size_t)
    _sig('chan_segfeat_element_at', [c_void_p, c_size_t], c_void_p)

    # Gap
    for fn in ['chan_gap_high', 'chan_gap_low']: _sig(fn, [c_void_p], c_double)

    # Refcount
    _lib.chan_retain.argtypes = [c_void_p]; _lib.chan_retain.restype = None
    _lib.chan_release.argtypes = [c_void_p]; _lib.chan_release.restype = None

    # Utility
    _lib.chan_str2timestamp.argtypes = [c_char_p]; _lib.chan_str2timestamp.restype = c_long

    # Bulk export
    _lib.chan_observer_raw_klines_export.argtypes = [c_void_p, POINTER(c_size_t)]
    _lib.chan_observer_raw_klines_export.restype = POINTER(c_double)

    # Direction helpers
    for fn in ['chan_direction_is_up', 'chan_direction_is_down', 'chan_direction_is_gap',
               'chan_direction_is_containment', 'chan_direction_is_junction',
               'chan_direction_flip']:
        _sig(fn, [c_int], c_int)

    # 虚线 analysis
    for fn in ['chan_dash_before', 'chan_dash_after']:
        _sig(fn, [c_void_p, c_void_p], c_int)

    # 线段 analysis
    _sig('chan_segment_sixiang', [c_void_p], c_char_p)
    _sig('chan_segment_feature_fractal_end', [c_void_p], c_int)
    _sig('chan_segment_feature_seq_state', [c_void_p, POINTER(c_int), POINTER(c_int), POINTER(c_int)], None)
    _sig('chan_segment_get_gap', [c_void_p], c_void_p)
    _sig('chan_segment_find_penetration', [c_void_p], c_void_p)

    # 笔 lookup (observer-level)
    _sig('chan_observer_find_stroke_by_wen', [c_void_p, c_void_p], c_void_p)
    _sig('chan_observer_find_stroke_by_wu', [c_void_p, c_void_p], c_void_p)
    _sig('chan_observer_find_stroke_by_ckline', [c_void_p, c_void_p, c_int], c_void_p)
    _sig('chan_stroke_relative_relation', [c_void_p, c_void_p], c_int)

    # 背驰分析
    _sig('chan_divergence_slope', [c_void_p, c_void_p], c_int)
    _sig('chan_divergence_measure', [c_void_p, c_void_p], c_int)
    for fn in ['chan_divergence_macd', 'chan_divergence_full', 'chan_divergence_any',
               'chan_divergence_config', 'chan_divergence_optional']:
        _sig(fn, [c_void_p, c_void_p, c_void_p, c_size_t], c_int)
    _sig('chan_divergence_mode', [c_void_p, c_void_p, c_void_p, c_size_t, c_void_p, c_char_p], c_int)

    # K-line array helper
    _sig('chan_observer_get_kline_array',
         [c_void_p, c_void_p, POINTER(c_void_p), POINTER(c_size_t)], c_int)
    _sig('chan_free_kline_array', [c_void_p], None)

    # 虚线 field accessors
    _sig('chan_dash_confirm_ckline', [c_void_p], c_void_p)
    _sig('chan_dash_prev_gap', [c_void_p], c_void_p)
    _sig('chan_dash_prev_end', [c_void_p], c_void_p)
    _sig('chan_dash_short_circuit', [c_void_p], c_int)

    # 虚线 creation
    _sig('chan_dash_create_stroke', [c_void_p, c_void_p, c_int], c_void_p)

    # 虚线 K-line extraction
    _sig('chan_dash_get_raw_klines',
         [c_void_p, c_void_p, POINTER(c_void_p), POINTER(c_size_t)], c_int)
    _sig('chan_dash_get_chan_klines',
         [c_void_p, c_void_p, POINTER(c_void_p), POINTER(c_size_t)], c_int)

    # 笔 utility (observer-level)
    _sig('chan_observer_stroke_chan_count', [c_void_p, c_void_p], c_int)
    for fn in ['chan_observer_stroke_second_high', 'chan_observer_stroke_second_low',
               'chan_observer_stroke_actual_high', 'chan_observer_stroke_actual_low']:
        _sig(fn, [c_void_p, c_int], c_void_p)

    # Pipeline analysis
    _sig('chan_observer_analyze_strokes', [c_void_p, c_void_p], None)
    _sig('chan_observer_analyze_segments', [c_void_p, c_void_p], None)
    _sig('chan_observer_analyze_ext_segments', [c_void_p, c_void_p], None)
    for fn in ['chan_observer_analyze_stroke_hubs', 'chan_observer_analyze_segment_hubs']:
        _sig(fn, [c_void_p, c_int, c_char_p], None)

    return _lib


def _ensure_lib() -> ctypes.CDLL:
    if _lib is None:
        return load_library()
    return _lib


# ================================================================
# Base class for all C object wrappers
# ================================================================

class _ChanObject:
    """Base wrapper around a C pointer.

    Two ownership modes:
      - owned (default): calls chan_release() in __del__
      - borrowed: does NOT call chan_release() (the sequence owns the ref)

    Subclasses define _wrap_borrowed() / _wrap_owned() for type-safe wrapping.
    """

    __slots__ = ('_ptr', '_owns')

    def __init__(self, ptr: int, owns: bool = True):
        if not ptr:
            raise ValueError("Cannot wrap NULL pointer")
        self._ptr = ptr
        self._owns = owns

    def __del__(self):
        if self._owns and self._ptr:
            try:
                _ensure_lib().chan_release(self._ptr)
            except Exception:
                pass  # ignore errors during GC
            self._ptr = 0

    def __eq__(self, other) -> bool:
        if isinstance(other, _ChanObject):
            return self._ptr == other._ptr
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._ptr)

    def __repr__(self) -> str:
        return f'<{type(self).__name__} at {self._ptr:#x}>'

    @property
    def ptr(self) -> int:
        """Raw C pointer value."""
        return self._ptr

    @property
    def owns(self) -> bool:
        """Whether this Python object owns a reference count on the C object."""
        return self._owns

    def retain(self) -> None:
        """Increment the reference count (acquire ownership)."""
        _ensure_lib().chan_retain(self._ptr)
        self._owns = True

    def release(self) -> None:
        """Decrement the reference count. The C object is freed when it reaches 0."""
        if self._owns and self._ptr:
            _ensure_lib().chan_release(self._ptr)
            self._owns = False
            self._ptr = 0

    @classmethod
    def _from_owned(cls: Type[T], ptr: int) -> T:
        """Create a wrapper that owns the pointer (will chan_release on __del__)."""
        if not ptr:
            return None
        return cls(ptr, owns=True)

    @classmethod
    def _from_borrowed(cls: Type[T], ptr: int) -> T:
        """Create a wrapper that borrows the pointer (no chan_release on __del__)."""
        if not ptr:
            return None
        return cls(ptr, owns=False)


# ================================================================
# Concrete wrapper classes (Chinese names matching chan.py)
# ================================================================

class K线(_ChanObject):
    """Raw candlestick (K线)."""

    @property
    def 标识(self) -> str:
        return _ensure_lib().chan_kline_id(self._ptr).decode('utf-8')

    @property
    def 序号(self) -> int:
        return _ensure_lib().chan_kline_index(self._ptr)

    @property
    def 周期(self) -> int:
        return _ensure_lib().chan_kline_period(self._ptr)

    @property
    def 时间戳(self) -> int:
        return _ensure_lib().chan_kline_timestamp(self._ptr)

    @property
    def 高(self) -> float:
        return _ensure_lib().chan_kline_high(self._ptr)

    @property
    def 低(self) -> float:
        return _ensure_lib().chan_kline_low(self._ptr)

    @property
    def 开盘价(self) -> float:
        return _ensure_lib().chan_kline_open(self._ptr)

    @property
    def 收盘价(self) -> float:
        return _ensure_lib().chan_kline_close(self._ptr)

    @property
    def 成交量(self) -> float:
        return _ensure_lib().chan_kline_volume(self._ptr)

    @property
    def 方向(self) -> 相对方向:
        return 相对方向(_ensure_lib().chan_kline_direction(self._ptr))

    @classmethod
    def 创建普K(cls, 标识: str, 时间戳: int,
               开盘价: float, 高: float, 低: float,
               收盘价: float, 成交量: float,
               序号: int, 周期: int) -> 'K线':
        """Create a new plain K-line (calls C K线_创建普K)."""
        ptr = _ensure_lib().chan_kline_create_plain(
            标识.encode('utf-8'), 时间戳,
            开盘价, 高, 低, 收盘价, 成交量, 序号, 周期)
        return cls._from_owned(ptr)

    def __repr__(self) -> str:
        return (f'<K线 id={self.标识!r} idx={self.序号} '
                f'O={self.开盘价} H={self.高} L={self.低} C={self.收盘价}>')


class 缠论K线(_ChanObject):
    """Merged Chan Theory candlestick (缠论K线)."""

    @property
    def 序号(self) -> int:
        return _ensure_lib().chan_ckline_index(self._ptr)

    @property
    def 时间戳(self) -> int:
        return _ensure_lib().chan_ckline_timestamp(self._ptr)

    @property
    def 高(self) -> float:
        return _ensure_lib().chan_ckline_high(self._ptr)

    @property
    def 低(self) -> float:
        return _ensure_lib().chan_ckline_low(self._ptr)

    @property
    def 方向(self) -> 相对方向:
        return 相对方向(_ensure_lib().chan_ckline_direction(self._ptr))

    @property
    def 分型(self) -> 分型结构:
        return 分型结构(_ensure_lib().chan_ckline_fractal_type(self._ptr))

    @property
    def 分型特征值(self) -> float:
        return _ensure_lib().chan_ckline_fractal_value(self._ptr)

    @property
    def 周期(self) -> int:
        return _ensure_lib().chan_ckline_period(self._ptr)

    @property
    def 标识(self) -> str:
        return _ensure_lib().chan_ckline_id(self._ptr).decode('utf-8')

    @property
    def 原始起始序号(self) -> int:
        return _ensure_lib().chan_ckline_orig_start(self._ptr)

    @property
    def 原始结束序号(self) -> int:
        return _ensure_lib().chan_ckline_orig_end(self._ptr)

    @property
    def 标的K线(self) -> Optional[K线]:
        ptr = _ensure_lib().chan_ckline_ref_kline(self._ptr)
        return K线._from_borrowed(ptr) if ptr else None


class 分型(_ChanObject):
    """Three adjacent Chan K-lines forming a top/bottom pattern (分型)."""

    @property
    def 左(self) -> Optional[缠论K线]:
        ptr = _ensure_lib().chan_fractal_left(self._ptr)
        return 缠论K线._from_borrowed(ptr) if ptr else None

    @property
    def 中(self) -> Optional[缠论K线]:
        ptr = _ensure_lib().chan_fractal_mid(self._ptr)
        return 缠论K线._from_borrowed(ptr) if ptr else None

    @property
    def 右(self) -> Optional[缠论K线]:
        ptr = _ensure_lib().chan_fractal_right(self._ptr)
        return 缠论K线._from_borrowed(ptr) if ptr else None

    @property
    def 结构(self) -> 分型结构:
        return 分型结构(_ensure_lib().chan_fractal_structure(self._ptr))

    @property
    def 时间戳(self) -> int:
        return _ensure_lib().chan_fractal_timestamp(self._ptr)

    @property
    def 分型特征值(self) -> float:
        return _ensure_lib().chan_fractal_feature_value(self._ptr)

    def __repr__(self) -> str:
        return (f'<分型 struct={self.结构.name} '
                f'ts={self.时间戳} feat={self.分型特征值:.6f}>')


class 虚线(_ChanObject):
    """Dashed line (虚线) — base class for strokes (笔) and segments (线段)."""

    @property
    def 序号(self) -> int:
        return _ensure_lib().chan_dash_index(self._ptr)

    @property
    def 标识(self) -> str:
        return _ensure_lib().chan_dash_id(self._ptr).decode('utf-8')

    @property
    def 级别(self) -> int:
        return _ensure_lib().chan_dash_level(self._ptr)

    @property
    def 文(self) -> Optional[分型]:
        """Start fractal (文)."""
        ptr = _ensure_lib().chan_dash_wen(self._ptr)
        return 分型._from_borrowed(ptr) if ptr else None

    @property
    def 武(self) -> Optional[分型]:
        """End fractal (武)."""
        ptr = _ensure_lib().chan_dash_wu(self._ptr)
        return 分型._from_borrowed(ptr) if ptr else None

    @property
    def 方向(self) -> 相对方向:
        return 相对方向(_ensure_lib().chan_dash_direction(self._ptr))

    @property
    def 高(self) -> float:
        return _ensure_lib().chan_dash_high(self._ptr)

    @property
    def 低(self) -> float:
        return _ensure_lib().chan_dash_low(self._ptr)

    @property
    def 有效性(self) -> bool:
        return bool(_ensure_lib().chan_dash_is_valid(self._ptr))

    @property
    def 模式(self) -> str:
        return _ensure_lib().chan_dash_mode(self._ptr).decode('utf-8')

    # Sub-sequences

    @property
    def 基础序列(self) -> List['虚线']:
        """Constituent strokes (for segments; empty for plain strokes)."""
        n = _ensure_lib().chan_dash_base_seq_length(self._ptr)
        return [虚线._from_borrowed(
            _ensure_lib().chan_dash_base_seq_at(self._ptr, i)) for i in range(n)]

    @property
    def 特征序列(self) -> List['线段特征']:
        n = _ensure_lib().chan_dash_feature_seq_length(self._ptr)
        return [线段特征._from_borrowed(
            _ensure_lib().chan_dash_feature_seq_at(self._ptr, i)) for i in range(n)]

    @property
    def 实_中枢序列(self) -> List['中枢']:
        return self._中枢序列('real')

    @property
    def 虚_中枢序列(self) -> List['中枢']:
        return self._中枢序列('virtual')

    @property
    def 合_中枢序列(self) -> List['中枢']:
        return self._中枢序列('combined')

    def _中枢序列(self, kind: str) -> List['中枢']:
        fn_len = getattr(_ensure_lib(), f'chan_dash_{kind}_hub_seq_length')
        fn_at = getattr(_ensure_lib(), f'chan_dash_{kind}_hub_seq_at')
        n = fn_len(self._ptr)
        return [中枢._from_borrowed(fn_at(self._ptr, i)) for i in range(n)]

    # ---- Additional struct fields ----

    @property
    def 确认K线(self) -> Optional['缠论K线']:
        """The K-line that confirmed this dash."""
        ptr = _ensure_lib().chan_dash_confirm_ckline(self._ptr)
        return 缠论K线._from_borrowed(ptr) if ptr else None

    @property
    def 前一缺口(self) -> Optional['缺口']:
        """The gap immediately before this dash."""
        ptr = _ensure_lib().chan_dash_prev_gap(self._ptr)
        return 缺口._from_borrowed(ptr) if ptr else None

    @property
    def 前一结束位置(self) -> Optional['虚线']:
        """The previous ending position dash."""
        ptr = _ensure_lib().chan_dash_prev_end(self._ptr)
        return 虚线._from_borrowed(ptr) if ptr else None

    @property
    def 短路修正(self) -> bool:
        """Whether short-circuit correction was applied."""
        return bool(_ensure_lib().chan_dash_short_circuit(self._ptr))

    # ---- K-line extraction (requires observer) ----

    def _提取K线(self, obs: '观察者', fn_name: str, item_type: Type[_ChanObject]) -> list:
        """Extract K-line array for this dash from an observer."""
        lib = _ensure_lib()
        out = c_void_p()
        out_len = c_size_t()
        fn = getattr(lib, fn_name)
        ok = fn(self._ptr, obs.ptr, byref(out), byref(out_len))
        if not ok or not out.value:
            return []
        n = out_len.value
        SrcArray = c_void_p * n
        src = SrcArray.from_address(out.value)
        # c_void_p array elements return plain ints when indexed
        result = [item_type._from_borrowed(int(src[i])) for i in range(n)]
        lib.chan_free_kline_array(out)
        return result

    def 获取普K序列(self, obs: '观察者') -> List['K线']:
        """Get the raw K-lines that compose this dash."""
        return self._提取K线(obs, 'chan_dash_get_raw_klines', K线)

    def 获取缠K序列(self, obs: '观察者') -> List['缠论K线']:
        """Get the Chan K-lines that compose this dash."""
        return self._提取K线(obs, 'chan_dash_get_chan_klines', 缠论K线)

    # ---- Analysis methods ----

    def 之前是(self, 之前: '虚线') -> bool:
        """Check if this dash is before another dash."""
        return bool(_ensure_lib().chan_dash_before(self._ptr, 之前.ptr))

    def 之后是(self, 之后: '虚线') -> bool:
        """Check if this dash is after another dash."""
        return bool(_ensure_lib().chan_dash_after(self._ptr, 之后.ptr))

    def __repr__(self) -> str:
        dname = '上' if self.方向 == 相对方向.向上 else '下'
        return (f'<{type(self).__name__} id={self.标识!r} idx={self.序号} '
                f'level={self.级别} dir={dname} '
                f'H={self.高:.6f} L={self.低:.6f}>')


class 笔(虚线):
    """A stroke (笔) — a dashed line connecting adjacent fractals."""

    @classmethod
    def 创建笔(cls, 文: 分型, 武: 分型, 有效性: bool = True) -> '笔':
        """Create a new stroke connecting two fractals."""
        ptr = _ensure_lib().chan_dash_create_stroke(文.ptr, 武.ptr, int(有效性))
        if not ptr:
            raise MemoryError("Failed to create stroke")
        return cls._from_owned(ptr)

    def 相对关系(self, 配置: 缠论配置) -> bool:
        """Check the relative relationship of this stroke under the given config."""
        return bool(_ensure_lib().chan_stroke_relative_relation(self._ptr, 配置.ptr))


class 线段(虚线):
    """A segment (线段) — a higher-level dashed line built from strokes."""

    def 四象(self) -> str:
        """Four-image classification of this segment. Returns a short string label."""
        result = _ensure_lib().chan_segment_sixiang(self._ptr)
        return result.decode('utf-8') if result else ''

    def 特征分型终结(self) -> bool:
        """Check if the feature fractal sequence terminates."""
        return bool(_ensure_lib().chan_segment_feature_fractal_end(self._ptr))

    def 特征序列状态(self):
        """Return (左状态, 中状态, 右状态) of the feature sequence — 3 bools."""
        left, mid, right = c_int(), c_int(), c_int()
        _ensure_lib().chan_segment_feature_seq_state(
            self._ptr, byref(left), byref(mid), byref(right))
        return (bool(left.value), bool(mid.value), bool(right.value))

    def 获取缺口(self) -> Optional['缺口']:
        """Get the gap associated with this segment, if any."""
        ptr = _ensure_lib().chan_segment_get_gap(self._ptr)
        return 缺口._from_borrowed(ptr) if ptr else None

    def 查找贯穿伤(self) -> Optional[虚线]:
        """Find a stroke that penetrates through this segment's hub."""
        ptr = _ensure_lib().chan_segment_find_penetration(self._ptr)
        return 虚线._from_borrowed(ptr) if ptr else None


class 线段特征(_ChanObject):
    """Segment feature sequence element (线段特征)."""

    @property
    def 序号(self) -> int:
        return _ensure_lib().chan_segfeat_index(self._ptr)

    @property
    def 标识(self) -> str:
        return _ensure_lib().chan_segfeat_id(self._ptr).decode('utf-8')

    @property
    def 方向(self) -> 相对方向:
        return 相对方向(_ensure_lib().chan_segfeat_direction(self._ptr))

    @property
    def 文(self) -> Optional[分型]:
        ptr = _ensure_lib().chan_segfeat_wen(self._ptr)
        return 分型._from_borrowed(ptr) if ptr else None

    @property
    def 武(self) -> Optional[分型]:
        ptr = _ensure_lib().chan_segfeat_wu(self._ptr)
        return 分型._from_borrowed(ptr) if ptr else None

    @property
    def 高(self) -> float:
        return _ensure_lib().chan_segfeat_high(self._ptr)

    @property
    def 低(self) -> float:
        return _ensure_lib().chan_segfeat_low(self._ptr)

    @property
    def 元素(self) -> List[虚线]:
        n = _ensure_lib().chan_segfeat_element_count(self._ptr)
        return [虚线._from_borrowed(
            _ensure_lib().chan_segfeat_element_at(self._ptr, i)) for i in range(n)]


class 中枢(_ChanObject):
    """Price hub (中枢) — overlapping region of three consecutive strokes/segments."""

    @property
    def 序号(self) -> int:
        return _ensure_lib().chan_hub_index(self._ptr)

    @property
    def 标识(self) -> str:
        return _ensure_lib().chan_hub_id(self._ptr).decode('utf-8')

    @property
    def 级别(self) -> int:
        return _ensure_lib().chan_hub_level(self._ptr)

    @property
    def 高(self) -> float:
        return _ensure_lib().chan_hub_high(self._ptr)

    @property
    def 低(self) -> float:
        return _ensure_lib().chan_hub_low(self._ptr)

    @property
    def 高高(self) -> float:
        return _ensure_lib().chan_hub_high_high(self._ptr)

    @property
    def 低低(self) -> float:
        return _ensure_lib().chan_hub_low_low(self._ptr)

    @property
    def 方向(self) -> 相对方向:
        return 相对方向(_ensure_lib().chan_hub_direction(self._ptr))

    @property
    def 完整性(self) -> bool:
        return bool(_ensure_lib().chan_hub_is_complete(self._ptr))

    def 当前状态(self) -> str:
        return _ensure_lib().chan_hub_status(self._ptr).decode('utf-8')

    @property
    def 第三买卖线(self) -> Optional[虚线]:
        ptr = _ensure_lib().chan_hub_third_buy_sell(self._ptr)
        return 虚线._from_borrowed(ptr) if ptr else None

    @property
    def 本级_第三买卖线(self) -> Optional[虚线]:
        ptr = _ensure_lib().chan_hub_local_third_buy_sell(self._ptr)
        return 虚线._from_borrowed(ptr) if ptr else None

    @property
    def 元素(self) -> List[虚线]:
        n = _ensure_lib().chan_hub_element_count(self._ptr)
        return [虚线._from_borrowed(
            _ensure_lib().chan_hub_element_at(self._ptr, i)) for i in range(n)]

    def __repr__(self) -> str:
        return (f'<中枢 id={self.标识!r} idx={self.序号} level={self.级别} '
                f'H={self.高:.6f} L={self.低:.6f}>')


class 缺口(_ChanObject):
    """Price gap (缺口) between two candles."""

    @property
    def 高(self) -> float:
        return _ensure_lib().chan_gap_high(self._ptr)

    @property
    def 低(self) -> float:
        return _ensure_lib().chan_gap_low(self._ptr)


# ================================================================
# Config
# ================================================================

# Key config fields with Chinese names for the C dispatch.
# Grouped by type for use in 缠论配置.__init__.
_CONFIG_INT_FIELDS = [
    '笔内元素数量', '笔弱化_原始数量', '买卖点偏移', '买卖点_T2S_最大层级',
    '平滑异同移动平均线_快线周期', '平滑异同移动平均线_慢线周期',
    '平滑异同移动平均线_信号周期', '相对强弱指数_周期',
    '相对强弱指数_移动平均线周期', '随机指标_RSV周期',
    '随机指标_K值平滑周期', '随机指标_D值平滑周期',
]

_CONFIG_DOUBLE_FIELDS = [
    '相对强弱指数_超买阈值', '相对强弱指数_超卖阈值',
    '随机指标_超买阈值', '随机指标_超卖阈值',
    '买卖点错过误差值', '买卖点_背离率', '买卖点_T2_回调阈值',
]

_CONFIG_BOOL_FIELDS = [
    '缠K合并替换', '笔内相同终点取舍', '笔内起始分型包含整笔',
    '笔内起始分型包含整笔_包括右', '笔内原始K线包含整笔',
    '笔次级成笔', '笔弱化', '线段_非缺口下穿刺',
    '线段_特征序列忽视老阴老阳', '线段_缺口后紧急修正', '线段_修正',
    '线段内部中枢图显', '扩展线段_当下分析',
    '分析笔', '分析线段', '分析扩展线段', '分析笔中枢', '分析线段中枢',
    '计算指标', '图表展示',
    '推送K线', '推送笔', '推送线段', '推送中枢',
    '图表展示_笔', '图表展示_线段', '图表展示_扩展线段',
    '图表展示_扩展线段_线段', '图表展示_线段_线段',
    '图表展示_中枢_笔', '图表展示_中枢_线段',
    '图表展示_中枢_扩展线段', '图表展示_中枢_扩展线段_线段',
    '图表展示_中枢_线段_线段', '图表展示_中枢_线段内部',
    '买卖点激进识别', '买卖点与MACD柱强相关',
    '买卖点_指标匹配_MACD', '买卖点_指标匹配_KDJ', '买卖点_指标匹配_RSI',
    '买卖点_峰值条件', '买卖点_计算线段BSP1', '买卖点_处理BSP2',
    '买卖点_计算线段BSP3', '买卖点_依赖T1', '买卖点_调试输出',
    '线段内部背驰_MACD', '线段内部背驰_斜率', '线段内部背驰_测度',
]

_CONFIG_STRING_FIELDS = [
    '手动终止', '指标计算方式', '买卖点_指标模式',
    '买卖点_计算方式', '买卖点_中枢来源', '线段内部背驰_模式',
    '标识', '加载文件路径',
]


class 缠论配置:
    """Configuration for the Chan Theory analysis pipeline.

    Holds a C 缠论配置* pointer and dispatches field access via
    string-keyed C getter/setter functions. All field names are in
    Chinese (matching the C struct member names).

    Typical usage:

        config = 缠论配置(no_push=True)
        config['分析笔'] = True
        config['分析线段'] = False
        obs = 观察者.读取数据文件('data.nb', config)
    """

    def __init__(self, no_push: bool = False):
        lib = _ensure_lib()
        if no_push:
            self._ptr = lib.chan_config_new_no_push()
        else:
            self._ptr = lib.chan_config_new()
        if not self._ptr:
            raise MemoryError("Failed to allocate 缠论配置")

    def __del__(self):
        if hasattr(self, '_ptr') and self._ptr:
            try:
                _ensure_lib().chan_config_release(self._ptr)
            except Exception:
                pass
            self._ptr = 0

    def __getitem__(self, field: str):
        return self._获取(field)

    def __setitem__(self, field: str, value):
        self._设置(field, value)

    def _获取(self, field: str):
        lib = _ensure_lib()
        if field in _CONFIG_BOOL_FIELDS:
            return bool(lib.chan_config_get_bool(self._ptr, field.encode('utf-8')))
        elif field in _CONFIG_INT_FIELDS:
            return lib.chan_config_get_int(self._ptr, field.encode('utf-8'))
        elif field in _CONFIG_DOUBLE_FIELDS:
            return lib.chan_config_get_double(self._ptr, field.encode('utf-8'))
        elif field in _CONFIG_STRING_FIELDS:
            result = lib.chan_config_get_string(self._ptr, field.encode('utf-8'))
            return result.decode('utf-8') if result else ''
        else:
            raise KeyError(f"Unknown config field: {field}")

    def _设置(self, field: str, value):
        lib = _ensure_lib()
        field_bytes = field.encode('utf-8')
        if field in _CONFIG_BOOL_FIELDS:
            lib.chan_config_set_bool(self._ptr, field_bytes, int(value))
        elif field in _CONFIG_INT_FIELDS:
            lib.chan_config_set_int(self._ptr, field_bytes, int(value))
        elif field in _CONFIG_DOUBLE_FIELDS:
            lib.chan_config_set_double(self._ptr, field_bytes, float(value))
        elif field in _CONFIG_STRING_FIELDS:
            lib.chan_config_set_string(self._ptr, field_bytes, str(value).encode('utf-8'))
        else:
            raise KeyError(f"Unknown config field: {field}")

    def __getattr__(self, name: str):
        """Allow attribute-style access for Chinese field names."""
        if name.startswith('_'):
            raise AttributeError(name)
        try:
            return self._获取(name)
        except KeyError:
            raise AttributeError(f"No config field named '{name}'")

    def __setattr__(self, name: str, value):
        if name.startswith('_'):
            super().__setattr__(name, value)
            return
        try:
            self._设置(name, value)
        except KeyError:
            raise AttributeError(f"No config field named '{name}'")

    def __dir__(self):
        return (super().__dir__() +
                _CONFIG_BOOL_FIELDS + _CONFIG_INT_FIELDS +
                _CONFIG_DOUBLE_FIELDS + _CONFIG_STRING_FIELDS)

    @property
    def ptr(self) -> int:
        """Raw C pointer for passing to observer constructors."""
        return self._ptr

    def release(self) -> None:
        """Explicitly release the C config object."""
        if self._ptr:
            _ensure_lib().chan_config_release(self._ptr)
            self._ptr = 0

    def __repr__(self) -> str:
        return f'<缠论配置 at {self._ptr:#x}>'


# ================================================================
# Sequence view — lazy iterable over observer sequences
# ================================================================

_SEQ_NAMES = [
    'raw_klines', 'chan_klines', 'base_chan_klines', 'fractals',
    'strokes', 'stroke_hubs', 'segments', 'hubs',
    'ext_segments', 'ext_hubs', 'ext_segments_seg', 'ext_hubs_seg',
    'seg_seg', 'seg_hubs', 'ext_seg_extseg', 'ext_hubs_extseg',
]

_SEQ_ITEM_TYPES: dict = {
    'raw_klines': K线,
    'chan_klines': 缠论K线,
    'base_chan_klines': 缠论K线,
    'fractals': 分型,
    'strokes': 笔,
    'stroke_hubs': 中枢,
    'segments': 线段,
    'hubs': 中枢,
    'ext_segments': 线段,
    'ext_hubs': 中枢,
    'ext_segments_seg': 线段,
    'ext_hubs_seg': 中枢,
    'seg_seg': 线段,
    'seg_hubs': 中枢,
    'ext_seg_extseg': 线段,
    'ext_hubs_extseg': 中枢,
}


class _SequenceView:
    """Lazy view of an observer's sequence array.

    Supports len(), indexing [], iteration, and to_list().
    All elements are returned as borrowed wrappers.
    """

    def __init__(self, obs_ptr: int, seq_name: str, item_type: Type[_ChanObject]):
        self._obs_ptr = obs_ptr
        self._seq_name = seq_name
        self._item_type = item_type
        lib = _ensure_lib()
        self._len_fn = getattr(lib, f'chan_observer_{seq_name}_length')
        self._at_fn = getattr(lib, f'chan_observer_{seq_name}_at')

    def __len__(self) -> int:
        return self._len_fn(self._obs_ptr)

    def __getitem__(self, idx: int):
        if idx < 0:
            idx += len(self)
        if idx < 0 or idx >= len(self):
            raise IndexError(
                f'{self._item_type.__name__} sequence index {idx} out of range')
        ptr = self._at_fn(self._obs_ptr, idx)
        if not ptr:
            raise IndexError(
                f'{self._item_type.__name__} at index {idx} is NULL')
        return self._item_type._from_borrowed(ptr)

    def __iter__(self) -> Iterator[_ChanObject]:
        for i in range(len(self)):
            yield self[i]

    def to_list(self) -> list:
        """Materialize the entire sequence into a Python list."""
        return list(self)

    def __repr__(self) -> str:
        return f'<_SequenceView {self._seq_name} len={len(self)}>'


# ================================================================
# Observer
# ================================================================

class 观察者:
    """Top-level orchestrator. Wraps a C 观察者* pointer.

    Holds all sequence arrays and drives the analysis pipeline.

    Typical usage:

        # Batch mode: load from file
        config = 缠论配置(no_push=True)
        obs = 观察者.读取数据文件('btcusd-300-xxx.nb', config)
        print(f'Strokes: {len(obs.笔序列)}')
        print(f'Segments: {len(obs.线段序列)}')
        print(f'Hubs: {len(obs.中枢序列)}')
        obs.release()

        # Incremental mode: feed K-lines one at a time
        config = 缠论配置(no_push=True)
        obs = 观察者('BTCUSD', 300, config)
        kline = K线.创建普K('Bar', 1234567890, 42000, 42100, 41900, 42050, 10.5, 0, 300)
        obs.增加原始K线(kline)
    """

    def __init__(self, 符号: str, 周期: int, 配置: 缠论配置):
        """Create a new observer.

        Args:
            符号: Trading pair symbol (e.g. 'BTCUSD')
            周期: K-line period in seconds (e.g. 300 for 5min)
            配置: 缠论配置 instance (ownership is NOT transferred;
                   you must keep config alive while the observer exists)
        """
        ptr = _ensure_lib().chan_observer_new(
            符号.encode('utf-8'), 周期, 配置.ptr)
        if not ptr:
            raise MemoryError("Failed to create observer")
        self._ptr = ptr
        self._配置 = 配置  # keep config alive
        self._owns = True

    def __del__(self):
        if hasattr(self, '_ptr') and self._ptr:
            try:
                观察者._释放(self._ptr)
            except Exception:
                pass
            self._ptr = 0

    @staticmethod
    def _释放(ptr: int) -> None:
        _ensure_lib().chan_observer_release(ptr)

    def release(self) -> None:
        """Explicitly release the C observer and all its managed objects.

        After calling this, all sequence views and borrowed wrappers
        from this observer are invalid.
        """
        if self._ptr:
            观察者._释放(self._ptr)
            self._ptr = 0

    @property
    def ptr(self) -> int:
        return self._ptr

    def 增加原始K线(self, 普K: K线) -> None:
        """Feed a single raw K-line into the incremental analysis pipeline.

        The observer does NOT take ownership of the kline — you must
        keep it alive if you need it after feeding.
        """
        _ensure_lib().chan_observer_feed_kline(self._ptr, 普K.ptr)

    @classmethod
    def 读取数据文件(cls, 文件路径: str, 配置: 缠论配置) -> '观察者':
        """Load a .nb data file and run the full analysis pipeline.

        Equivalent to: 观察者(symbol, period, config) + feed loop.
        """
        ptr = _ensure_lib().chan_observer_read_file(
            文件路径.encode('utf-8'), 配置.ptr)
        if not ptr:
            raise FileNotFoundError(f"Failed to read: {文件路径}")
        obs = cls.__new__(cls)
        obs._ptr = ptr
        obs._配置 = 配置
        obs._owns = True
        return obs

    # ---- Sequence properties (Chinese names matching chan.py) ----

    @property
    def 普通K线序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'raw_klines', K线)

    @property
    def 缠论K线序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'chan_klines', 缠论K线)

    @property
    def 基础缠K序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'base_chan_klines', 缠论K线)

    @property
    def 分型序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'fractals', 分型)

    @property
    def 笔序列(self) -> _SequenceView:
        """笔 (Bi) — strokes connecting adjacent fractals."""
        return _SequenceView(self._ptr, 'strokes', 笔)

    @property
    def 笔_中枢序列(self) -> _SequenceView:
        """笔中枢 — hubs built from strokes."""
        return _SequenceView(self._ptr, 'stroke_hubs', 中枢)

    @property
    def 线段序列(self) -> _SequenceView:
        """线段 — segments built from strokes."""
        return _SequenceView(self._ptr, 'segments', 线段)

    @property
    def 中枢序列(self) -> _SequenceView:
        """中枢 — hubs built from segments."""
        return _SequenceView(self._ptr, 'hubs', 中枢)

    @property
    def 扩展线段序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'ext_segments', 线段)

    @property
    def 扩展中枢序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'ext_hubs', 中枢)

    @property
    def 扩展线段序列_线段(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'ext_segments_seg', 线段)

    @property
    def 扩展中枢序列_线段(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'ext_hubs_seg', 中枢)

    @property
    def 线段_线段序列(self) -> _SequenceView:
        """线段 of 线段."""
        return _SequenceView(self._ptr, 'seg_seg', 线段)

    @property
    def 线段_中枢序列(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'seg_hubs', 中枢)

    @property
    def 扩展线段序列_扩展线段(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'ext_seg_extseg', 线段)

    @property
    def 扩展中枢序列_扩展线段(self) -> _SequenceView:
        return _SequenceView(self._ptr, 'ext_hubs_extseg', 中枢)

    # ---- Pipeline analysis (re-run on observer's sequences) ----

    def 笔_分析(self) -> None:
        """Re-run stroke analysis on the current observer sequences."""
        _ensure_lib().chan_observer_analyze_strokes(self._ptr, self._配置.ptr)

    def 线段_分析(self) -> None:
        """Re-run segment analysis on the current stroke sequence."""
        _ensure_lib().chan_observer_analyze_segments(self._ptr, self._配置.ptr)

    def 扩展线段_分析(self) -> None:
        """Re-run extended segment analysis on the current stroke sequence."""
        _ensure_lib().chan_observer_analyze_ext_segments(self._ptr, self._配置.ptr)

    def 笔_中枢分析(self, 跳过首部: bool = False, 标识: str = "笔中枢") -> None:
        """Re-run hub analysis on the stroke sequence."""
        _ensure_lib().chan_observer_analyze_stroke_hubs(
            self._ptr, int(跳过首部), 标识.encode('utf-8'))

    def 线段_中枢分析(self, 跳过首部: bool = False, 标识: str = "中枢") -> None:
        """Re-run hub analysis on the segment sequence."""
        _ensure_lib().chan_observer_analyze_segment_hubs(
            self._ptr, int(跳过首部), 标识.encode('utf-8'))

    # ---- 笔 utility methods (operate on observer's Chan K-line sequence) ----

    def 获取缠K数量(self) -> int:
        """Get the count of Chan K-lines in the stroke analysis."""
        return _ensure_lib().chan_observer_stroke_chan_count(self._ptr, self._配置.ptr)

    def 次高(self, 笔内相同终点取舍: bool = False) -> Optional[缠论K线]:
        """Second-highest Chan K-line in the observer's sequence."""
        ptr = _ensure_lib().chan_observer_stroke_second_high(
            self._ptr, int(笔内相同终点取舍))
        return 缠论K线._from_borrowed(ptr) if ptr else None

    def 次低(self, 笔内相同终点取舍: bool = False) -> Optional[缠论K线]:
        """Second-lowest Chan K-line in the observer's sequence."""
        ptr = _ensure_lib().chan_observer_stroke_second_low(
            self._ptr, int(笔内相同终点取舍))
        return 缠论K线._from_borrowed(ptr) if ptr else None

    def 实际高点(self, 笔内相同终点取舍: bool = False) -> Optional[缠论K线]:
        """Actual high Chan K-line in the observer's sequence."""
        ptr = _ensure_lib().chan_observer_stroke_actual_high(
            self._ptr, int(笔内相同终点取舍))
        return 缠论K线._from_borrowed(ptr) if ptr else None

    def 实际低点(self, 笔内相同终点取舍: bool = False) -> Optional[缠论K线]:
        """Actual low Chan K-line in the observer's sequence."""
        ptr = _ensure_lib().chan_observer_stroke_actual_low(
            self._ptr, int(笔内相同终点取舍))
        return 缠论K线._from_borrowed(ptr) if ptr else None

    # ---- Stroke lookup methods ----

    def 以文会友(self, 文: 分型) -> Optional[笔]:
        """Find a stroke by its start fractal (文)."""
        ptr = _ensure_lib().chan_observer_find_stroke_by_wen(self._ptr, 文.ptr)
        return 笔._from_borrowed(ptr) if ptr else None

    def 以武会友(self, 武: 分型) -> Optional[笔]:
        """Find a stroke by its end fractal (武)."""
        ptr = _ensure_lib().chan_observer_find_stroke_by_wu(self._ptr, 武.ptr)
        return 笔._from_borrowed(ptr) if ptr else None

    def 根据缠K找笔(self, 缠K: 缠论K线, 偏移: int = 1) -> Optional[笔]:
        """Find a stroke by its constituent Chan K-line."""
        ptr = _ensure_lib().chan_observer_find_stroke_by_ckline(self._ptr, 缠K.ptr, 偏移)
        return 笔._from_borrowed(ptr) if ptr else None

    # ---- Divergence (背驰) analysis ----

    def _获取K线数组(self, dash: 虚线):
        """Extract raw K-line pointer array for a dash.
        Returns (array_address, count) — array_address is a c_void_p whose
        .value is the malloc'd void** pointer. Caller must call
        chan_free_kline_array on the .value when done.
        """
        out = c_void_p()
        out_len = c_size_t()
        ok = _ensure_lib().chan_observer_get_kline_array(
            self._ptr, dash.ptr, byref(out), byref(out_len))
        if not ok or not out.value:
            return None, 0
        return out, out_len.value

    def _背驰检查(self, 进入段: 虚线, 离开段: 虚线, fn_name: str, *extra_args) -> bool:
        """Internal: run a divergence check with K-line arrays for both dashes."""
        lib = _ensure_lib()
        arr1, n1 = self._获取K线数组(进入段)
        arr2, n2 = self._获取K线数组(离开段)
        if not arr1 or not arr2:
            if arr1: lib.chan_free_kline_array(arr1)
            if arr2: lib.chan_free_kline_array(arr2)
            return False

        total = n1 + n2
        CombinedArray = c_void_p * total
        combined = CombinedArray()

        # arr1.value is the address of a malloc'd void*[] — map it
        SrcArray1 = c_void_p * n1
        src1 = SrcArray1.from_address(arr1.value)
        for i in range(n1):
            combined[i] = int(src1[i])

        SrcArray2 = c_void_p * n2
        src2 = SrcArray2.from_address(arr2.value)
        for i in range(n2):
            combined[n1 + i] = int(src2[i])

        fn = getattr(lib, fn_name)
        args = [进入段.ptr, 离开段.ptr, ctypes.cast(combined, c_void_p), total] + list(extra_args)
        result = fn(*args)

        lib.chan_free_kline_array(arr1)
        lib.chan_free_kline_array(arr2)
        return bool(result)

    def MACD背驰(self, 进入段: 虚线, 离开段: 虚线, 方式: str = "总") -> bool:
        return self._背驰检查(进入段, 离开段, 'chan_divergence_macd', 方式.encode('utf-8'))

    def 斜率背驰(self, 进入段: 虚线, 离开段: 虚线) -> bool:
        return bool(_ensure_lib().chan_divergence_slope(进入段.ptr, 离开段.ptr))

    def 测度背驰(self, 进入段: 虚线, 离开段: 虚线) -> bool:
        return bool(_ensure_lib().chan_divergence_measure(进入段.ptr, 离开段.ptr))

    def 全量背驰(self, 进入段: 虚线, 离开段: 虚线) -> bool:
        return self._背驰检查(进入段, 离开段, 'chan_divergence_full')

    def 任意背驰(self, 进入段: 虚线, 离开段: 虚线) -> bool:
        return self._背驰检查(进入段, 离开段, 'chan_divergence_any')

    def 配置背驰(self, 进入段: 虚线, 离开段: 虚线) -> bool:
        return self._背驰检查(进入段, 离开段, 'chan_divergence_config', self._配置.ptr)

    def 任选背驰(self, 进入段: 虚线, 离开段: 虚线) -> bool:
        return self._背驰检查(进入段, 离开段, 'chan_divergence_optional')

    def 背驰模式(self, 进入段: 虚线, 离开段: 虚线, 模式: str) -> bool:
        return self._背驰检查(进入段, 离开段, 'chan_divergence_mode', self._配置.ptr, 模式.encode('utf-8'))

    def __repr__(self) -> str:
        if not self._ptr:
            return '<观察者 (released)>'
        try:
            nk = len(self.普通K线序列)
            ns = len(self.笔序列)
            nseg = len(self.线段序列)
            nh = len(self.中枢序列)
        except Exception:
            return f'<观察者 at {self._ptr:#x}>'
        return (f'<观察者 at {self._ptr:#x} '
                f'Klines={nk} Strokes={ns} Segments={nseg} Hubs={nh}>')


# ================================================================
# Backward-compatible English aliases
# ================================================================

KLine = K线
ChanKLine = 缠论K线
Fractal = 分型
DashLine = 虚线
Stroke = 笔
Segment = 线段
SegmentFeature = 线段特征
Hub = 中枢
Gap = 缺口
ChanConfig = 缠论配置
Observer = 观察者

# English property aliases on each class
# K线
K线.id = K线.标识
K线.index = K线.序号
K线.period = K线.周期
K线.timestamp = K线.时间戳
K线.high = K线.高
K线.low = K线.低
K线.open = K线.开盘价
K线.close = K线.收盘价
K线.volume = K线.成交量
K线.direction = K线.方向
K线.create_plain = K线.创建普K

# 缠论K线
缠论K线.index = 缠论K线.序号
缠论K线.timestamp = 缠论K线.时间戳
缠论K线.high = 缠论K线.高
缠论K线.low = 缠论K线.低
缠论K线.direction = 缠论K线.方向
缠论K线.fractal_type = 缠论K线.分型
缠论K线.fractal_feature_value = 缠论K线.分型特征值
缠论K线.period = 缠论K线.周期
缠论K线.id = 缠论K线.标识
缠论K线.orig_start = 缠论K线.原始起始序号
缠论K线.orig_end = 缠论K线.原始结束序号
缠论K线.ref_kline = 缠论K线.标的K线

# 分型
分型.left = 分型.左
分型.mid = 分型.中
分型.right = 分型.右
分型.structure = 分型.结构
分型.timestamp = 分型.时间戳
分型.feature_value = 分型.分型特征值

# 虚线
虚线.index = 虚线.序号
虚线.id = 虚线.标识
虚线.level = 虚线.级别
虚线.wen = 虚线.文
虚线.wu = 虚线.武
虚线.direction = 虚线.方向
虚线.high = 虚线.高
虚线.low = 虚线.低
虚线.is_valid = 虚线.有效性
虚线.mode = 虚线.模式
虚线.base_sequence = 虚线.基础序列
虚线.feature_sequence = 虚线.特征序列
虚线.real_hubs = 虚线.实_中枢序列
虚线.virtual_hubs = 虚线.虚_中枢序列
虚线.combined_hubs = 虚线.合_中枢序列

# 线段特征
线段特征.index = 线段特征.序号
线段特征.id = 线段特征.标识
线段特征.direction = 线段特征.方向
线段特征.wen = 线段特征.文
线段特征.wu = 线段特征.武
线段特征.high = 线段特征.高
线段特征.low = 线段特征.低
线段特征.elements = 线段特征.元素

# 中枢
中枢.index = 中枢.序号
中枢.id = 中枢.标识
中枢.level = 中枢.级别
中枢.high = 中枢.高
中枢.low = 中枢.低
中枢.high_high = 中枢.高高
中枢.low_low = 中枢.低低
中枢.direction = 中枢.方向
中枢.is_complete = 中枢.完整性
中枢.status = 中枢.当前状态
中枢.third_buy_sell = 中枢.第三买卖线
中枢.local_third_buy_sell = 中枢.本级_第三买卖线
中枢.elements = 中枢.元素

# 虚线 — analysis method aliases
虚线.before = 虚线.之前是
虚线.after = 虚线.之后是

# 笔 — analysis method aliases
笔.relative_relation = 笔.相对关系

# 线段 — analysis method aliases
线段.sixiang = 线段.四象
线段.feature_fractal_end = 线段.特征分型终结
线段.feature_seq_state = 线段.特征序列状态
线段.get_gap = 线段.获取缺口
线段.find_penetration = 线段.查找贯穿伤

# 缺口
缺口.high = 缺口.高
缺口.low = 缺口.低

# 观察者
观察者.feed_kline = 观察者.增加原始K线
观察者.read_file = 观察者.读取数据文件
观察者.raw_klines = 观察者.普通K线序列
观察者.chan_klines = 观察者.缠论K线序列
观察者.base_chan_klines = 观察者.基础缠K序列
观察者.fractals = 观察者.分型序列
观察者.strokes = 观察者.笔序列
观察者.stroke_hubs = 观察者.笔_中枢序列
观察者.segments = 观察者.线段序列
观察者.hubs = 观察者.中枢序列
观察者.ext_segments = 观察者.扩展线段序列
观察者.ext_hubs = 观察者.扩展中枢序列
观察者.ext_segments_seg = 观察者.扩展线段序列_线段
观察者.ext_hubs_seg = 观察者.扩展中枢序列_线段
观察者.seg_seg = 观察者.线段_线段序列
观察者.seg_hubs = 观察者.线段_中枢序列
观察者.ext_seg_extseg = 观察者.扩展线段序列_扩展线段
观察者.ext_hubs_extseg = 观察者.扩展中枢序列_扩展线段
# 观察者 — analysis method aliases
观察者.find_stroke_by_wen = 观察者.以文会友
观察者.find_stroke_by_wu = 观察者.以武会友
观察者.find_stroke_by_ckline = 观察者.根据缠K找笔
观察者.divergence_slope = 观察者.斜率背驰
观察者.divergence_measure = 观察者.测度背驰
观察者.divergence_macd = 观察者.MACD背驰
观察者.divergence_full = 观察者.全量背驰
观察者.divergence_any = 观察者.任意背驰
观察者.divergence_config = 观察者.配置背驰
观察者.divergence_optional = 观察者.任选背驰
观察者.divergence_mode = 观察者.背驰模式
# 观察者 — pipeline analysis & utility aliases
观察者.analyze_strokes = 观察者.笔_分析
观察者.analyze_segments = 观察者.线段_分析
观察者.analyze_ext_segments = 观察者.扩展线段_分析
观察者.analyze_stroke_hubs = 观察者.笔_中枢分析
观察者.analyze_segment_hubs = 观察者.线段_中枢分析
观察者.stroke_chan_count = 观察者.获取缠K数量
观察者.second_high = 观察者.次高
观察者.second_low = 观察者.次低
观察者.actual_high = 观察者.实际高点
观察者.actual_low = 观察者.实际低点
# 笔 — creation alias
笔.create_stroke = 笔.创建笔
# 虚线 — field & extraction aliases
虚线.confirm_ckline = 虚线.确认K线
虚线.prev_gap = 虚线.前一缺口
虚线.prev_end_position = 虚线.前一结束位置
虚线.short_circuit_fix = 虚线.短路修正
虚线.get_raw_klines = 虚线.获取普K序列
虚线.get_chan_klines = 虚线.获取缠K序列
