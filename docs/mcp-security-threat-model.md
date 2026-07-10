# TrenchBroom MCP Security Threat Model

## Scope

This document covers the local MCP HTTP server, the legacy stdio shim, bridge
configuration, tool authorization, document targeting, map mutation, external
process tools, and cached MCP resources. It assumes one desktop user and one
active TrenchBroom MCP instance.

## Protected Assets

- The active map and TrenchBroom undo/redo state.
- Unsaved user work in every open document.
- The MCP Bearer token and local configuration.
- Stable object ids, operation history, metadata, modules, previews, and review
  resources held in memory.
- Local files read or written by IR, review, export, heightmap, and compile tools.
- External commands launched by compile profiles or the legacy Python bridge.

## Trust Boundaries

The MCP client is outside the editor trust boundary. Loopback networking narrows
exposure but does not authenticate callers: any process running as the user can
connect to a local port. The Bearer token authenticates the client to TrenchBroom.

The stdio shim is also outside the editor process. It reads the same TrenchBroom
config and forwards requests through the authenticated local pipe. Skill recipes
are untrusted input producers: they may write IR, but only the C++ kernel may
validate and apply it.

## Main Threats And Controls

| Threat | Control |
| --- | --- |
| Unauthenticated local process calls MCP | Every `/mcp` GET and POST requires `Authorization: Bearer <token>`; failures return `401` and `WWW-Authenticate: Bearer`. |
| Browser-origin request abuses loopback | OPTIONS allows `Authorization`; CORS echoes only the accepted request origin. Authentication remains mandatory. |
| Token leaks through UI or generated config | Preferences hides the token. Copy actions insert it only into the clipboard command. Codex uses `TB_MCP_TOKEN` through `--bearer-token-env-var`. Config writes attempt owner read/write permissions. |
| Client requests more privilege than configured | The effective mode is the stricter of configured mode and `requestedMode`. Tool catalog mode checks run before dispatch. |
| Write reaches the wrong map | Mutating tools accept `expectedDocumentPath` and `expectedDocumentFingerprint`; when both are present, both must match. |
| Partial IR mutation leaves inconsistent editor/session state | `ir_apply` uses one outer native transaction and stages history, counters, metadata, modules, and object registry state. Any stage failure rolls back all state. |
| Previewed IR changes before apply | File preview records a content hash. `ir_apply_from_file` rejects a changed file before mutation. |
| Unsupported IR version changes semantics | New IR uses integer `schemaVersion:1`. Invalid versions, versions below 1, and future versions fail before mutation. |
| Timeout causes unsafe blind retry | Tool cost classes use 10/30/120-second response waits. Timeout diagnostics report `mutatedDocument:"unknown"`, `retrySafe:false`, request identity, and history recovery steps. |
| Unbounded session data exhausts memory | Session limits are 1024 operation records, 128 reviews, 64 previews with a 10-minute TTL, and four document fingerprints. Status and doctor expose counts and evictions. |
| Stale or evicted resource is mistaken for live state | Resource reads return structured eviction and recovery guidance. Object resolution reports stale/live/mismatch state. |
| Second instance steals an active bridge endpoint | Startup probes the pipe and HTTP listener. It refuses an active instance and removes only an inactive stale endpoint. |
| Arbitrary local execution through compile or Python | These tools require Edit mode, remain hidden/legacy where appropriate, and require explicit user authorization in Agent workflows. Scene recipes cannot call MCP, `tb2`, or edit `.map` files. |

## Protocol Requirements

- Requests declare `jsonrpc:"2.0"`.
- `params` must be an object; other values return `-32602`.
- Initialize advertises only protocol `2025-06-18`.
- HTTP listens on loopback only.
- The stdio shim uses the TrenchBroom application config path, not a separate
  `trenchbroom-mcp` config directory.

## Residual Risks

The token protects the endpoint from accidental and cross-process access, but any
process that can read the user's config or environment can impersonate the client.
Bearer authentication does not isolate hostile software running as the same OS
user.

Compile profiles and `python_generate_blockout` can execute local programs. Their
mode checks do not make untrusted scripts safe. Agents must obtain user approval
before compile or arbitrary Python execution and should prefer deterministic
recipes that emit IR.

MCP history mirrors native undo/redo. Manual user edits can invalidate the expected
stack order. Clients must refresh `history_status` after timeouts, manual edits, or
unexpected validation results.

## Verification

Run the automated gates:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "*"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "[McpBridgeServer]"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "[McpHttpServer]"
powershell -ExecutionPolicy Bypass -File scripts\mcp-reliability-acceptance.ps1
```

The real acceptance script uses disposable maps and checks HTTP authentication,
atomic IR undo/redo, rollback after entity failure, preview tampering, document
guards, a controlled stdio request longer than five seconds, review resource reads,
review-resource eviction recovery, and crash-log counts.
