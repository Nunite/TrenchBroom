# 开发日志

## 任务：合并旧版本功能到当前项目

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

### Next Steps
-   Verify the build succeeds with the applied fixes.
-   Launch the application to verify Chinese interface is loaded.
-   Continue with merging other features as requested.
