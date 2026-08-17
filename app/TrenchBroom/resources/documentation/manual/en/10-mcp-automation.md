# MCP Automation {#mcp_automation}

## MCP Settings {#mcp_settings}

The MCP group configures TrenchBroom's optional localhost HTTP endpoint for external agents. Access is **Off** by default. **Read-only** allows inspection and captures; **Edit** also permits guarded map changes. The tool profile controls discovery size: **Core** is minimal, **Modeling** is the recommended default, and **Full** includes expert and debugging tools.

The displayed endpoint has the form `http://127.0.0.1:<port>/mcp`. Use **Copy URL** for a generic MCP client or **Copy Setup Command** for Claude Code. Changing access or profile rewrites the local MCP configuration and restarts the bridge.

The server listens only on the loopback interface and does not use a bearer token. Enabling Read-only or Edit therefore trusts other processes running as the same local user. Leave access Off when it is not needed, verify the active document before allowing edits, and use TrenchBroom's undo history to review changes.
