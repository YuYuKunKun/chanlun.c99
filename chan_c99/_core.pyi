"""Type stubs for chan_c99 native bindings.

All class, property, and method names use Chinese (matching chan.py).
English aliases are available at module level for backward compatibility.
"""

from typing import Any
from enum import IntEnum

# ---- Enums ----

class 相对方向(IntEnum):
    向上: int
    向下: int
    向上缺口: int
    向下缺口: int
    衔接向上: int
    衔接向下: int
    顺: int
    逆: int
    同: int
    def 翻转(self) -> '相对方向': ...
    @classmethod
    def 分析(cls, 前高: float, 前低: float, 后高: float, 后低: float) -> '相对方向': ...

class 分型结构(IntEnum):
    上: int
    下: int
    顶: int
    底: int
    散: int
    @classmethod
    def 分析(cls, 左: Any, 中: Any, 右: Any,
             可以逆序包含: bool = False,
             忽视顺序包含: bool = False) -> '分型结构': ...

class ObjectType(IntEnum): ...

# ---- Core Types ----

class K线:
    """Raw candlestick (OHLCV bar)."""
    标识: str
    序号: int
    周期: int
    时间戳: int
    高: float
    低: float
    开盘价: float
    收盘价: float
    成交量: float
    方向: int
    ptr: int
    owns: bool

    @classmethod
    def 创建普K(cls, 标识: str, 时间戳: int, 开盘价: float, 高: float,
                低: float, 收盘价: float, 成交量: float, 序号: int = 0,
                周期: int = 0) -> 'K线': ...
    def retain(self) -> None: ...
    def release(self) -> None: ...

class 缠论K线:
    """Merged Chan K-line with direction and fractal type."""
    序号: int
    时间戳: int
    高: float
    低: float
    方向: int
    分型: int
    分型特征值: float
    周期: int
    标识: str
    原始起始序号: int
    原始结束序号: int
    标的K线: K线
    ptr: int
    owns: bool

class 缺口:
    """Gap between two price ranges."""
    高: float
    低: float

class 分型:
    """Fractal — three consecutive Chan K-lines."""
    左: 缠论K线
    中: 缠论K线
    右: 缠论K线
    结构: int
    时间戳: int
    分型特征值: float
    ptr: int
    owns: bool

class 虚线:
    """Abstract dash line — base for strokes and segments."""
    序号: int
    标识: str
    级别: int
    文: 分型
    武: 分型
    方向: int
    高: float
    低: float
    有效性: bool
    模式: str
    确认K线: 缠论K线
    前一缺口: 缺口
    前一结束位置: '虚线'
    短路修正: bool
    基础序列: list
    特征序列: list
    实_中枢序列: list
    虚_中枢序列: list
    合_中枢序列: list
    ptr: int
    owns: bool

    def 之前是(self, other: '虚线') -> bool: ...
    def 之后是(self, other: '虚线') -> bool: ...
    def 获取普K序列(self, obs: '观察者') -> list: ...
    def 获取缠K序列(self, obs: '观察者') -> list: ...
    @classmethod
    def 创建笔(cls, 文: 分型, 武: 分型, 有效性: bool = True) -> '笔': ...
    @classmethod
    def 创建线段(cls, arr: '动态数组') -> '线段': ...

