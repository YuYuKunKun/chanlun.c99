"""
chan_c99 — Python bindings for the chan2c99 缠论 technical analysis library.

Provides Python access to the C99 port of the Chan Theory (缠论) K-line
analysis pipeline. The C library performs multi-level market structure
analysis (strokes, segments, hubs) with reference-counted memory management.

All class, property, and method names follow chan.py's Chinese naming
convention. English backward-compatible aliases are also exported.

Usage:
    from chan_c99 import 观察者, 缠论配置, K线

    # Batch mode: load from .nb data file
    config = 缠论配置(no_push=True)
    obs = 观察者.读取数据文件('btcusd-300-xxx.nb', config)
    print(f'Strokes: {len(obs.笔序列)}, Hubs: {len(obs.中枢序列)}')

    obs.release()
"""

import os

# Mode switch: set CHAN_C99_MODE=ctypes to use the old ctypes bindings
_mode = os.environ.get('CHAN_C99_MODE', 'native')

if _mode == 'ctypes':
    from ._bindings import (
        load_library,
        观察者, 缠论配置,
        K线, 缠论K线, 分型, 虚线, 笔, 线段, 线段特征, 中枢, 缺口,
    )
    # English aliases
    from ._bindings import (
        Observer, ChanConfig,
        KLine, ChanKLine, Fractal, DashLine, Stroke, Segment,
        SegmentFeature, Hub, Gap,
    )
else:
    # Native Python C API extension
    from ._core import (
        观察者, 缠论配置, 背驰分析,
        K线, 缠论K线, 分型, 虚线, 笔, 线段, 线段特征, 中枢, 缺口, 动态数组,
        _设置自定义strrepr,
    )

    # English backward-compatible aliases
    Observer = 观察者
    ChanConfig = 缠论配置
    KLine = K线
    ChanKLine = 缠论K线
    Fractal = 分型
    DashLine = 虚线
    Stroke = 笔
    Segment = 线段
    SegmentFeature = 线段特征
    Hub = 中枢
    Gap = 缺口
    DynArray = 动态数组
    Divergence = 背驰分析

    # Placeholder for ctypes-only load_library
    def load_library(path=None):
        """Native bindings don't need explicit library loading. No-op."""
        pass


from .enums import (
    相对方向, 分型结构, ObjectType,
    RelativeDirection, FractalStructure,
)

__all__ = [
    # Chinese (primary)
    '观察者', '缠论配置', '背驰分析',
    'K线', '缠论K线', '分型', '虚线', '笔', '线段', '线段特征', '中枢', '缺口', '动态数组',
    '相对方向', '分型结构', 'ObjectType',
    # English (backward-compatible aliases)
    'Observer', 'ChanConfig', 'Divergence',
    'KLine', 'ChanKLine', 'Fractal', 'DashLine', 'Stroke', 'Segment',
    'SegmentFeature', 'Hub', 'Gap', 'DynArray',
    'RelativeDirection', 'FractalStructure',
    'load_library', '_设置自定义strrepr',
]
