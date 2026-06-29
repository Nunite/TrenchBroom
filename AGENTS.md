# TrenchBroom Coding Agent Guidance

## Project structure
- The project contains several applications under /app. The main application target is TrenchBroom.
- Most code lives in libraries under /lib.
- Each application target and library target has its own CMakeLists.txt file.
- Shared CMake utilities are in /cmake.
- Each library usually has a <Name>LibTest target for its tests, for example TbMdlLibTest.
- Some libraries also have a <Name>TestUtilsLib target for shared test helpers, and some of those have a matching <Name>TestUtilsLibTest target.

## Build and test
- TrenchBroom uses CMake as its build system.
- In Visual Studio Code, prefer CMake Tools for builds.
- Build the narrowest relevant target instead of building the whole workspace when possible.
- For library changes, prefer the corresponding <Name>LibTest target to validate the change.
- Always build the relevant test target before running tests.
- Tests use Catch2.
- If VS Code test discovery is unavailable, run the built test executable directly from the build tree, for example build/lib/TbMdlLib/test/TbMdlLibTest.
- Use --list-tests to discover available tests and Catch2 filters to run a focused subset.
- Use Build.md for platform-specific setup and dependency details.

### Windows Release build used by this branch
- The active local Release build tree is usually `build-release-codex`.
- On this machine, prefer the checked-in wrapper script instead of hand-written CMake commands:
  ```powershell
  scripts\build_release_codex.cmd
  ```
- This is important because the local Windows SDK tools are installed under `D:\Windows Kits\10\...`, not only the default Visual Studio-discovered path. The wrapper script pins `VsDevCmd.bat`, `rc.exe`, and `mt.exe` so Release rebuilds stay reproducible.
- The wrapper script deletes and recreates the target build directory before configuring. Do not point it at an arbitrary directory unless you intentionally want a full clean rebuild there.
- If you need the same clean Release flow for another directory, pass it explicitly:
  ```powershell
  scripts\build_release_codex.cmd build-release-other
  ```
