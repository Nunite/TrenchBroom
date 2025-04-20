#!/usr/bin/env python
# -*- coding: utf-8 -*-

print("Python脚本测试成功！")
print("PyScript test successful!")

# 测试导入模块
try:
    import sys
    print(f"Python版本: {sys.version}")
    print(f"Python路径: {sys.path}")
    
    try:
        import numpy as np
        print(f"NumPy版本: {np.__version__}")
        array = np.array([1, 2, 3])
        print(f"NumPy数组: {array}")
    except ImportError:
        print("NumPy模块未安装")
    
    print("测试完成")
except Exception as e:
    print(f"错误: {e}")