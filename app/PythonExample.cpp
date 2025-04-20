#include "python_interpreter/pythoninterpreter.h"
#include "python_interpreter/embeddedmoduleexample.h" // 包含 dummy 函数声明
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <pybind11/embed.h>

namespace py = pybind11;

#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

// 简单的路径父目录获取函数
::std::string getParentDir(const ::std::string& path) {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != ::std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

int main(int argc, char* argv[]) {
    // 调用 dummy 函数强制链接 embeddedmoduleexample.obj
    ensure_fast_calc_linked(); 
    
    try {
        ::std::cout << "Starting TrenchBroom Python embedding example..." << ::std::endl;

        ::std::string exePath(argv[0]);
        ::std::string exeDir = getParentDir(exePath);
        ::std::cout << "Executable directory: " << exeDir << ::std::endl;

        ::std::string pythonDir = PythonInterpreter::getPythonDir(exeDir);
        ::std::cout << "Python directory: " << pythonDir << ::std::endl;

        ::std::string scriptsPath = pythonDir + PATH_SEPARATOR + "PyScripts";
        ::std::cout << "Scripts path: " << scriptsPath << ::std::endl;
        
        ::std::string sitePackagesPath = pythonDir + PATH_SEPARATOR + "Lib" + PATH_SEPARATOR + "site-packages";
        ::std::cout << "Target site-packages: " << sitePackagesPath << ::std::endl;

        ::std::vector<::std::string> externalSearchPaths {
            exeDir,
            scriptsPath
        };
        
        ::std::cout << "Initializing Python interpreter..." << ::std::endl;
        PythonInterpreter interp(exePath, externalSearchPaths);
        
        // Execute simple Python code and check paths
        ::std::cout << "\nExecuting simple Python code and checking paths..." << ::std::endl;
        interp.executeCode(R"raw_string(
import sys
print("Hello from Python!")
print("Python executable: " + sys.executable)
print("Python prefix: " + sys.prefix)
result = 42 * 2
print("Calculation result: " + str(result))
)raw_string");
        
        // Check NumPy support
        ::std::cout << "\nChecking NumPy support..." << ::std::endl;
        ::std::string sitePackagesPathForPython = sitePackagesPath;
        #ifdef _WIN32
            ::std::replace(sitePackagesPathForPython.begin(), sitePackagesPathForPython.end(), '\\', '/');
        #endif
        
        // 将路径传递给 Python 全局变量
        py::globals()["target_path_cpp"] = py::cast(sitePackagesPathForPython);

        interp.executeCode(R"raw_string(
import sys
import os
import subprocess # 确保导入 subprocess

# 从 C++ 获取目标路径
target_path = target_path_cpp 

try:
    print("Attempting to import NumPy...")
    import numpy as np
    print("NumPy version: " + np.__version__)
    arr = np.array([1, 2, 3, 4, 5])
    print("NumPy array: " + str(arr))
    print("NumPy array mean: " + str(np.mean(arr)))
except ImportError as e:
    print("NumPy import error: " + str(e))
    print("Attempting to install NumPy...")
    try:
        # import subprocess # 已在开头导入
        print("Ensuring pip is installed...")
        try:
            import pip
        except ImportError:
            try:
                # 重定向输出避免编码问题
                subprocess.check_call([sys.executable, "-m", "ensurepip"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception as ensure_pip_err:
                print("Could not ensure pip: " + str(ensure_pip_err))
        print("Installing NumPy to target: " + target_path)
        try:
            # 重定向输出避免编码问题
            subprocess.check_call([sys.executable, "-m", "pip", "install", "numpy", "--target=" + target_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as install_numpy_err:
            print("Could not install NumPy with pip: " + str(install_numpy_err))
            if not os.path.exists(target_path):
                try:
                    os.makedirs(target_path)
                    print("Created target directory: " + target_path)
                except Exception as mkdir_err:
                    print("Could not create target directory: " + str(mkdir_err))
            if target_path not in sys.path:
                 sys.path.append(target_path)
                 print("Manually added target_path to sys.path")
            print("Try installing again after ensuring directory exists and path added...")
            try:
                # 再次尝试安装，仍然重定向输出
                subprocess.check_call([sys.executable, "-m", "pip", "install", "numpy", "--target=" + target_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception as install_numpy_err2:
                 print("Second attempt to install NumPy also failed: " + str(install_numpy_err2))
                 # 不再重新抛出，只打印错误，避免程序中断
        
        print("Installation process finished. Retrying NumPy import...")
        if target_path not in sys.path:
            sys.path.append(target_path)
        print("Updated sys.path: " + str(sys.path))
        try:
            import numpy as np
            print("NumPy version: " + np.__version__)
            arr = np.array([1, 2, 3, 4, 5])
            print("NumPy array: " + str(arr))
            print("NumPy array mean: " + str(np.mean(arr)))
        except ImportError as e2:
             print("Still cannot import NumPy after installation attempt: " + str(e2))
             try:
                 print("Contents of target site-packages (" + target_path + "):")
                 print(os.listdir(target_path))
             except Exception as listdir_err:
                 print("Could not list target directory: " + str(listdir_err))
    except Exception as install_err:
        print("Error during NumPy check/install process: " + str(install_err))
)raw_string");
        
        // Test embedded C++ module
        ::std::cout << "\nTesting embedded C++ module..." << ::std::endl;
        interp.executeCode(R"raw_string(
try:
    import fast_calc
    print("Module docstring: " + fast_calc.__doc__)
    print("1 + 2 = " + str(fast_calc.add(1, 2)))
    print("5 - 3 = " + str(fast_calc.subtract(5, 3)))
    print("4 * 3 = " + str(fast_calc.multiply(4, 3)))
    print("10 / 2 = " + str(fast_calc.divide(10, 2)))
    print("Vector dot product: " + str(fast_calc.dot_product([1, 2, 3], [4, 5, 6])))
    # Test error handling
    try:
        fast_calc.divide(1, 0)
    except ValueError as e:
        print("Expected division by zero error: " + str(e))
    try:
        fast_calc.dot_product([1, 2], [1, 2, 3])
    except ValueError as e:
        print("Expected vector dimension error: " + str(e))
except ImportError as e:
    print("fast_calc module import error: " + str(e))
)raw_string");
        
        // Test external script module
        ::std::cout << "\nTesting custom Python module..." << ::std::endl;
        interp.executeCode(R"raw_string(
try:
    import calc
    print("10 + 20 = " + str(calc.add(10, 20)))
    print("30 - 15 = " + str(calc.subtract(30, 15)))
    print("7 * 8 = " + str(calc.multiply(7, 8)))
    print("100 / 4 = " + str(calc.divide(100, 4)))
    # Test error handling
    try:
        calc.divide(1, 0)
    except ValueError as e:
        print("Expected division by zero error: " + str(e))
except ImportError as e:
    print("calc module import error: " + str(e))
)raw_string");
        
        ::std::cout << "\nPython embedding example finished!" << ::std::endl;
        
    } catch (const ::std::exception& e) {
        ::std::cerr << "Error: " << e.what() << ::std::endl;
        return 1;
    }

    return 0;
} 