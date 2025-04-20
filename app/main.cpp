#include "python_interpreter/pythoninterpreter.h"
#include <filesystem>

int main(int argc, char* argv[]) {
    // 初始化Python解释器
    try {
        std::filesystem::path exePath(argv[0]);
        auto exeDir = exePath.parent_path();
        auto pythonDir = PythonInterpreter::getPythonDir(exeDir);
        auto scriptsPath = pythonDir / "PyScripts";
        
        std::vector<std::filesystem::path> externalSearchPaths {
            exeDir,
            scriptsPath
        };
        
        PythonInterpreter interp(exePath, externalSearchPaths);
        
        // 执行一段Python代码
        interp.executeCode(R"(
            import sys
            print("Python版本:", sys.version)
            print("Python路径:", sys.path)
            
            try:
                import numpy as np
                print("NumPy版本:", np.__version__)
                arr = np.array([1, 2, 3, 4, 5])
                print("NumPy数组:", arr)
            except ImportError as e:
                print("未能导入NumPy:", e)
                
            # 测试嵌入式模块
            try:
                import fast_calc
                print("1 + 2 =", fast_calc.add(1, 2))
                print("向量点积:", fast_calc.dot_product([1, 2, 3], [4, 5, 6]))
            except ImportError as e:
                print("未能导入fast_calc模块:", e)
                
            # 测试外部脚本模块
            try:
                import calc
                print("自定义加法:", calc.add(10, 20))
                print("自定义乘法:", calc.multiply(10, 20))
            except ImportError as e:
                print("未能导入calc模块:", e)
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Python解释器错误: " << e.what() << std::endl;
    }

    return 0;
} 