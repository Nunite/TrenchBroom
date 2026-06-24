# Performance Testing

`TbPerf` is a small Release-only benchmark runner for custom features that are likely
to affect editor responsiveness. It is intentionally separate from Catch2 tests so
normal correctness tests stay fast and deterministic.

## Quick Start

```powershell
scripts\perf-codex.ps1
```

The script builds `TbPerf`, runs the default `mcp` suite, prints a compact table, and
writes JSON to:

```text
build-release-codex\perf\latest.json
```

List available benchmarks:

```powershell
scripts\perf-codex.ps1 -List
```

Run without rebuilding:

```powershell
scripts\perf-codex.ps1 -NoBuild -Iterations 1000
```

Compare against an older run and fail if the median time regresses by more than 10%:

```powershell
scripts\perf-codex.ps1 -Baseline build-release-codex\perf\baseline.json -MaxRegressionPercent 10
```

## Current Coverage

The first suite focuses on MCP catalog and JSON-RPC response construction:

- `mcp.default_tool_catalog`
- `mcp.tools_list_json_edit`
- `mcp.tool_diagnostics_json_edit`
- `mcp.initialize_result`
- `mcp.tools_list_result_edit`
- `mcp.find_all_tools`

These benchmarks are useful because MCP is active during editor sessions and can
accidentally add UI-thread cost if tool catalog or response generation becomes too
heavy.

## Expansion Roadmap

Good next benchmark suites:

- `selection`: 2D box selection candidate filtering on synthetic map sizes.
- `asset`: GoldSrc `.mdl/.spr/.wav` asset scanning and preview state loading.
- `render`: frame timing probes for skybox, readable outlines, and overlay passes.
- `python`: plugin manifest scan, direct script load, timer cleanup.

Each benchmark should prefer stable model-level code over live UI automation. UI frame
timing can be added later with explicit smoke scenarios once the low-level suites are
stable.