class 笔(虚线):
    """Stroke — a sequence of Chan K-lines between two fractals."""
    @classmethod
    def 创建笔(cls, 文: 分型, 武: 分型, 有效性: bool = True) -> '笔': ...
    def 相对关系(self, config: '缠论配置') -> bool: ...
    @classmethod
    def 分析(cls, 初始分型: 分型 | None, 分型序列: '动态数组',
             笔序列: '动态数组', 缠K序列: '动态数组',
             普K序列: '动态数组', 配置: '缠论配置') -> None: ...
    @classmethod
    def 获取缠K数量(cls, 缠K序列: '动态数组', 笔序列: '动态数组',
                    配置: '缠论配置') -> int: ...
    @classmethod
    def 次高(cls, 缠K序列: '动态数组',
             笔内相同终点取舍: bool = False) -> 缠论K线 | None: ...
    @classmethod
    def 次低(cls, 缠K序列: '动态数组',
             笔内相同终点取舍: bool = False) -> 缠论K线 | None: ...
    @classmethod
    def 实际高点(cls, 缠K序列: '动态数组',
                 笔内相同终点取舍: bool = False) -> 缠论K线 | None: ...
    @classmethod
    def 实际低点(cls, 缠K序列: '动态数组',
                 笔内相同终点取舍: bool = False) -> 缠论K线 | None: ...
    @classmethod
    def 以文会友(cls, 笔序列: '动态数组', 文: 分型) -> '笔 | None': ...
    @classmethod
    def 以武会友(cls, 笔序列: '动态数组', 武: 分型) -> '笔 | None': ...
    @classmethod
    def 根据缠K找笔(cls, 笔序列: '动态数组', 缠K: 缠论K线,
                    偏移: int = 0) -> '笔 | None': ...

class 线段(虚线):
    """Segment — a sequence of overlapping strokes."""
    def 四象(self) -> str: ...
    def 特征分型终结(self) -> bool: ...
    def 特征序列状态(self) -> tuple: ...
    def 获取缺口(self) -> 缺口: ...
    def 查找贯穿伤(self) -> 虚线: ...
    @classmethod
    def 创建线段(cls, arr: '动态数组') -> '线段': ...
    @classmethod
    def 分析(cls, 笔序列: '动态数组', 线段序列: '动态数组',
             配置: '缠论配置') -> None: ...

class 线段特征:
    """Segment feature — feature sequence element."""
    序号: int
    标识: str
    方向: int
    文: 分型
    武: 分型
    高: float
    低: float
    元素: list
    ptr: int
    owns: bool
    @classmethod
    def 静态分析(cls, 虚线序列: '动态数组', 方向: int, 四象: str,
                是否忽视: bool, 结果: '动态数组') -> None: ...

class 中枢:
    """Hub — overlapping zone of three consecutive dashes."""
    序号: int
    标识: str
    级别: int
    方向: int
    高: float
    低: float
    高高: float
    低低: float
    完整性: bool
    第三买卖线: 虚线
    本级_第三买卖线: 虚线
    元素: list
    ptr: int
    owns: bool
    def 当前状态(self) -> str: ...
    def 获取序列(self) -> '动态数组': ...
    @classmethod
    def 分析(cls, 虚线序列: '动态数组', 中枢序列: '动态数组',
             跳过首部: bool = False, 标识: str = 'hub') -> None: ...

class 动态数组:
    """Dynamic array — Python list-like wrapper for C 动态数组."""
    def append(self, item: Any) -> None: ...
    def pop(self) -> Any: ...
    def pop_typed(self, pytype: type) -> Any: ...
    def clear(self) -> None: ...
    def __len__(self) -> int: ...
    def __getitem__(self, i: int) -> Any: ...

