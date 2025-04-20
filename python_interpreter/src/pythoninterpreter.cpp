#include "python_interpreter/pythoninterpreter.h"

#include <iostream>
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

    auto pythonDir = getPythonDir(exeDir);
    
    auto pythonExe = getPythonExe(pythonDir);
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
    status = setPyConfigString(&config, &config.home, pythonHome.c_str());
    PyStatusExitOnError(status);
    
    // Add external Python module search paths
    ::std::stringstream pyPath;
    // Use different separators based on OS
    // https://docs.python.org/3/c-api/init_config.html#c.PyConfig.pythonpath_env
#ifdef _WIN32
    const auto delim = ";";
#else
    const auto delim = ":";
#endif
    for (const auto& path : externalSearchPaths) {
        pyPath << path << delim;
    }
    status = setPyConfigString(&config, &config.pythonpath_env, pyPath.str().c_str());

    PyStatusExitOnError(status);

    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }
    
    // Python API can now be called
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

void PythonInterpreter::executeCode(const ::std::string& code) {
    try {
        py::exec(code);
        ::std::cout << "Python code executed successfully" << ::std::endl;
    } catch(py::error_already_set& ex) {
        ::std::cerr << "Error executing Python code: " << ex.what() << ::std::endl;
    }
}

void PythonInterpreter::executeFile(const ::std::string& scriptPath) {
    try {
        // Build Python code to execute script file
        ::std::stringstream code;
        code << "exec(open('" << scriptPath << "').read())";
        py::exec(code.str());
        ::std::cout << "Python script executed successfully: " << scriptPath << ::std::endl;
    } catch(py::error_already_set& ex) {
        ::std::cerr << "Error executing Python script: " << ex.what() << ::std::endl;
    }
}

PythonInterpreter::~PythonInterpreter() {
    Py_Finalize();
    ::std::cout << "Python interpreter closed" << ::std::endl;
}
