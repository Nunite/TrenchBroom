# TrenchBroom MCP HTTP/Stdio Protocol Parity Plan

## Goal

Make HTTP and stdio expose the configured tool profile and the same retained
operation/review resource behavior without changing tool schemas, permissions, or map
mutation semantics.

## Contract

- `McpBridgeRequest` remains backward compatible with untyped tool-call JSON.
- New local bridge request types are `tool_call`, `resources_list`, and
  `resource_read`.
- `resources/list` enumerates retained operation and review resources only.
- Pages contain at most 100 compact resource descriptors and use an opaque cursor.
- Evicted resources are omitted from enumeration; direct reads retain existing
  structured eviction guidance.
- Stdio uses `McpBridgeConfig::toolProfile`; `McpMode` remains the permission ceiling.

## Task 1: Typed Local Bridge Requests

1. Add failing message roundtrip tests for all request types and legacy tool requests.
2. Add failing client tests for resource list/read request serialization.
3. Extend bridge messages and refactor `McpBridgeClient` around one bounded request
   path.
4. Build and run `McpBridgeMessages` and `McpBridgeClient` tests.
5. Commit the typed bridge protocol.

## Task 2: Retained Resource Enumeration

1. Add failing session tests for operation/review descriptors, 100-entry pagination,
   invalid cursors, and omission of eviction hints.
2. Add failing local transport tests for authenticated typed list/read requests.
3. Implement deterministic resource descriptors and opaque cursor parsing in session
   state.
4. Route typed requests in `McpBridgeServer` after token validation.
5. Build and run session and bridge tests.
6. Commit resource enumeration and local dispatch.

## Task 3: Shared JSON-RPC and Stdio Adapter

1. Add failing JSON-RPC tests for `resources/list`, pagination, and provider errors.
2. Add a shared contract suite covering initialize, tools/list, tools/call, errors,
   notifications, resources/list, and resources/read against direct and stdio
   adapters.
3. Add a testable stdio adapter in `TbMcpLib`; make the executable delegate to it.
4. Pass configured profile, resource lister, and resource reader through HTTP and
   stdio.
5. Add focused HTTP tests for Core profile and resource list/read wiring.
6. Commit protocol parity.

## Verification

Run serially:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "Mcp*"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpStdioClientTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpStdioClientTest.exe -TestFilter "Mcp*"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "[McpBridgeServer]"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TrenchBroom
powershell -ExecutionPolicy Bypass -File scripts\mcp-reliability-acceptance.ps1
```

Finish with `git diff --check`, conflict-marker search, and a clean worktree.
