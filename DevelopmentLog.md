# 开发日志

## 任务：合并旧版本功能到当前项目
- 不要用中文注释
## 2026-01-08: Interface Localization Implementation

### Initial Setup
1.  **Resource Migration**:
    -   Copied `trenchbroom_zh_CN.qm` and `trenchbroom_zh_CN.ts` to `app/resources/translations/`.
    -   Updated `app/resources/qrc/resources.qrc` to include the `.qm` file under the prefix `/translations`.

### Code Adaptation
1.  **Preferences**:
    -   Modified `common/src/Preferences.h`: Added `Language` preference declaration and helper functions `languageEnglish()` and `languageChinese()`.
    -   Modified `common/src/Preferences.cpp`: Implemented `Language` preference with default value "Chinese".

2.  **Application Logic**:
    -   Modified `common/src/TrenchBroomApp.h`: Added `QTranslator* m_translator` member to `TrenchBroomApp` class.
    -   Modified `common/src/TrenchBroomApp.cpp`:
        -   In the constructor, initialized `m_translator`.
        -   Added logic to check `Preferences::Language`.
        -   If Chinese is selected, attempted to load `trenchbroom_zh_CN.qm` from resources (`:/translations`), app directory, or system resource directories.
        -   Installed the translator using `installTranslator`.
        -   Set `QLocale` default to Chinese/China.

### Bug Fixes
1.  **Build Errors**:
    -   Encountered `C2027: use of undefined type 'QApplication'` in `Preferences.cpp`.
        -   **Fix**: Added `#include <QApplication>` to `common/src/Preferences.cpp`.
    -   Encountered `C2027: use of undefined type 'QTranslator'` in `TrenchBroomApp.cpp`.
        -   **Fix**: Added `#include <QTranslator>` to `common/src/TrenchBroomApp.cpp`.
    -   **Garbled Text in Preferences**: The "Description" column in the Keyboard shortcuts tab displayed garbled text for localized entries.
        -   **Cause**: Incorrect conversion from `std::filesystem::path` to `QString` using `generic_string()` (ANSI on Windows) instead of `generic_wstring()` (UTF-16). `QString::fromStdString` expected UTF-8.
        -   **Fix**: Modified `common/src/ui/KeyboardShortcutModel.cpp` to use `io::pathAsGenericQString`, which handles platform-specific path string conversion correctly.

    ### New Features
    1.  **Language Selection in Preferences**:
        -   Added a new "Language" tab to the Preferences dialog.
        -   Created `common/src/ui/LanguagePreferencePane.h/cpp` based on the implementation in the `merge` directory.
        -   Updated `common/src/ui/PreferenceDialog.cpp` to include the new pane and icon.
        -   Created `app/resources/graphics/images/LanguagePreferences.svg` (copied from `GeneralPreferences.svg`).
        -   Updated `common/CMakeLists.txt` to include the new source file.

### 2026-01-08: Box Selection Feature Integration
1.  **Box Selection Logic**:
    -   Modified `common/src/ui/BoxSelectionTool.h/cpp` to generalize `BoxSelectionDragDelegate` to work with `Tool&` and `mdl::Map&` instead of specific tool classes.
    -   Fixed include errors in `BoxSelectionTool.cpp` (`ui/Grid.h` -> `mdl/Grid.h`, `ui/Transaction.h` -> `mdl/Transaction.h`).
    -   Replaced `MapDocument` usage with `mdl::Map` and `mdl::Map_Selection` functions in `BoxSelectionTool.cpp` to decouple from `MapDocument`.

2.  **Tool Integration**:
    -   Modified `common/src/ui/SelectionTool.cpp`:
        -   Added check for Alt+LeftDrag in `acceptMouseDrag`.
        -   Implemented instantiation of `HandleDragTracker<BoxSelectionDragDelegate>` when Alt is pressed.
        -   Calculated start point for box selection using ray-plane intersection (plane passing through origin aligned with camera).

### Bug Fixes (Round 2)
1.  **Compilation Errors**:
    -   **BoxSelectionTool.cpp**:
        -   Fixed `Color` initialization error by using `Color(RgbaF(...))` instead of direct `Color(...)`.
    -   **SelectionTool.cpp**:
        -   Fixed `use of undefined type 'tb::render::Camera'` by adding `#include "render/Camera.h"`.
        -   Fixed `ray()` not member of `InputState` by changing to `pickRay()`.
        -   Fixed malformed include directive.
        -   Fixed `error C2737: 'point': const object must be initialized` which was caused by the `ray()` method not being found, leading to type deduction failure. Corrected to `pickRay()`.

### Next Steps
-   Verify the build succeeds with the applied fixes.
-   Launch the application to verify Chinese interface is loaded.
-   Verify Box Selection works (Alt+LeftDrag).
