"""
Setup script for chan_c99 — Python bindings for the chan2c99 缠论 library.

Usage:
    pip install .                  # install into site-packages
    pip install -e .               # editable install
    python -m build                # build sdist + wheel

Compiler notes:
    源代码使用中文标识符（UTF-8 编码），需要编译器支持输入源字符集指定。
    - GCC:        -finput-charset=UTF-8
    - Clang:      -finput-charset=UTF-8 (since Clang 3.3)
    - MSVC:       /utf-8
    - 未知编译器: 跳过 charset flag，假设系统 locale 为 UTF-8


"""

from setuptools import setup, Extension
import os
import sys
import sysconfig
import tempfile
import subprocess


def _检测编译器_charset_flag():
    """试编译一个含中文标识符的微型 C 文件，检测需要的源码字符集标志。

    返回 (compile_args, link_args) 元组。
    """
    # 包含中文标识符的探测代码（必须能通过 -std=c99 编译）
    探测源码 = """
    #include <stdlib.h>
    int 测试变量 = 42;
    int 获取测试(void) { return 测试变量; }
    """

    link_args = []
    if sys.platform not in ("win32", "darwin"):
        link_args.extend(["-lm", "-lpthread"])

    # 获取编译器（优先用 CC 环境变量，其次用 sysconfig）
    cc = os.environ.get("CC", sysconfig.get_config_var("CC") or "cc")

    # 基础 C99 编译参数
    if sys.platform == "win32":
        base_args = ["/std:c11", "/O2"]
    else:
        base_args = ["-std=c99", "-Wall", "-O2", "-fPIC"]

    # 候选 charset 标志列表，按优先级排列
    candidates = ["-finput-charset=UTF-8", "/utf-8"]

    # 先试不带 charset flag（某些系统 UTF-8 是默认）
    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".c", delete=False, encoding="utf-8") as f:
            f.write(探测源码)
            tmp_src = f.name
        with tempfile.NamedTemporaryFile(suffix=".o", delete=False) as f:
            tmp_obj = f.name

        cmd = [cc] + base_args + ["-c", "-o", tmp_obj, tmp_src]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            # 不需要 charset flag
            return base_args, link_args
    except Exception:
        pass
    finally:
        for p in [tmp_src, tmp_obj]:
            try:
                os.unlink(p)
            except OSError:
                pass

    # 逐个尝试 charset 候选
    for flag in candidates:
        try:
            with tempfile.NamedTemporaryFile(mode="w", suffix=".c", delete=False, encoding="utf-8") as f:
                f.write(探测源码)
                tmp_src = f.name
            with tempfile.NamedTemporaryFile(suffix=".o", delete=False) as f:
                tmp_obj = f.name

            cmd = [cc] + base_args + [flag, "-c", "-o", tmp_obj, tmp_src]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode == 0:
                return base_args + [flag], link_args
        except Exception:
            continue
        finally:
            for p in [tmp_src, tmp_obj]:
                try:
                    os.unlink(p)
                except OSError:
                    pass

    # 所有尝试都失败，发出警告但仍然继续（用户可能设置 CC 为能工作的编译器）
    import warnings

    warnings.warn("无法自动检测 C 编译器对 UTF-8 源码的支持。请设置 CC 环境变量指向 GCC 或 Clang。")
    return base_args, link_args


编译参数, 链接参数 = _检测编译器_charset_flag()

chan_ext = Extension(
    "chan_c99._core",
    sources=["chan.c", "_core.c"],
    extra_compile_args=编译参数,
    extra_link_args=链接参数,
    include_dirs=["."],
    depends=["chan.h"],
)

setup(
    name="chan-c99",
    version="0.1.6",
    description="Python bindings for the chan2c99 缠论 technical analysis library",
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    author="YuYuKunKun",
    license="MIT",
    packages=["chan_c99"],
    ext_modules=[chan_ext],
    python_requires=">=3.8",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Financial and Insurance Industry",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: C",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: Python :: 3.14",
        "Topic :: Office/Business :: Financial :: Investment",
        "Operating System :: POSIX :: Linux",
        "Operating System :: MacOS",
        "Operating System :: Microsoft :: Windows",
    ],
)
