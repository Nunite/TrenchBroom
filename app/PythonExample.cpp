#include "python_interpreter/pythoninterpreter.h"
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

// 简单的路径父目录获取函数
std::string getParentDir(const std::string& path) {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

int main(int argc, char* argv[]) {
    try {
        ::std::cout << "Starting TrenchBroom Python embedding example..." << ::std::endl;

        ::std::string exePath(argv[0]);
        ::std::string exeDir = getParentDir(exePath);
        ::std::cout << "Executable directory: " << exeDir << ::std::endl;

        ::std::string pythonDir = PythonInterpreter::getPythonDir(exeDir);
        ::std::cout << "Python directory: " << pythonDir << ::std::endl;

        ::std::string scriptsPath = pythonDir + PATH_SEPARATOR + "PyScripts";
        ::std::cout << "Scripts path: " << scriptsPath << ::std::endl;
        
        ::std::vector<::std::string> externalSearchPaths {
            exeDir,
            scriptsPath
        };
        
        // Initialize Python interpreter
        ::std::cout << "Initializing Python interpreter..." << ::std::endl;
        PythonInterpreter interp(exePath, externalSearchPaths);
        
        // Execute simple Python code
        ::std::cout << "\nExecuting simple Python code..." << ::std::endl;
        interp.executeCode(
            "print(\"Hello from Python!\")\n"
            "result = 42 * 2\n"
            "print(f\"Calculation result: {result}\")\n"
        );
        
        // Check NumPy support
        ::std::cout << "\nChecking NumPy support..." << ::std::endl;
        interp.executeCode(
            "try:\n"
            "    import numpy as np\n"
            "    print(f\"NumPy version: {np.__version__}\")\n"
            "    arr = np.array([1, 2, 3, 4, 5])\n"
            "    print(f\"NumPy array: {arr}\")\n"
            "    print(f\"NumPy array mean: {np.mean(arr)}\")\n"
            "except ImportError as e:\n"
            "    print(f\"NumPy import error: {e}\")\n"
            "    print(\"Attempting to install NumPy...\")\n"
            "    try:\n"
            "        import sys\n"
            "        import subprocess\n"
            "        try:\n"
            "            import pip\n"
            "        except ImportError:\n"
            "            print(\"Installing pip...\")\n"
            "            subprocess.check_call([sys.executable, \"-m\", \"ensurepip\"])\n"
            "        print(\"Installing NumPy...\")\n"
            "        subprocess.check_call([sys.executable, \"-m\", \"pip\", \"install\", \"numpy\", \"--user\"])\n"
            "        print(\"Retrying NumPy import...\")\n"
            "        import numpy as np\n"
            "        print(f\"NumPy version: {np.__version__}\")\n"
            "        arr = np.array([1, 2, 3, 4, 5])\n"
            "        print(f\"NumPy array: {arr}\")\n"
            "        print(f\"NumPy array mean: {np.mean(arr)}\")\n"
            "    except Exception as install_err:\n"
            "        print(f\"Error installing NumPy: {install_err}\")\n"
        );
        
        // Test embedded C++ module
        ::std::cout << "\nTesting embedded C++ module..." << ::std::endl;
        interp.executeCode(
            "try:\n"
            "    import fast_calc\n"
            "    print(f\"Module docstring: {fast_calc.__doc__}\")\n"
            "    print(f\"1 + 2 = {fast_calc.add(1, 2)}\")\n"
            "    print(f\"5 - 3 = {fast_calc.subtract(5, 3)}\")\n"
            "    print(f\"4 * 3 = {fast_calc.multiply(4, 3)}\")\n"
            "    print(f\"10 / 2 = {fast_calc.divide(10, 2)}\")\n"
            "    print(f\"Vector dot product: {fast_calc.dot_product([1, 2, 3], [4, 5, 6])}\")\n"
            "    # Test error handling\n"
            "    try:\n"
            "        fast_calc.divide(1, 0)\n"
            "    except ValueError as e:\n"
            "        print(f\"Expected division by zero error: {e}\")\n"
            "    try:\n"
            "        fast_calc.dot_product([1, 2], [1, 2, 3])\n"
            "    except ValueError as e:\n"
            "        print(f\"Expected vector dimension error: {e}\")\n"
            "except ImportError as e:\n"
            "    print(f\"fast_calc module import error: {e}\")\n"
        );
        
        // Test external script module
        ::std::cout << "\nTesting custom Python module..." << ::std::endl;
        interp.executeCode(
            "try:\n"
            "    import calc\n"
            "    print(f\"10 + 20 = {calc.add(10, 20)}\")\n"
            "    print(f\"30 - 15 = {calc.subtract(30, 15)}\")\n"
            "    print(f\"7 * 8 = {calc.multiply(7, 8)}\")\n"
            "    print(f\"100 / 4 = {calc.divide(100, 4)}\")\n"
            "    # Test error handling\n"
            "    try:\n"
            "        calc.divide(1, 0)\n"
            "    except ValueError as e:\n"
            "        print(f\"Expected division by zero error: {e}\")\n"
            "except ImportError as e:\n"
            "    print(f\"calc module import error: {e}\")\n"
        );
        
        ::std::cout << "\nPython embedding example finished!" << ::std::endl;
        
    } catch (const ::std::exception& e) {
        ::std::cerr << "Error: " << e.what() << ::std::endl;
        return 1;
    }

    return 0;
} 