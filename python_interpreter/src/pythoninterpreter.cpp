#include "python_interpreter/pythoninterpreter.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <pybind11/embed.h>
namespace py = pybind11;

// Internal function to handle differences in paths
// Paths use wchar_t on Windows and char on other platforms
// Provide cross-platform string assignments with function overloading
PyStatus setPyConfigString(PyConfig* config, wchar_t **config_str, const char* val) {
    return PyConfig_SetBytesString(config, config_str, val);
}
PyStatus setPyConfigString(PyConfig* config, wchar_t **config_str, const wchar_t* val) {
    return PyConfig_SetString(config, config_str, val);
}

static void PyStatusExitOnError(PyStatus status)
{
  if (PyStatus_Exception(status))
  {
    ::std::cerr << "Python initialization error!";
    // This calls `exit`
    Py_ExitStatusException(status);
  }
}

PythonInterpreter::PythonInterpreter(::std::string exePath, ::std::vector<::std::string> externalSearchPaths, bool useSystemPython) {
    
    ::std::cerr << "[DEBUG] PythonInterpreter constructor started." << ::std::endl;
    ::std::cerr << "[DEBUG]   exePath: " << exePath << ::std::endl;
    ::std::cerr << "[DEBUG]   externalSearchPaths: [";
    for (const auto& p : externalSearchPaths) { ::std::cerr << "\"" << p << "\", "; }
    ::std::cerr << "]" << ::std::endl;
    ::std::cerr << "[DEBUG]   useSystemPython: " << useSystemPython << ::std::endl;

    PyStatus status;
    PyPreConfig preconfig;
    if (useSystemPython) {
        PyPreConfig_InitPythonConfig(&preconfig);
    } else {
        PyPreConfig_InitIsolatedConfig(&preconfig);
    }
    preconfig.utf8_mode = 1;

    status = Py_PreInitialize(&preconfig);
    PyStatusExitOnError(status);

    // Python now uses UTF-8
    PyConfig config;
    if (useSystemPython) {
        PyConfig_InitPythonConfig(&config);
    } else {
        PyConfig_InitIsolatedConfig(&config);
    }
    
    config.user_site_directory = useSystemPython;
    
    // Get parent directory of executable path
    ::std::string exeDir;
    size_t lastSlash = exePath.find_last_of("/\\");
    if (lastSlash != ::std::string::npos) {
        exeDir = exePath.substr(0, lastSlash);
    }
    else {
        exeDir = ".";
    }
    ::std::cerr << "[DEBUG]   Calculated exeDir: " << exeDir << ::std::endl;

    auto pythonDir = getPythonDir(exeDir);
    ::std::cerr << "[DEBUG]   Calculated pythonDir: " << pythonDir << ::std::endl;
    
    auto pythonExe = getPythonExe(pythonDir);
    ::std::cerr << "[DEBUG]   Calculated pythonExe: " << pythonExe << ::std::endl;

    status = setPyConfigString(&config, &config.program_name, pythonExe.c_str());
    PyStatusExitOnError(status);
    
    // Python searches for modules relative to home
    // On Mac/Linux:
    // home/bin/pythonMajor.Minor (executable)
    // home/lib/pythonMajor.Minor/site-packages (installed Python modules)
    // On Windows:
    // home/
    // home/Lib/site-packages
    auto pythonHome = pythonDir;
    ::std::cerr << "[DEBUG]   Calculated pythonHome: " << pythonHome << ::std::endl;

    status = setPyConfigString(&config, &config.home, pythonHome.c_str());
    PyStatusExitOnError(status);
    
    // --- 修改模块搜索路径设置 --- 
    config.module_search_paths_set = 1; // 告诉 Python 我们要手动设置搜索路径

    // 构建搜索路径列表 (宽字符串)
    ::std::vector<::std::wstring> searchPathsW;
    // 添加基础 Python 路径 (根据平台)
    #ifdef _WIN32
        ::std::wstring wPythonDir(pythonDir.begin(), pythonDir.end());
        searchPathsW.push_back(wPythonDir);
        searchPathsW.push_back(wPythonDir + L"/DLLs");
        searchPathsW.push_back(wPythonDir + L"/Lib");
    #else
        // Linux/macOS 可能需要不同的基础路径，例如 pythonDir/lib/pythonX.Y
        // 这里简化处理，仅添加 pythonDir
        ::std::wstring wPythonDir(pythonDir.begin(), pythonDir.end()); // 假设路径已经是UTF-8
        searchPathsW.push_back(wPythonDir);
        searchPathsW.push_back(wPythonDir + L"/lib/python" + std::to_wstring(PYTHON_VERSION_MAJOR) + L"." + std::to_wstring(PYTHON_VERSION_MINOR));
        searchPathsW.push_back(wPythonDir + L"/lib/python" + std::to_wstring(PYTHON_VERSION_MAJOR) + L"." + std::to_wstring(PYTHON_VERSION_MINOR) + L"/lib-dynload");
    #endif

    // 添加外部搜索路径
    for (const auto& searchPath : externalSearchPaths) {
        ::std::wstring wSearchPath(searchPath.begin(), searchPath.end());
        // 确保路径分隔符统一为 Python 喜欢的 '/' (即使在Windows上)
        #ifdef _WIN32
            ::std::replace(wSearchPath.begin(), wSearchPath.end(), L'\\', L'/');
        #endif
        searchPathsW.push_back(wSearchPath);
    }

    // 将 wstring 转换为 wchar_t* 列表供 PyConfig 使用
    ::std::vector<wchar_t*> searchPathsPtrs;
    for (const auto& wpath : searchPathsW) {
        searchPathsPtrs.push_back(const_cast<wchar_t*>(wpath.c_str()));
    }
    searchPathsPtrs.push_back(nullptr); // PyConfig 需要以 nullptr 结尾

    ::std::cerr << "[DEBUG]   Final module_search_paths: [" << ::std::endl;
    for (const auto& wpath : searchPathsW) {
        // 将宽字符串转换为多字节字符串以便打印
        std::wstring ws(wpath);
        std::string s(ws.begin(), ws.end());
        ::std::cerr << "    \"" << s << "\"," << ::std::endl;
    }
    ::std::cerr << "  ]" << ::std::endl;

    status = PyConfig_SetWideStringList(&config, &config.module_search_paths, searchPathsPtrs.size() - 1, searchPathsPtrs.data());
    PyStatusExitOnError(status);
    // --- 结束修改模块搜索路径设置 --- 

    // 不再需要设置 PYTHONPATH 环境变量
    // status = setPyConfigString(&config, &config.pythonpath_env, pyPath.str().c_str());
    // PyStatusExitOnError(status);

    ::std::cerr << "[DEBUG] Calling Py_InitializeFromConfig..." << ::std::endl;
    status = Py_InitializeFromConfig(&config);
    ::std::cerr << "[DEBUG] Py_InitializeFromConfig finished." << ::std::endl;

    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }
    
    ::std::cout << "Python interpreter initialized successfully" << ::std::endl;
}

