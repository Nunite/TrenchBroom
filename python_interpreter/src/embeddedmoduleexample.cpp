#include "python_interpreter/embeddedmoduleexample.h"

#include <pybind11/embed.h>
#include <vector>

namespace py = pybind11;

// This defines a Python module named "fast_calc" that can be imported in Python code
PYBIND11_EMBEDDED_MODULE(fast_calc, m) {
    // `m` is a `py::module_` used to bind functions and classes
    
    // Add module docstring
    m.doc() = "TrenchBroom C++ accelerated calculation module";
    
    // Add basic functions
    m.def("add", [](int i, int j) {
        return i + j;
    }, "Add two numbers");
    
    m.def("subtract", [](int i, int j) {
        return i - j;
    }, "Subtract two numbers");
    
    m.def("multiply", [](int i, int j) {
        return i * j;
    }, "Multiply two numbers");
    
    m.def("divide", [](double i, double j) {
        if (j == 0) {
            throw py::value_error("Division by zero");
        }
        return i / j;
    }, "Divide two numbers");
    
    // Add a more complex example - vector dot product
    m.def("dot_product", [](const ::std::vector<double>& v1, const ::std::vector<double>& v2) {
        if (v1.size() != v2.size()) {
            throw py::value_error("Vectors must have same dimensions");
        }
        double result = 0.0;
        for (size_t i = 0; i < v1.size(); ++i) {
            result += v1[i] * v2[i];
        }
        return result;
    }, "Compute dot product of two vectors", py::arg("v1"), py::arg("v2"));
}