- Once `build-release-codex` exists, prefer the filtered wrapper for routine incremental Release builds and focused tests. It keeps the full log under `build-release-codex\codex-logs` while hiding repetitive dependency/deploy noise from the terminal:
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TrenchBroom
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "McpBridgeServer"
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "McpToolCatalog"
  ```
- If the filtered output hides something relevant, rerun with `-NoFilter` or inspect the matching log in `build-release-codex\codex-logs`.
- For UI/library work, build the focused test target first. If the filtered wrapper is unavailable, use the explicit Visual Studio environment form:
  ```powershell
  cmd.exe /c 'call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build-release-codex --target TbUiLibTest --config Release --parallel'
  ```
- If a plain `cmake --build build-release-codex ...` fails with missing SDK tools, missing `kernel32.lib`, or `type_traits`/STL lookup errors, assume the shell environment is incomplete and go back to `scripts\build_release_codex.cmd` instead of debugging source code first.
- Run focused Catch2 tests directly from the build tree, for example:
  ```powershell
  build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe "ModelBrowserView"
  build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe "GoldSrcSpritePreview"
  build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe "PythonV2"
  ```
- Before linking `TrenchBroom.exe`, make sure the Release app is not running. If link fails with `LINK : fatal error LNK1104: cannot open file 'app\TrenchBroom\TrenchBroom.exe'`, check for a stale process:
  ```powershell
  Get-Process | Where-Object { $_.ProcessName -like '*TrenchBroom*' } | Select-Object Id,ProcessName,Path
  ```
  Ask before killing a user process unless the user explicitly told you to close it.
- The expected Release executable is:
  ```text
  build-release-codex\app\TrenchBroom\TrenchBroom.exe
  ```
- Useful static checks before commit:
  ```powershell
  git diff --check
  rg -n "^(<<<<<<<|=======|>>>>>>>)" lib app CMakeLists.txt
  ```

## Current custom branch notes
- The active feature branch is `feature/latest-upstream-merge`; it contains upstream master plus local custom features. Do not assume upstream TrenchBroom behavior when touching custom areas.
- Important custom feature areas include:
  - Python v2 plugin/runtime work (`tb2`) and manifest-style plugins.
  - Unified GoldSrc asset browser for `.mdl`, `.spr`, and `.wav`.
  - GoldSrc sprite preview decoding and `ERROR` fallback placeholders.
  - Model browser drag/drop behavior for model and sprite assets.
  - 3D sky rendering for GoldSrc `skyname`.
  - 2D readable brush outlines.
  - Path Tool and Pie Menu action wiring.
  - Outliner and property editor customizations.
- Legacy Python `tb` compatibility was intentionally removed from the active plugin path. Do not reintroduce legacy `tb` module registration unless explicitly requested; new plugin examples should use `tb2` or `import tb2 as tb`.
- If a change touches the unified asset browser, run at least `TbUiLibTest "ModelBrowserView"` and any specific parser/preview tests such as `GoldSrcSpritePreview`.
- If a change touches Python plugins, run focused `TbUiLibTest` filters for `PythonV2` and `PythonPluginManifest`, plus any relevant panel/timer tests.
- If a change touches rendering, be careful with Release-only behavior and OpenGL resource lifetime. Several previous issues only reproduced in Release builds, so build the Release executable before declaring rendering work done.
- Keep local noise out of commits. In this project, `.codegraph/`, temporary markdown experiments, generated crash logs, and ad hoc asset/debug files should not be committed unless the user explicitly asks.
- When using web or external research for GoldSrc formats, prefer primary/simple references and record the practical decision in code or docs. Avoid copying large third-party implementations or license-sensitive code.

## MCP development governance
- Before adding or changing TrenchBroom MCP tools, read and follow `docs/mcp-development-governance.md`.
- C++ MCP is the guarded editor execution kernel. Do not add scene prefab tools such as `create_temple`, `create_courtyard`, `create_kz_route`, `create_racetrack`, `create_house`, or similar layout-specific generators.
- Put prefab-like composition, gameplay/domain judgement, and reusable scene families into skill recipes that emit IR files. MCP should preview/apply the IR, recover targets, validate geometry, and render reviews.
- Add a C++ MCP capability only when it needs TrenchBroom internals such as document guards, undo transactions, selection/object identity, live map geometry, validation, or review rendering.
- New high-volume MCP outputs must be compact by default (`idsMode:"count"` or `"sample"`, `detail:"summary"` style behavior) with full ids/details opt-in.
- Modeling profile growth requires justification. Prefer hidden/searchable expert tools over visible duplicate convenience aliases.
- For dense old maps or ambiguous brush ownership, prefer user selection plus selection-aware MCP tools instead of complex automatic brush matching.

### Code coverage
- **Enable coverage instrumentation**: Pass `-DTB_ENABLE_GCOV=1` for gcov-compatible coverage (works with GCC or Clang) or `-DTB_ENABLE_LCOV=1` for LLVM source-based coverage (Clang only).
- **Generate coverage data**:
  - For gcov: Build and run tests normally. `.gcno` and `.gcda` files are automatically generated in the build tree.
  - For LLVM/lcov: Run tests with `LLVM_PROFILE_FILE=default.profraw <test-executable>` to generate `.profraw` profile data.
- **Analyze coverage**: Use coverage tools to identify uncovered code paths, untested branches, and low-coverage functions.
- **Guide test improvements**: When reviewing or creating tests, examine coverage reports to identify and address gaps:
  - Suggest new tests for uncovered code paths or error conditions.
  - Improve existing tests to cover branch conditions not yet exercised.
  - Identify edge cases or exception handling that lack test coverage.
- **Reference coverage in commit messages**: When submitting test improvements motivated by coverage analysis, mention that coverage-guided testing was used to identify gaps.

## Test structure
- For each compilation unit, tests are usually in one file named tst_<CompilationUnit>.cpp.
- Prefer one test case per class.
- Prefer one section per member function.
- For free functions, prefer one test case per file and one section per function.

## Code style
- Format changes with clang-format. The repository style is defined in /.clang-format.
- Respect the existing include ordering rules from /.clang-format. In particular, Qt headers must come first.
- Follow the surrounding file's style and patterns unless there is a clear reason not to.

## Git History
- Keep the git history as clean as possible.
- Avoid unnecessary churn, including changing the same code multiple times in a branch when a cleaner edit is possible.
- Prefer changes that read like a clean transformation from the original state to the desired result.
- When creating a series of commits, keep each commit coherent, buildable, and with the relevant tests passing when practical.
- After completing and verifying a coherent source or documentation change, commit it without waiting for the user to ask again, unless the user explicitly says not to commit or the worktree contains unrelated changes that cannot be safely separated.
- Stage only files that belong to the completed change. Leave unrelated dirty files, generated maps, screenshots, logs, and temporary reports unstaged unless the user explicitly asks to include them.
- When asked to write commit messages, explain why the change was made in the context of a feature or bug fix, not just what changed.
