"""
Setup script for chan_c99 — Python bindings for the chan2c99 缠论 library.

Usage:
    pip install .              # install into site-packages
    pip install -e .           # editable install
    python setup.py build_ext --inplace  # build _core.so in-place
"""

from setuptools import setup, Extension, find_packages
import sys
import os

# Platform-specific compiler flags
if sys.platform == 'win32':
    extra_compile_args = ['/std:c11', '/O2', '/utf-8']
else:
    extra_compile_args = [
        '-std=c99', '-Wall', '-O2', '-fPIC',
        '-finput-charset=UTF-8',
    ]

extra_link_args = []
if sys.platform not in ('win32', 'darwin'):
    extra_link_args.append('-lm')

chan_ext = Extension(
    'chan_c99._core',
    sources=['chan.c', '_core.c'],
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
    include_dirs=['.'],
    depends=['chan.h'],
)

setup(
    name='chan-c99',
    version='0.1.5',
    description='Python bindings for the chan2c99 缠论 technical analysis library',
    author='YuYuKunKun',
    license='MIT',
    packages=['chan_c99'],
    ext_modules=[chan_ext],
    python_requires='>=3.8',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Financial and Insurance Industry',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: C',
        'Programming Language :: Python :: 3',
        'Topic :: Office/Business :: Financial :: Investment',
    ],
)
