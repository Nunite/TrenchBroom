#include "python_interpreter/pythoninterpreter.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <pybind11/embed.h>
#include <stdexcept>
#include <sys/types.h>

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
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

PythonInterpreter::PythonInterpreter(::std::string exePath, ::std::vector<::std::string> externalSearchPaths, std::ofstream& logStream, bool useSystemPython)
  : m_log(logStream)
{
    
    m_log << "[DEBUG] PythonInterpreter constructor started." << std::endl;
    m_log << "[DEBUG]   exePath: " << exePath << std::endl;
    m_log << "[DEBUG]   externalSearchPaths: [";
    for (const auto& p : externalSearchPaths) { m_log << "\"" << p << "\", "; }
    m_log << "]" << std::endl;
    m_log << "[DEBUG]   useSystemPython: " << useSystemPython << std::endl;

    PyStatus status;
    PyPreConfig preconfig;
    m_log << "[DEBUG] Initializing PyPreConfig..." << std::endl;
    if (useSystemPython) {
        PyPreConfig_InitPythonConfig(&preconfig);
    } else {
        PyPreConfig_InitPythonConfig(&preconfig);
        m_log << "[DEBUG] Using Python config instead of isolated config for better compatibility" << std::endl;
    }
    preconfig.utf8_mode = 1;  // 这个选项尽量保留，对于解决编码问题非常重要
    m_log << "[DEBUG] PyPreConfig initialized with utf8_mode=1" << std::endl;

    m_log << "[DEBUG] Calling Py_PreInitialize..." << std::endl;
    status = Py_PreInitialize(&preconfig);
    if (PyStatus_Exception(status)) {
        m_log << "[CRITICAL] Py_PreInitialize failed! Status: " << status.exitcode << std::endl;
        throw std::runtime_error("Python pre-initialization failed.");
    }
    m_log << "[DEBUG] Py_PreInitialize finished successfully." << std::endl;

    // Python now uses UTF-8
    PyConfig config;
    m_log << "[DEBUG] Initializing PyConfig..." << std::endl;
    if (useSystemPython) {
        PyConfig_InitPythonConfig(&config);
    } else {
        PyConfig_InitPythonConfig(&config);
        m_log << "[DEBUG] Using PyConfig_InitPythonConfig instead of isolated config" << std::endl;
    }
    m_log << "[DEBUG] PyConfig initialized." << std::endl;
    
    config.user_site_directory = useSystemPython;
    // 注意：某些Python版本可能不支持这些选项，如果不支持则省略
    m_log << "[DEBUG] PyConfig set: user_site_directory=" << useSystemPython << std::endl;
    
    // Get parent directory of executable path
    ::std::string exeDir;
    size_t lastSlash = exePath.find_last_of("/\\");
    if (lastSlash != ::std::string::npos) {
        exeDir = exePath.substr(0, lastSlash);
    } else {
        exeDir = ".";
    }
    m_log << "[DEBUG]   Calculated exeDir: " << exeDir << std::endl;
    
    // 检查exeDir是否指向Release目录，如果是，则需要上移app目录
    if (exeDir.find("Release") != ::std::string::npos || exeDir.find("Debug") != ::std::string::npos) {
        // 当前路径可能是app/Release，需要上移一级以匹配预期的路径
        size_t parentSlash = exeDir.find_last_of("/\\");
        if (parentSlash != ::std::string::npos) {
            ::std::string parentDir = exeDir.substr(0, parentSlash);
            m_log << "[DEBUG]   Adjusted exeDir: " << parentDir << " (moved up from Release/Debug)" << std::endl;
            exeDir = parentDir;
        }
    }
    
    // 检查各种python目录的可能位置
    ::std::vector<::std::string> possiblePythonPaths;
    
    // 1. 直接在exeDir下
    possiblePythonPaths.push_back(exeDir + "\\python");
    
    // 2. 在build目录下
    size_t buildPos = exeDir.find("build");
    if (buildPos != ::std::string::npos) {
        ::std::string buildDir = exeDir.substr(0, buildPos + 5);  // +5 包括 "build" 本身
        possiblePythonPaths.push_back(buildDir + "\\python");
    }
    
    // 3. 在项目根目录下
    size_t trenchbroomPos = exeDir.find("TrenchBroom");
    if (trenchbroomPos != ::std::string::npos) {
        ::std::string rootDir = exeDir.substr(0, trenchbroomPos + 11);  // +11 包括 "TrenchBroom" 本身
        possiblePythonPaths.push_back(rootDir + "\\python");
        
        // 添加常见子目录
        possiblePythonPaths.push_back(rootDir + "\\build\\python");
        possiblePythonPaths.push_back(rootDir + "\\app\\python");
        possiblePythonPaths.push_back(rootDir + "\\build\\app\\python");
    }
    
    m_log << "[DEBUG] Checking possible Python directories: " << std::endl;
    // 使用不同的变量名避免重定义
    struct _stat pathBuffer;
    for (const auto& path : possiblePythonPaths) {
        bool exists = (_stat(path.c_str(), &pathBuffer) == 0);
        m_log << "[DEBUG]   - " << path << ": " << (exists ? "exists" : "not found") << std::endl;
    }

    auto pythonDir = getPythonDir(exeDir);
    m_log << "[DEBUG]   Calculated pythonDir: " << pythonDir << std::endl;
    
    // 检查Python目录是否存在
    #ifdef _WIN32
    struct _stat dirBuffer;
    bool pythonDirExists = (_stat(pythonDir.c_str(), &dirBuffer) == 0);
    #else
    struct stat dirBuffer;
    bool pythonDirExists = (stat(pythonDir.c_str(), &dirBuffer) == 0);
    #endif
    m_log << "[DEBUG]   Python directory exists: " << (pythonDirExists ? "yes" : "no") << std::endl;
    
    if (!pythonDirExists) {
        m_log << "[CRITICAL] Python directory does not exist: " << pythonDir << std::endl;
        throw std::runtime_error("Python directory not found: " + pythonDir);
    }
    
    auto pythonExe = getPythonExe(pythonDir);
    m_log << "[DEBUG]   Calculated pythonExe: " << pythonExe << std::endl;
    
    // 检查Python可执行文件是否存在
    #ifdef _WIN32
    struct _stat exeBuffer;
    bool pythonExeExists = (_stat(pythonExe.c_str(), &exeBuffer) == 0);
    #else
    struct stat exeBuffer;
    bool pythonExeExists = (stat(pythonExe.c_str(), &exeBuffer) == 0);
    #endif
    m_log << "[DEBUG]   Python executable exists: " << (pythonExeExists ? "yes" : "no") << std::endl;
    
    if (!pythonExeExists) {
        m_log << "[CRITICAL] Python executable not found: " << pythonExe << std::endl;
        throw std::runtime_error("Python executable not found: " + pythonExe);
    }

    m_log << "[DEBUG] Setting config.program_name..." << std::endl;
    status = setPyConfigString(&config, &config.program_name, pythonExe.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[CRITICAL] Failed to set config.program_name! Status: " << status.exitcode << std::endl;
        throw std::runtime_error("Failed to set Python program name.");
    }
    m_log << "[DEBUG] config.program_name set." << std::endl;
    
    // Python searches for modules relative to home
    auto pythonHome = pythonDir;
    m_log << "[DEBUG]   Calculated pythonHome: " << pythonHome << std::endl;

    m_log << "[DEBUG] Setting config.home..." << std::endl;
    status = setPyConfigString(&config, &config.home, pythonHome.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[CRITICAL] Failed to set config.home! Status: " << status.exitcode << std::endl;
        throw std::runtime_error("Failed to set Python home directory.");
    }
    m_log << "[DEBUG] config.home set." << std::endl;
    
    // 显式设置模块搜索路径
    m_log << "[DEBUG] Setting module search paths..." << std::endl;
    config.module_search_paths_set = 1;
    
    // 添加标准库路径
#ifdef _WIN32
    // 在Windows上，组合完整路径
    std::wstring libPath = std::wstring(L"") + Py_DecodeLocale(pythonHome.c_str(), NULL) + L"\\Lib";
    std::wstring dllsPath = std::wstring(L"") + Py_DecodeLocale(pythonHome.c_str(), NULL) + L"\\DLLs";
    std::wstring sitePackagesPath = std::wstring(L"") + Py_DecodeLocale(pythonHome.c_str(), NULL) + L"\\Lib\\site-packages";
    
    // 检查目录是否存在
    std::string libPathStr = pythonHome + "\\Lib";
    std::string dllsPathStr = pythonHome + "\\DLLs";
    struct _stat libBuffer, dllsBuffer;
    bool libExists = (_stat(libPathStr.c_str(), &libBuffer) == 0);
    bool dllsExists = (_stat(dllsPathStr.c_str(), &dllsBuffer) == 0);
    
    m_log << "[DEBUG] Lib directory exists: " << (libExists ? "yes" : "no") << std::endl;
    m_log << "[DEBUG] DLLs directory exists: " << (dllsExists ? "yes" : "no") << std::endl;
    
    // 如果目录不存在，创建警告但继续执行
    if (!libExists) {
        m_log << "[WARNING] Python Lib directory does not exist: " << libPathStr << std::endl;
    }
    
    if (!dllsExists) {
        m_log << "[WARNING] Python DLLs directory does not exist: " << dllsPathStr << std::endl;
    }
    
    status = PyWideStringList_Append(&config.module_search_paths, libPath.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[WARNING] Failed to add Lib to module_search_paths" << std::endl;
    } else {
        m_log << "[DEBUG] Added to module_search_paths: " << pythonHome << "\\Lib" << std::endl;
    }
    
    status = PyWideStringList_Append(&config.module_search_paths, dllsPath.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[WARNING] Failed to add DLLs to module_search_paths" << std::endl;
    } else {
        m_log << "[DEBUG] Added to module_search_paths: " << pythonHome << "\\DLLs" << std::endl;
    }
    
    status = PyWideStringList_Append(&config.module_search_paths, sitePackagesPath.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[WARNING] Failed to add site-packages to module_search_paths" << std::endl;
    } else {
        m_log << "[DEBUG] Added to module_search_paths: " << pythonHome << "\\Lib\\site-packages" << std::endl;
    }
#else
    // 在Unix/Mac上，组合完整路径
    std::string version = std::to_string(PYTHON_VERSION_MAJOR) + "." + std::to_string(PYTHON_VERSION_MINOR);
    std::wstring libPath = std::wstring(L"") + Py_DecodeLocale(pythonHome.c_str(), NULL) + L"/lib/python" + Py_DecodeLocale(version.c_str(), NULL);
    
    status = PyWideStringList_Append(&config.module_search_paths, libPath.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[WARNING] Failed to add lib/python to module_search_paths" << std::endl;
    } else {
        m_log << "[DEBUG] Added to module_search_paths: " << pythonHome << "/lib/python" << version << std::endl;
    }
#endif
    
    // 添加外部搜索路径
    m_log << "[DEBUG] Setting config.pythonpath_env for external paths..." << std::endl;
    std::stringstream pyPathEnv;
#ifdef _WIN32
    const auto delim = ";";
#else
    const auto delim = ":";
#endif
    for (const auto& path : externalSearchPaths) {
        pyPathEnv << path << delim; 
    }
    std::string pyPathEnvStr = pyPathEnv.str();
    if (!pyPathEnvStr.empty()) {
        pyPathEnvStr.pop_back();
    }
    m_log << "[DEBUG]   Constructed pythonpath_env: " << pyPathEnvStr << std::endl;
    status = setPyConfigString(&config, &config.pythonpath_env, pyPathEnvStr.c_str());
    if (PyStatus_Exception(status)) {
        m_log << "[CRITICAL] Failed to set config.pythonpath_env! Status: " << status.exitcode << std::endl;
        throw std::runtime_error("Failed to set Python environment path.");
    }
    m_log << "[DEBUG] config.pythonpath_env set." << std::endl;
    
    // 设置命令行参数
    m_log << "[DEBUG] Setting program arguments..." << std::endl;
    wchar_t* program = Py_DecodeLocale(pythonExe.c_str(), NULL);
    if (program == NULL) {
        m_log << "[WARNING] Failed to decode program name" << std::endl;
    } else {
        status = PyConfig_SetArgv(&config, 1, &program);
        if (PyStatus_Exception(status)) {
            m_log << "[WARNING] Failed to set argv" << std::endl;
        }
        PyMem_RawFree(program);
    }
    
    m_log << "[DEBUG] Calling Py_InitializeFromConfig..." << std::endl;
    status = Py_InitializeFromConfig(&config);
    m_log << "[DEBUG] Py_InitializeFromConfig finished." << std::endl;

    // --- 改进错误处理 --- 
    if (PyStatus_Exception(status)) {
        m_log << "[CRITICAL] Python initialization failed! Function: " 
              << (status.func ? status.func : "<unknown>") 
              << ", Message: " 
              << (status.err_msg ? status.err_msg : "<no message>") 
              << ", Status code: " << status.exitcode << std::endl;
        PyConfig_Clear(&config);
        throw std::runtime_error(std::string("Python initialization failed: ") + 
                               (status.err_msg ? status.err_msg : "<no message>") +
                               " in " + 
                               (status.func ? status.func : "<unknown>"));
    }
    // --- 结束改进错误处理 ---

    // --- 新增检查 --- 
    bool isInitialized = Py_IsInitialized();
    m_log << "[DEBUG] Py_IsInitialized() returned: " << isInitialized << std::endl;
    m_log.flush(); // 强制刷新日志

    if (PyErr_Occurred()) {
        m_log << "[ERROR] Python error occurred during initialization!" << std::endl;
        // 尝试获取并记录 Python 错误信息 (这可能需要更复杂的处理来捕获输出)
        // PyErr_Print(); // 这个会打印到 stderr，不一定能看到
        PyObject *ptype, *pvalue, *ptraceback;
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        if (ptype != NULL) {
             PyObject* pystr = PyObject_Str(pvalue ? pvalue : ptype);
             if (pystr != NULL) {
                const char* error_str = PyUnicode_AsUTF8(pystr);
                if (error_str != NULL) {
                     m_log << "[ERROR] Python Error Details: " << error_str << std::endl;
                } else {
                    m_log << "[ERROR] Could not convert Python error object to UTF8 string." << std::endl;
                }
                Py_DECREF(pystr);
             } else {
                 m_log << "[ERROR] Could not get string representation of Python error object." << std::endl;
             }
        }
        PyErr_Clear(); // 清除错误状态，避免影响后续判断
        Py_XDECREF(ptype);
        Py_XDECREF(pvalue);
        Py_XDECREF(ptraceback);
        m_log.flush(); // 强制刷新日志
    }
    // --- 结束新增检查 ---

    PyConfig_Clear(&config);
    m_log << "[DEBUG] PyConfig cleared." << std::endl;

    // 如果没有 C API 状态异常，并且 Python 认为自己已初始化，我们才认为成功
    if (!isInitialized) {
        m_log << "[CRITICAL] Py_IsInitialized() returned false after Py_InitializeFromConfig, even though no C API exception was reported." << std::endl;
        throw std::runtime_error("Python initialization failed (Py_IsInitialized check).");
    }

    m_log << "[DEBUG] Python interpreter initialized successfully." << std::endl;
}

