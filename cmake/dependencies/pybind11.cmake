find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

message(STATUS "Fetching pybind11...")
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11
  GIT_TAG        v3.0.1
  SYSTEM
)

set(PYBIND11_FINDPYTHON ON)
set(PYBIND11_TEST OFF)
set(PYBIND11_INSTALL OFF)
FetchContent_MakeAvailable(pybind11)

suppress_dependency_warnings(pybind11_headers)
