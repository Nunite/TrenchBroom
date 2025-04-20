project(python_interpreter)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

set(PYTHON_VERSION ${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR})
set(PYTHON_VER_NAME python${PYTHON_VERSION})
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin") 
    # Mac特定配置
    set(PY_RUNTIME_DIR "$<TARGET_BUNDLE_CONTENT_DIR:TrenchBroom>/Resources/python")
    set(PY_INSTALL_DIR "${RESOURCE_INSTALL_PREFIX}/python")
elseif("${CMAKE_HOST_SYSTEM_NAME}" MATCHES "Windows")
    # Windows特定配置
    set(PY_RUNTIME_DIR "$<TARGET_FILE_DIR:TrenchBroom>/python")
    set(PY_INSTALL_DIR "${RUNTIME_INSTALL_DIR}/python")
else()
    # Linux特定配置
    set(PY_RUNTIME_DIR "$<TARGET_FILE_DIR:TrenchBroom>/python")
    set(PY_INSTALL_DIR "${RUNTIME_INSTALL_DIR}/python")
endif()

message("Python_FOUND: ${Python3_FOUND}")
message("Python_EXECUTABLE: ${Python3_EXECUTABLE}")
message("Python3_LIBRARIES: ${Python3_LIBRARIES}")
message("Python3_RUNTIME_LIBRARY_DIRS: ${Python3_RUNTIME_LIBRARY_DIRS}")
message("Python3_RUNTIME_LIBRARY_RELEASE: ${Python3_RUNTIME_LIBRARY_RELEASE}")
message("Python3_RUNTIME_LIBRARY_DEBUG: ${Python3_RUNTIME_LIBRARY_DEBUG}")
message("Python3_STDLIB: ${Python3_STDLIB}")
message("Python3_STDARCH: ${Python3_STDARCH}")
message("Python version: ${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")
get_target_property(py_imported_loc Python3::Python IMPORTED_LOCATION_RELEASE)
message("Python IMPORTED_LOCATION_RELEASE: ${py_imported_loc}")
get_target_property(py_imported_lib Python3::Python IMPORTED_IMPLIB_RELEASE)
message("Python IMPORTED_IMPLIB_RELEASE: ${py_imported_lib}")

# 添加源文件
set(SOURCE_FILES
    src/pythoninterpreter.cpp
)
set(HEADER_FILES
    include/python_interpreter/pythoninterpreter.h
    include/python_interpreter/pythoninterpreterdefines.h
)

set(PY_SCRIPTS 
    PyScripts/calc.py
)

# 创建库
add_library(${PROJECT_NAME} 
    ${SOURCE_FILES} 
    ${HEADER_FILES} 
)
target_link_libraries(${PROJECT_NAME} PUBLIC 
    pybind11::embed 
)
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# 设置定义以允许查找Python
target_compile_definitions(${PROJECT_NAME} PUBLIC 
    PYTHON_VERSION_MAJOR=${Python3_VERSION_MAJOR}
    PYTHON_VERSION_MINOR=${Python3_VERSION_MINOR}
    PYTHON_EXECUTABLE_NAME=${Python3_EXECUTABLE}
)
# 定义在Windows上构建时使用declspec(dllexport)的符号
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<BOOL:${BUILD_SHARED_LIBS}>:python_interpreter_EXPORTS>)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<BOOL:${BUILD_SHARED_LIBS}>:BUILD_SHARED_LIBS>)

# 设置编译选项
if(MSVC)
    # 强制使用UTF-8编码
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /utf-8 
        /wd4819  # 禁用关于代码页的警告
        /std:c++17  # 显式指定C++17标准
    )
    
    # 不要同时使用/source-charset:utf-8，因为它与/utf-8冲突
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# 注：以下pip相关命令被注释掉，以避免在构建时需要运行Python解释器
# 您可以在应用程序首次运行时安装所需的Python包
# # 确保pip存在
# execute_process(COMMAND ${Python3_EXECUTABLE} -m ensurepip)
# # 通过pip安装所需模块
# # 这里，您可以使用requirements.txt文件...
# execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install numpy --isolated)

# 将自定义Python文件复制到生成的二进制目录。
# Python解释器必须设置为在此目录中搜索文件
foreach(pyFile IN ITEMS ${PY_SCRIPTS})
    add_custom_command(
                TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different 
                "${CMAKE_CURRENT_SOURCE_DIR}/${pyFile}"
                "${PY_RUNTIME_DIR}/${pyFile}")
endforeach()