::std::string PythonInterpreter::getPythonDir(const ::std::string& exeDir) {
#ifdef __APPLE__
    return exeDir + "/../Resources/python";
#elif defined(_WIN32)
    return exeDir + "/python";
#else
    return exeDir + "/python";
#endif
}

::std::string PythonInterpreter::getPythonExe(const ::std::string& pythonDir) {
#if defined(_WIN32)
    return pythonDir + "/python.exe";
#else
    ::std::stringstream pyVer;
    pyVer << "python" << PYTHON_VERSION_MAJOR << "." << PYTHON_VERSION_MINOR;
    return pythonDir + "/bin/" + pyVer.str();
#endif
}

::std::optional<::std::string> PythonInterpreter::executeCode(const ::std::string& code) {
    try {
        py::exec(code);
        // ::std::cout << "Python code executed successfully" << ::std::endl;
        return std::nullopt; // Success
    } catch(py::error_already_set& ex) {
        // ::std::cerr << "Error executing Python code: " << ex.what() << ::std::endl;
        return ::std::string(ex.what()); // Return error message
    }
}

::std::optional<::std::string> PythonInterpreter::executeFile(const ::std::string& scriptPath) {
    try {
        // 构建 Python 代码来执行脚本文件
        ::std::stringstream code;
        // 使用原始字符串处理路径，确保斜杠正确
        ::std::string correctedPath = scriptPath;
        #ifdef _WIN32
            ::std::replace(correctedPath.begin(), correctedPath.end(), '\\', '/');
        #endif
        code << "exec(open(r'" << correctedPath << "').read())"; 
        py::exec(code.str());
        // ::std::cout << "Python script executed successfully: " << scriptPath << ::std::endl;
        return std::nullopt; // Success
    } catch(py::error_already_set& ex) {
        // ::std::cerr << "Error executing Python script: " << ex.what() << ::std::endl;
        return ::std::string(ex.what()); // Return error message
    }
}

PythonInterpreter::~PythonInterpreter() {
    Py_Finalize();
    ::std::cout << "Python interpreter closed" << ::std::endl;
}
