"""calc.py - TrenchBroom Python计算模块示例"""


def add(i, j):
    """加法函数"""
    return i + j


def subtract(i, j):
    """减法函数"""
    return i - j


def multiply(i, j):
    """乘法函数"""
    return i * j


def divide(i, j):
    """除法函数"""
    if j == 0:
        raise ValueError("除数不能为零")
    return i / j