# Python文件夹结构在Windows/Unix之间有所不同
if("${CMAKE_HOST_SYSTEM_NAME}" MATCHES "Windows")
    cmake_path(GET Python3_EXECUTABLE PARENT_PATH PYTHON_DIR)
    # 将Python复制到运行时目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${PYTHON_DIR}/ ${PY_RUNTIME_DIR})

    ##### 设置打包安装程序的文件 ####

    install(
        DIRECTORY ${PYTHON_DIR}/
        DESTINATION ${PY_INSTALL_DIR}
        # 排除不需要的文件。您可能需要根据您的项目进行调整
        PATTERN "__pycache__" EXCLUDE               # * 任何缓存 *
        PATTERN "config-${PYTHON_VERSION}m/*.a" EXCLUDE  # 静态库
        PATTERN "lib2to3" EXCLUDE                   
        PATTERN "tkinter" EXCLUDE                   
        PATTERN "lib-dynload/_tkinter.*" EXCLUDE    
        PATTERN "idlelib" EXCLUDE                   
        PATTERN "test" EXCLUDE                      
        PATTERN "turtledemo" EXCLUDE               
        PATTERN "turtle.py" EXCLUDE                 
        PATTERN "wininst*.exe" EXCLUDE              # 来自 distutils，避免恶意软件误报
      )
    # 安装Python dll 
    install(IMPORTED_RUNTIME_ARTIFACTS Python3::Module
        RUNTIME        # DLLs, Exes, 
        DESTINATION ${RUNTIME_INSTALL_DIR}
        BUNDLE         # 标记为BUNDLE的目标
        DESTINATION ${BUNDLE_INSTALL_DIR}
        LIBRARY        # 共享库 - DLLs   
        DESTINATION ${RUNTIME_INSTALL_DIR}
        FRAMEWORK      # 标记为FRAMEWORK的目标
        DESTINATION ${FRAMEWORK_INSTALL_DIR}
    )
    # Python还依赖于zlib1，但似乎没有很好的方法检索它。
    # 搜索python dll依赖项，排除系统库，并将它们添加到安装程序
    # https://stackoverflow.com/questions/62884439/how-to-use-cmake-file-get-runtime-dependencies-in-an-install-statement
    install(CODE "set(PYTHON_DIR \"${PYTHON_DIR}\")")
    install(CODE "set(RUNTIME_INSTALL_DIR \"${RUNTIME_INSTALL_DIR}\")")
    install(CODE [[
      file(GET_RUNTIME_DEPENDENCIES
        LIBRARIES $<TARGET_FILE:Python3::Python>
        RESOLVED_DEPENDENCIES_VAR r_deps
        UNRESOLVED_DEPENDENCIES_VAR u_deps
        DIRECTORIES ${PYTHON_DIR}
        POST_EXCLUDE_REGEXES ".*WINDOWS[\\/]system32.*"
      )
      foreach(_file ${r_deps})
        file(INSTALL
          DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
          TYPE SHARED_LIBRARY
          FOLLOW_SYMLINK_CHAIN
          FILES "${_file}"
        )
      endforeach()
      list(LENGTH u_deps u_length)
      if("${u_length}" GREATER 0)
        message(WARNING "Unresolved dependencies detected: ${u_deps}")
      endif()
    ]])

else()
    # 复制Python模块
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_directory
                       ${Python3_STDLIB} ${PY_RUNTIME_DIR}/lib/${PYTHON_VER_NAME})
    # 复制可执行文件（如果只嵌入，则不必要）
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different
                       ${Python3_EXECUTABLE} "${PY_RUNTIME_DIR}/bin/${PYTHON_VER_NAME}")


    ##### 设置打包安装程序的文件 ####

    # 安装Python3_EXECUTABLE 
    install(IMPORTED_RUNTIME_ARTIFACTS Python3::Interpreter
        RUNTIME        # DLLs, Exes, 
        DESTINATION ${RUNTIME_INSTALL_DIR}
        BUNDLE         # 标记为BUNDLE的目标
        DESTINATION ${BUNDLE_INSTALL_DIR}
        LIBRARY        # 共享库 - DLLs   
        DESTINATION ${RUNTIME_INSTALL_DIR}
        FRAMEWORK      # 标记为FRAMEWORK的目标
        DESTINATION ${FRAMEWORK_INSTALL_DIR}
    )

    install(
        DIRECTORY ${Python3_STDLIB}
        DESTINATION ${PY_INSTALL_DIR}/lib
        # 排除不需要的文件。您可能需要根据您的项目进行调整
        PATTERN "__pycache__" EXCLUDE               # * 任何缓存 *
        PATTERN "config-${PYTHON_VERSION}m/*.a" EXCLUDE  # 静态库
        PATTERN "lib2to3" EXCLUDE                   
        PATTERN "tkinter" EXCLUDE                   
        PATTERN "lib-dynload/_tkinter.*" EXCLUDE    
        PATTERN "idlelib" EXCLUDE                   
        PATTERN "test" EXCLUDE                      
        PATTERN "turtledemo" EXCLUDE               
        PATTERN "turtle.py" EXCLUDE                 
        PATTERN "wininst*.exe" EXCLUDE              # 来自 distutils，避免恶意软件误报
      )
endif()

install(FILES ${PY_SCRIPTS} DESTINATION ${PY_INSTALL_DIR}/PyScripts)

# 需要distutils/pip
install(
    FILES ${Python3_INCLUDE_DIRS}/pyconfig.h
    DESTINATION ${PY_INSTALL_DIR}/include/${PYTHON_VER_NAME}
)

install(TARGETS ${PROJECT_NAME}
            RUNTIME        # DLLs, Exes, 
                DESTINATION ${RUNTIME_INSTALL_DIR}
            BUNDLE         # 标记为BUNDLE的目标
                DESTINATION ${BUNDLE_INSTALL_DIR}
            ARCHIVE        # 静态库, .libs 
                DESTINATION ${ARCHIVE_INSTALL_DIR}
            LIBRARY        # 共享库 - DLLs   
                DESTINATION ${LIBRARY_INSTALL_DIR}
            FRAMEWORK      # 标记为FRAMEWORK的目标
                DESTINATION ${FRAMEWORK_INSTALL_DIR}
            PUBLIC_HEADER  # 公共头文件
                DESTINATION ${INCLUDE_INSTALL_DIR}
            RESOURCE       # 资源文件
                DESTINATION ${RESOURCE_INSTALL_PREFIX}
        )