class 背驰分析:
    """Divergence analysis (背驰分析) — static namespace matching chan.py 背驰分析."""
    @classmethod
    def MACD背驰(cls, 进入段: 虚线, 离开段: 虚线,
                  普K序列: 动态数组, 方式: str = 'default') -> bool: ...
    @classmethod
    def 斜率背驰(cls, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    @classmethod
    def 测度背驰(cls, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    @classmethod
    def 全量背驰(cls, 进入段: 虚线, 离开段: 虚线,
                  普K序列: 动态数组) -> bool: ...
    @classmethod
    def 任意背驰(cls, 进入段: 虚线, 离开段: 虚线,
                  普K序列: 动态数组) -> bool: ...
    @classmethod
    def 配置背驰(cls, 进入段: 虚线, 离开段: 虚线,
                  普K序列: 动态数组, 配置: 缠论配置) -> bool: ...
    @classmethod
    def 任选背驰(cls, 进入段: 虚线, 离开段: 虚线,
                  普K序列: 动态数组) -> bool: ...
    @classmethod
    def 背驰模式(cls, 进入段: 虚线, 离开段: 虚线,
                  普K序列: 动态数组, 配置: 缠论配置,
                  模式: str = 'default') -> bool: ...

class 缠论配置:
    """Configuration for the Chan Theory analysis pipeline."""
    ptr: int
    owns: bool
    def __init__(self, no_push: bool = False) -> None: ...
    def __getitem__(self, key: str) -> Any: ...
    def __setitem__(self, key: str, value: Any) -> None: ...
    def release(self) -> None: ...

class 观察者:
    """Top-level orchestrator for the Chan Theory analysis pipeline."""
    # Sequence views (lazy, support len/[]/iter)
    普通K线序列: Any
    缠论K线序列: Any
    基础缠K序列: Any
    分型序列: Any
    笔序列: Any
    笔_中枢序列: Any
    线段序列: Any
    中枢序列: Any
    扩展线段序列: Any
    扩展中枢序列: Any
    扩展线段序列_线段: Any
    扩展中枢序列_线段: Any
    线段_线段序列: Any
    线段_中枢序列: Any
    扩展线段序列_扩展线段: Any
    扩展中枢序列_扩展线段: Any
    ptr: int
    owns: bool

    def __init__(self, 符号: str, 周期: int, 配置: 缠论配置) -> None: ...
    def __enter__(self) -> '观察者': ...
    def __exit__(self, *args: Any) -> None: ...
    def 增加原始K线(self, k: K线) -> None: ...
    @classmethod
    def 读取数据文件(cls, 文件路径: str, 配置: 缠论配置) -> '观察者': ...
    def release(self) -> None: ...

    # Pipeline analysis
    def 笔_分析(self, config: 缠论配置) -> None: ...
    def 线段_分析(self, config: 缠论配置) -> None: ...
    def 扩展线段_分析(self, config: 缠论配置) -> None: ...
    def 笔_中枢分析(self, 跳过首部: bool = False, 标识: str = 'stroke_hub') -> None: ...
    def 线段_中枢分析(self, 跳过首部: bool = False, 标识: str = 'seg_hub') -> None: ...

    # Stroke utilities
    def 获取缠K数量(self, config: 缠论配置) -> int: ...
    def 次高(self, same_end: bool) -> 缠论K线: ...
    def 次低(self, same_end: bool) -> 缠论K线: ...
    def 实际高点(self, same_end: bool) -> 缠论K线: ...
    def 实际低点(self, same_end: bool) -> 缠论K线: ...

    # Stroke lookup
    def 以文会友(self, fractal: 分型) -> 笔: ...
    def 以武会友(self, fractal: 分型) -> 笔: ...
    def 根据缠K找笔(self, 缠K: 缠论K线, 偏移: int = 0) -> 笔: ...

    # Divergence analysis
    def 背驰_斜率(self, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    def 背驰_测度(self, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    def 背驰_MACD(self, 进入段: 虚线, 离开段: 虚线, 方式: str = 'default') -> bool: ...
    def 背驰_全量(self, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    def 背驰_任意(self, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    def 背驰_配置(self, 进入段: 虚线, 离开段: 虚线, 配置: 缠论配置 = None) -> bool: ...
    def 背驰_任选(self, 进入段: 虚线, 离开段: 虚线) -> bool: ...
    def 背驰_模式(self, 进入段: 虚线, 离开段: 虚线, 配置: 缠论配置 = None,
                 模式: str = 'default') -> bool: ...

# ---- English aliases ----
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
ChanConfig = 缠论配置
Observer = 观察者
Divergence = 背驰分析
RelativeDirection = 相对方向
FractalStructure = 分型结构
