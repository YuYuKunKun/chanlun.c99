"""Python enum mirrors for chan2c99 C enums — Chinese names matching chan.py."""

from enum import Enum, IntEnum


class 相对方向(IntEnum):
    """Corresponds to C enum 相对方向."""

    向上 = 0
    向下 = 1
    向上缺口 = 2
    向下缺口 = 3
    衔接向上 = 4
    衔接向下 = 5
    顺 = 6
    逆 = 7
    同 = 8

    def __str__(self):
        return f"相对方向.{self.name}"

    def __repr__(self):
        return f"相对方向.{self.name}"

    def 翻转(self) -> "相对方向":
        """Flip direction (上↔下, 顺↔逆, etc.)."""
        from ._core import _rel_dir_flip

        return 相对方向(_rel_dir_flip(int(self)))

    def 是否向上(self) -> bool:
        return self in (相对方向.向上, 相对方向.向上缺口, 相对方向.衔接向上)

    def 是否向下(self) -> bool:
        return self in (相对方向.向下, 相对方向.向下缺口, 相对方向.衔接向下)

    def 是否包含(self) -> bool:
        return self in (相对方向.顺, 相对方向.逆, 相对方向.同)

    def 是否缺口(self) -> bool:
        return self in (相对方向.向下缺口, 相对方向.向上缺口)

    def 是否衔接(self) -> bool:
        return self in (相对方向.衔接向下, 相对方向.衔接向上)

    @classmethod
    def 分析(cls, 前高: float, 前低: float, 后高: float, 后低: float) -> "相对方向":
        """Analyze relative direction of two price ranges."""
        from ._core import _rel_dir_analyze

        return cls(_rel_dir_analyze(前高, 前低, 后高, 后低))


class 分型结构(IntEnum):
    """Corresponds to C enum 分型结构."""

    上 = 0
    下 = 1
    顶 = 2
    底 = 3
    散 = 4

    @classmethod
    def 分析(cls, 左, 中, 右, 可以逆序包含: bool = False, 忽视顺序包含: bool = False) -> "分型结构 | None":
        """Analyze fractal structure from three consecutive gaps (Chan K-lines)."""
        from ._core import _frac_struct_analyze

        return cls(_frac_struct_analyze(左, 中, 右, 可以逆序包含, 忽视顺序包含))


class ObjectType(IntEnum):
    """Corresponds to C enum 对象类型 (CHAN_TYPE_*). C-level internal type tag."""

    KLINE = 0
    CHAN_KLINE = 1
    FRACTAL = 2
    GAP = 3
    DASH = 4
    HUB = 5
    SEGMENT_FEATURE = 6
    FEATURE_FRACTAL = 7
    MACD = 8
    RSI = 9
    KDJ = 10
    CONFIG = 11
    OBSERVER = 12


class 买卖点类型(str, Enum):
    """Corresponds to C enum 买卖点类型.

    传统缠论三类买卖点 + T系列扩展六类买卖点。
    """

    一买 = "一买"
    一卖 = "一卖"
    二买 = "二买"
    二卖 = "二卖"
    三买 = "三买"
    三卖 = "三卖"
    T1买 = "T1买"
    T1卖 = "T1卖"
    T1P买 = "T1P买"
    T1P卖 = "T1P卖"
    T2买 = "T2买"
    T2卖 = "T2卖"
    T2S买 = "T2S买"
    T2S卖 = "T2S卖"
    T3A买 = "T3A买"
    T3A卖 = "T3A卖"
    T3B买 = "T3B买"
    T3B卖 = "T3B卖"

    def __str__(self) -> str:
        return self.name

    def __repr__(self) -> str:
        return self.name

    @property
    def 是买点(self) -> bool:
        return "买" in self.value

    @property
    def 是卖点(self) -> bool:
        return "卖" in self.value


# Backward-compatible aliases
RelativeDirection = 相对方向
FractalStructure = 分型结构
TradePointType = 买卖点类型

# Enum value aliases for old code
for _cls, _prefix in ((相对方向, ""), (分型结构, "")):
    pass
# RelativeDirection value aliases (class attributes, done via simple assignments)
# These allow: from chan_c99.enums import RelativeDirection; RelativeDirection.UP
相对方向.UP = 相对方向.向上
相对方向.DOWN = 相对方向.向下
相对方向.GAP_UP = 相对方向.向上缺口
相对方向.GAP_DOWN = 相对方向.向下缺口
相对方向.JUNCTION_UP = 相对方向.衔接向上
相对方向.JUNCTION_DOWN = 相对方向.衔接向下
相对方向.FORWARD = 相对方向.顺
相对方向.REVERSE = 相对方向.逆
相对方向.SAME = 相对方向.同

分型结构.RISING = 分型结构.上
分型结构.FALLING = 分型结构.下
分型结构.TOP = 分型结构.顶
分型结构.BOTTOM = 分型结构.底
分型结构.SCATTERED = 分型结构.散