::std::string PythonInterpreter::getPythonDir(const ::std::string& exeDir) {
    ::std::string pythonDir;

#ifdef __APPLE__
    pythonDir = exeDir + "/../Resources/python";
#elif defined(_WIN32)
    // 首先尝试在程序运行目录（通常是Release/Debug）查找python目录
    ::std::string originalExePath = exeDir;
    size_t lastSlash = originalExePath.find_last_of("/\\");
    ::std::string exeDirName;
    if (lastSlash != ::std::string::npos) {
        exeDirName = originalExePath.substr(lastSlash + 1);
    }
    
    bool isReleaseDir = (exeDirName == "Release" || exeDirName == "Debug");
    
    // 如果当前目录是Release/Debug，直接在这里查找python
    if (isReleaseDir) {
        ::std::string releasePythonPath = originalExePath + "\\python";
        struct _stat releaseDirBuffer;
        if (_stat(releasePythonPath.c_str(), &releaseDirBuffer) == 0) {
            return releasePythonPath; // 直接返回Release/Debug下的python路径
        }
    }
    
    // 1. 尝试exeDir/Release或exeDir/Debug目录下查找
    ::std::string releaseDir = exeDir + "\\Release";
    ::std::string debugDir = exeDir + "\\Debug";
    ::std::string releasePythonPath = releaseDir + "\\python";
    ::std::string debugPythonPath = debugDir + "\\python";
    
    struct _stat releaseDirBuffer, debugDirBuffer;
    if (_stat(releasePythonPath.c_str(), &releaseDirBuffer) == 0) {
        return releasePythonPath;
    }
    if (_stat(debugPythonPath.c_str(), &debugDirBuffer) == 0) {
        return debugPythonPath;
    }
    
    // 2. 尝试直接在exeDir查找python目录
    ::std::string directPath = exeDir + "\\python";
    struct _stat dirPathBuffer;
    if (_stat(directPath.c_str(), &dirPathBuffer) == 0) {
        pythonDir = directPath;
    } else {
        // 3. 如果仍未找到，尝试作为父目录查找
        size_t lastSlash = exeDir.find_last_of("\\");
        if (lastSlash != ::std::string::npos) {
            ::std::string parentDir = exeDir.substr(0, lastSlash);
            ::std::string siblingPath = parentDir + "\\python";
            struct _stat siblingBuffer;
            if (_stat(siblingPath.c_str(), &siblingBuffer) == 0) {
                pythonDir = siblingPath;
            } else {
                // 如果仍然没找到，默认使用直接路径
                pythonDir = directPath;
            }
        } else {
            pythonDir = directPath;
        }
    }
#else
    pythonDir = exeDir + "/python";
#endif

    return pythonDir;
}

