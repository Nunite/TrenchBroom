#pragma once

#include "python_interpreter/pythoninterpreterdefines.h"

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <optional>

class PYTHON_INTERPRETER_API PythonInterpreter {
public:
    /*
     * Initializes Python
     * @param exePath Path to the main executable
     * @param externalSearchPaths Additional Python module search paths
     * @param useSystemPython Setup system installed Python if true, isolated mode otherwise.
     *
     * @post Python API can be called
     */
    PythonInterpreter(::std::string exePath, ::std::vector<::std::string> externalSearchPaths, bool useSystemPython = false);
    ~PythonInterpreter();
    
    /*
     * Return root folder containing Python:
     * root/bin
     * root/lib
     * @param exePath Path to the main executable
     */
    static ::std::string getPythonDir(const ::std::string& exeDir);

    /*
     * Return path to Python executable.
     * @param pythonDir Path to folder containing Python
     */
    static ::std::string getPythonExe(const ::std::string& pythonDir);
    
    /*
     * Execute Python code from string
     * @param code Python code to execute
     * @return std::nullopt on success, error message string on failure
     */
    ::std::optional<::std::string> executeCode(const ::std::string& code);
    
    /*
     * Execute Python code from file
     * @param scriptPath Path to Python script file
     * @return std::nullopt on success, error message string on failure
     */
    ::std::optional<::std::string> executeFile(const ::std::string& scriptPath);
};
