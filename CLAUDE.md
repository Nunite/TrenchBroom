# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TrenchBroom is a modern cross-platform level editor for Quake-engine based games, written in C++ with Qt6. It supports editing in 3D and up to three 2D views, with features like brush editing, entity editing, and support for multiple game engines.

## Build Instructions

### Prerequisites
- CMake 3.25 or higher
- Qt 6.7 or higher
- Pandoc
- Compiler with C++20 support (Clang, GCC, or MSVC)

### Dependencies
- Uses vcpkg for dependency management (assimp, freeimage, freetype, Catch2, fmt, GLEW, miniz, tinyxml2)
- Qt6 components: Core, Widgets, OpenGL, OpenGLWidgets, Network, Svg, Test

### Building on Windows
```bash
mkdir build
cd build
cmake .. -G"Visual Studio 17 2022" -T v143 -A x64 -DCMAKE_PREFIX_PATH="<QT_INSTALL_DIR>\msvc2022_64"
cmake --build . --target TrenchBroom
```

### Building on Linux
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="cmake/packages;<QT_INSTALL_DIR>/gcc_64"
cmake --build . --target TrenchBroom
```

### Building on macOS
```bash
mkdir build
cd build
cmake .. -GNinja -DCMAKE_PREFIX_PATH="<QT_INSTALL_DIR>/macos"
cmake --build . --target TrenchBroom
```

## Testing

Run tests with:
```bash
# Unit tests
./common/test/common-test

# Regression tests
./common/test/common-regression-test
```

Tests are organized by module:
- `common/test/src/el/` - Expression language tests
- `common/test/src/io/` - Input/output tests
- `common/test/src/mdl/` - Model/brush/entity tests
- `common/test/src/render/` - Rendering tests
- `common/test/src/ui/` - UI tests

## Code Architecture

### Major Components
1. **Model (mdl)** - Core data structures for brushes, entities, maps, game definitions
2. **Input/Output (io)** - File format parsers/loaders for .map, .bsp, .mdl, .md3, etc.
3. **Rendering (render)** - OpenGL-based rendering system
4. **User Interface (ui)** - Qt-based UI components and tools
5. **Expression Language (el)** - Custom expression language for entity properties

### Key Directories
- `common/src/` - Core shared functionality
- `app/src/` - Main application entry point
- `common/test/` - Unit and regression tests
- `lib/` - Third-party libraries (kdl, stackwalker, upd, vm)

### Coding Standards
- C++17/20 with "almost always auto" style
- Camel case naming: classes start with uppercase, variables/functions with lowercase
- Private members prefixed with `m_`
- Use `std::optional` instead of magic values
- Prefer value semantics and RAII
- No exceptions - use `Result` and `Error` types instead
- Use clang-format for code formatting
- Use forward declarations to minimize includes

## Common Development Tasks

### Adding a New Test
1. Create test file in appropriate subdirectory under `common/test/src/`
2. Add to CMakeLists.txt in `common/test/`
3. Follow Catch2 test framework patterns

### Adding New Functionality
1. Follow existing code patterns in the relevant module
2. Add unit tests in corresponding test directory
3. Use forward declarations in headers when possible
4. Implement in .cpp files with anonymous namespaces for helper functions