::std::string PythonInterpreter::getPythonExe(const ::std::string& pythonDir) {
#if defined(_WIN32)
    ::std::string exePath = pythonDir + "\\python.exe";
    
    // 检查python.exe是否存在
    struct _stat exePathBuffer;
    bool exeExists = (_stat(exePath.c_str(), &exePathBuffer) == 0);
    
    if (!exeExists) {
        // 尝试其他可能的位置
        ::std::string altPath1 = pythonDir + "\\bin\\python.exe";
        ::std::string altPath2 = pythonDir + "\\..\\python\\python.exe";
        
        struct _stat altPath1Buffer;
        if (_stat(altPath1.c_str(), &altPath1Buffer) == 0) {
            return altPath1;
        } 
        
        struct _stat altPath2Buffer;
        if (_stat(altPath2.c_str(), &altPath2Buffer) == 0) {
            return altPath2;
        }
    }
    
    return exePath;
#else
    ::std::stringstream pyVer;
    pyVer << "python" << PYTHON_VERSION_MAJOR << "." << PYTHON_VERSION_MINOR;
    ::std::string exePath = pythonDir + "/bin/" + pyVer.str();
    
    // 检查Python可执行文件是否存在
    struct stat unixExeBuffer;
    bool exeExists = (stat(exePath.c_str(), &unixExeBuffer) == 0);
    
    if (!exeExists) {
        // 尝试不带版本号的python
        ::std::string altPath = pythonDir + "/bin/python";
        struct stat altPathBuffer;
        if (stat(altPath.c_str(), &altPathBuffer) == 0) {
            return altPath;
        }
    }
    
    return exePath;
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
}
