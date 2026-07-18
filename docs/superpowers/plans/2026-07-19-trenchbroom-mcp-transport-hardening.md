# TrenchBroom MCP Transport Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bound unauthenticated HTTP and local-socket request framing, connection counts, and incomplete-request lifetimes without changing valid MCP behavior.

**Architecture:** Add injectable production-default transport budget structs to the existing server classes, then enforce those budgets at the socket edge before JSON-RPC or tool dispatch. Keep HTTP and QLocalSocket policies independent, preserve current authentication and dispatch code, and use the existing Qt event loop with one single-shot deadline timer per accepted connection.

**Tech Stack:** C++20, Qt 6 Network/Core (`QTcpServer`, `QTcpSocket`, `QLocalServer`, `QLocalSocket`, `QTimer`), Catch2, CMake/Ninja, Windows Release build wrappers.

---

## Scope and File Map

Modify only these production files:

- `lib/TbUiLib/include/ui/mcp/McpHttpServer.h`: HTTP limits, injected constructor,
  connection/deadline state, and helper declarations.
- `lib/TbUiLib/src/mcp/McpHttpServer.cpp`: bounded HTTP framing, status responses,
  deadlines, and ordinary/SSE connection accounting.
- `lib/TbUiLib/include/ui/mcp/McpBridgeServer.h`: local bridge limits, injected
  constructors, connection/deadline state, and helper declarations.
- `lib/TbUiLib/src/mcp/McpBridgeServer.cpp`: constructor delegation for injected local
  bridge limits.
- `lib/TbUiLib/src/mcp/McpBridgeServerTransport.cpp`: bounded newline framing,
  deadlines, and local connection accounting.

Modify only these test files:

- `lib/TbUiLib/test/src/tst_McpHttpServer.cpp`: HTTP framing, fragmentation, timeout,
  ordinary connection, and SSE connection regression tests.
- `lib/TbUiLib/test/src/tst_McpBridgeServer.cpp`: local-socket framing, fragmentation,
  timeout, and connection-limit regression tests.

Do not change tool schemas, MCP profiles, resource behavior, recipes, IR, mutation
handlers, or map fixtures in this plan. Subproject 2 will handle stdio/HTTP protocol
parity separately.

Do not run two `build-filtered.ps1` commands concurrently against
`build-release-codex`.

### Task 1: Add Injectable Transport Budget Contracts

**Files:**

- Modify: `lib/TbUiLib/include/ui/mcp/McpHttpServer.h`
- Modify: `lib/TbUiLib/include/ui/mcp/McpBridgeServer.h`
- Modify: `lib/TbUiLib/src/mcp/McpHttpServer.cpp:138-142`
- Modify: `lib/TbUiLib/src/mcp/McpBridgeServer.cpp:71-896`
- Modify: `lib/TbUiLib/src/mcp/McpBridgeServer.cpp:908-919`
- Test: `lib/TbUiLib/test/src/tst_McpHttpServer.cpp`
- Test: `lib/TbUiLib/test/src/tst_McpBridgeServer.cpp`

- [ ] **Step 1: Write compile-failing default-budget contract tests**

Add this section immediately after the opening brace of the `McpHttpServer` test case
at current line 187:

```cpp
SECTION("uses bounded production defaults")
{
  const auto limits = McpHttpServerLimits{};
  CHECK(limits.maxHeaderBytes == 64 * 1024);
  CHECK(limits.maxBodyBytes == 4 * 1024 * 1024);
  CHECK(limits.incompleteRequestTimeoutMs == 10'000);
  CHECK(limits.maxOrdinaryConnections == 32);
  CHECK(limits.maxSseConnections == 8);
}
```

Add this section immediately after the opening brace of the `McpBridgeServer` test
case at current line 66:

```cpp
SECTION("uses bounded local transport defaults")
{
  const auto limits = McpBridgeTransportLimits{};
  CHECK(limits.maxRequestBytes == 4 * 1024 * 1024);
  CHECK(limits.incompleteRequestTimeoutMs == 10'000);
  CHECK(limits.maxConnections == 32);
}
```

- [ ] **Step 2: Build the focused UI test target and confirm the new tests do not compile**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest
```

Expected: build fails because `McpHttpServerLimits` and
`McpBridgeTransportLimits` are not declared. A failure for any other reason must be
investigated before continuing.

- [ ] **Step 3: Add the HTTP budget type and injected constructor**

In `McpHttpServer.h`, add `#include <QtGlobal>` with the Qt headers and declare this
type before `McpHttpServer`:

```cpp
struct McpHttpServerLimits
{
  qsizetype maxHeaderBytes = 64 * 1024;
  qsizetype maxBodyBytes = 4 * 1024 * 1024;
  int incompleteRequestTimeoutMs = 10'000;
  int maxOrdinaryConnections = 32;
  int maxSseConnections = 8;
};
```

Add the member and overload:

```cpp
private:
  McpBridgeServer& m_bridgeServer;
  McpHttpServerLimits m_limits;
  mcp::McpBridgeConfig m_config;
  std::unique_ptr<QTcpServer> m_server;

public:
  explicit McpHttpServer(McpBridgeServer& bridgeServer, QObject* parent = nullptr);
  McpHttpServer(
    McpBridgeServer& bridgeServer,
    McpHttpServerLimits limits,
    QObject* parent = nullptr);
```

Replace the current constructor implementation with delegating constructors:

```cpp
McpHttpServer::McpHttpServer(McpBridgeServer& bridgeServer, QObject* parent)
  : McpHttpServer{bridgeServer, McpHttpServerLimits{}, parent}
{
}

McpHttpServer::McpHttpServer(
  McpBridgeServer& bridgeServer, McpHttpServerLimits limits, QObject* parent)
  : QObject{parent}
  , m_bridgeServer{bridgeServer}
  , m_limits{std::move(limits)}
{
}
```

Add `<utility>` to `McpHttpServer.cpp` for `std::move`.

- [ ] **Step 4: Add the local bridge budget type and injected constructors**

In `McpBridgeServer.h`, add `#include <QtGlobal>` and declare this type before
`McpBridgeServer`:

```cpp
struct McpBridgeTransportLimits
{
  qsizetype maxRequestBytes = 4 * 1024 * 1024;
  int incompleteRequestTimeoutMs = 10'000;
  int maxConnections = 32;
};
```

Add `McpBridgeTransportLimits m_transportLimits;` immediately after `m_config`, and
add these overloads without removing the existing signatures:

```cpp
McpBridgeServer(
  AppController& appController,
  McpBridgeTransportLimits transportLimits,
  QObject* parent = nullptr);
McpBridgeServer(
  ToolHandler toolHandler,
  McpBridgeTransportLimits transportLimits,
  QObject* parent = nullptr);
McpBridgeServer(
  ToolHandler toolHandler,
  ActiveMapProvider activeMapProvider,
  McpBridgeTransportLimits transportLimits,
  QObject* parent = nullptr);
```

Make the existing constructors delegate to production defaults. Insert this default
constructor immediately before the current AppController constructor:

```cpp
McpBridgeServer::McpBridgeServer(AppController& appController, QObject* parent)
  : McpBridgeServer{appController, McpBridgeTransportLimits{}, parent}
{
}
```

Change the signature of the existing AppController constructor from:

```cpp
McpBridgeServer::McpBridgeServer(AppController& appController, QObject* parent)
```

to:

```cpp
McpBridgeServer::McpBridgeServer(
  AppController& appController,
  McpBridgeTransportLimits transportLimits,
  QObject* parent)
```

Leave the existing dispatcher lambda and constructor body unchanged. In its delegating
initializer, replace the final argument block:

```cpp
      parent}
```

with:

```cpp
      std::move(transportLimits),
      parent}
```

Then replace the current ToolHandler-only and ActiveMapProvider constructor
implementations with these complete overload pairs:

```cpp

McpBridgeServer::McpBridgeServer(ToolHandler toolHandler, QObject* parent)
  : McpBridgeServer{
      std::move(toolHandler), McpBridgeTransportLimits{}, parent}
{
}

McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler,
  McpBridgeTransportLimits transportLimits,
  QObject* parent)
  : QObject{parent}
  , m_transportLimits{std::move(transportLimits)}
  , m_toolHandler{std::move(toolHandler)}
{
}

McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler, ActiveMapProvider activeMapProvider, QObject* parent)
  : McpBridgeServer{
      std::move(toolHandler),
      std::move(activeMapProvider),
      McpBridgeTransportLimits{},
      parent}
{
}

McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler,
  ActiveMapProvider activeMapProvider,
  McpBridgeTransportLimits transportLimits,
  QObject* parent)
  : QObject{parent}
  , m_transportLimits{std::move(transportLimits)}
  , m_toolHandler{std::move(toolHandler)}
  , m_activeMapProvider{std::move(activeMapProvider)}
{
}
```

This task changes constructor routing only; it does not edit any tool-name branch,
registry loop, or active-map-provider statement inside the retained AppController
constructor body.

- [ ] **Step 5: Format and run both focused contract filters**

Run:

```powershell
clang-format -i lib\TbUiLib\include\ui\mcp\McpHttpServer.h lib\TbUiLib\include\ui\mcp\McpBridgeServer.h lib\TbUiLib\src\mcp\McpHttpServer.cpp lib\TbUiLib\src\mcp\McpBridgeServer.cpp lib\TbUiLib\test\src\tst_McpHttpServer.cpp lib\TbUiLib\test\src\tst_McpBridgeServer.cpp
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpBridgeServer"
```

Expected: both filters pass; constructor injection has not changed behavior.

- [ ] **Step 6: Commit the budget seam**

```powershell
git add lib\TbUiLib\include\ui\mcp\McpHttpServer.h lib\TbUiLib\include\ui\mcp\McpBridgeServer.h lib\TbUiLib\src\mcp\McpHttpServer.cpp lib\TbUiLib\src\mcp\McpBridgeServer.cpp lib\TbUiLib\test\src\tst_McpHttpServer.cpp lib\TbUiLib\test\src\tst_McpBridgeServer.cpp
git commit -m "Add injectable MCP transport budgets" -m "Make production framing and connection limits explicit so focused tests can exercise boundary behavior without long waits or multi-megabyte payloads."
```

### Task 2: Enforce HTTP Framing Limits

**Files:**

- Modify: `lib/TbUiLib/src/mcp/McpHttpServer.cpp:40-136`
- Modify: `lib/TbUiLib/src/mcp/McpHttpServer.cpp:235-403`
- Test: `lib/TbUiLib/test/src/tst_McpHttpServer.cpp:38-185`
- Test: `lib/TbUiLib/test/src/tst_McpHttpServer.cpp:187-422`

- [ ] **Step 1: Refactor the HTTP response reader so tests can fragment writes**

Extract the read half of `sendRequest` into this helper, then make `sendRequest` call
it after writing:

```cpp
HttpResponse readResponse(QTcpSocket& socket, const int timeoutMs = 2000)
{
  auto bytes = QByteArray{};
  auto timer = QElapsedTimer{};
  timer.start();
  while (timer.elapsed() < timeoutMs && socket.state() != QAbstractSocket::UnconnectedState)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10))
    {
      bytes += socket.readAll();
    }
    if (bytes.contains("Content-Type: text/event-stream") && bytes.contains("\r\n\r\n"))
    {
      break;
    }
  }
  bytes += socket.readAll();

  const auto headerEnd = bytes.indexOf("\r\n\r\n");
  REQUIRE(headerEnd >= 0);
  const auto statusLine = bytes.left(bytes.indexOf("\r\n"));
  const auto statusParts = statusLine.split(' ');
  REQUIRE(statusParts.size() >= 2);

  auto ok = false;
  const auto statusCode = statusParts[1].toInt(&ok);
  REQUIRE(ok);
  return HttpResponse{statusCode, bytes.left(headerEnd), bytes.mid(headerEnd + 4)};
}
```

- [ ] **Step 2: Add failing HTTP framing tests**

Add sections covering each rejected shape with small injected limits:

```cpp
SECTION("accepts a valid fragmented request")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{512, 512, 10'000, 32, 8}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));

  const auto request =
    makeHttpRequest("POST", "/mcp", jsonRequest(20, "initialize"));
  auto socket = QTcpSocket{};
  socket.connectToHost("127.0.0.1", httpServer.port());
  REQUIRE(socket.waitForConnected(1000));
  const auto split = request.size() / 2;
  REQUIRE(socket.write(request.left(split)) == split);
  REQUIRE(socket.waitForBytesWritten(1000));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  CHECK(socket.bytesAvailable() == 0);
  REQUIRE(socket.write(request.mid(split)) == request.size() - split);
  REQUIRE(socket.waitForBytesWritten(1000));
  CHECK(readResponse(socket).statusCode == 200);
}

SECTION("rejects a header beyond the configured budget")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{96, 512, 10'000, 32, 8}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));
  auto request = makeHttpRequest("POST", "/mcp", jsonRequest(21, "initialize"));
  request.replace(
    "Connection: close\r\n",
    "Connection: close\r\nX-Fill: " + QByteArray(128, 'x') + "\r\n");
  CHECK(sendRequest(httpServer.port(), request).statusCode == 431);
}

SECTION("rejects a body length beyond the configured budget")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{512, 32, 10'000, 32, 8}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));
  auto request = makeHttpRequest("POST", "/mcp", {});
  request.replace("Content-Length: 0", "Content-Length: 33");
  CHECK(sendRequest(httpServer.port(), request).statusCode == 413);
}

SECTION("rejects duplicate content length fields")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{bridgeServer};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));
  auto request = makeHttpRequest("POST", "/mcp", {});
  request.replace(
    "Content-Length: 0\r\n",
    "Content-Length: 0\r\nContent-Length: 0\r\n");
  CHECK(sendRequest(httpServer.port(), request).statusCode == 400);
}

SECTION("rejects unsupported transfer encoding")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{bridgeServer};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));
  auto request = makeHttpRequest("POST", "/mcp", {});
  request.replace(
    "Content-Length: 0\r\n",
    "Transfer-Encoding: chunked\r\nContent-Length: 0\r\n");
  CHECK(sendRequest(httpServer.port(), request).statusCode == 400);
}
```

- [ ] **Step 3: Run the HTTP filter and verify the new cases fail**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
```

Expected: fragmented input still passes, while at least the 431, 413, duplicate
`Content-Length`, and `Transfer-Encoding` expectations fail against the old parser.

- [ ] **Step 4: Implement bounded header parsing and status codes**

Extend `reasonPhrase` with:

```cpp
case 408:
  return "Request Timeout";
case 413:
  return "Content Too Large";
case 431:
  return "Request Header Fields Too Large";
case 503:
  return "Service Unavailable";
```

Replace the single-value header scanner with these helpers:

```cpp
QList<QByteArray> headerValues(const QList<QByteArray>& lines, const QByteArray& key)
{
  const auto lowerKey = key.toLower() + ":";
  auto values = QList<QByteArray>{};
  for (int i = 1; i < lines.size(); ++i)
  {
    const auto line = lines[i];
    if (line.toLower().startsWith(lowerKey))
    {
      values.push_back(line.mid(lowerKey.size()).trimmed());
    }
  }
  return values;
}

QString headerValue(const QList<QByteArray>& lines, const QByteArray& key)
{
  return QString::fromUtf8(headerValues(lines, key).value(0));
}
```

At the start of `handleSocketReadyRead`, return immediately for a completed request,
then compute the aggregate budget and enforce it before peeking:

```cpp
if (socket.property("tbMcpHttpRequestComplete").toBool())
{
  return;
}

const auto maxRequestBytes =
  m_limits.maxHeaderBytes + 4 + m_limits.maxBodyBytes;
if (socket.bytesAvailable() > maxRequestBytes)
{
  socket.setProperty("tbMcpHttpRequestComplete", true);
  writeHttpResponse(
    socket, 413, reasonPhrase(413), "application/json", jsonBody("HTTP request too large"));
  socket.disconnectFromHost();
  return;
}

const auto requestBytes = socket.peek(maxRequestBytes);
```

After finding `headerEnd`, reject `headerEnd > m_limits.maxHeaderBytes` with 431.
Before parsing content length, reject any `Transfer-Encoding` value and reject a
`Content-Length` list whose size is greater than one. Parse the one allowed value with
`toLongLong`, reject malformed/negative values with 400, and reject values above
`m_limits.maxBodyBytes` with 413.

Use `const auto totalLength = qsizetype{headerEnd + 4} + contentLength;`. Keep waiting
when fewer bytes are present. After `socket.read(totalLength)`, set the
`tbMcpHttpRequestComplete` property before endpoint, origin, authentication, method,
or JSON-RPC dispatch logic runs. This prevents pipelined bytes or SSE client input from
being interpreted as a second request on the same connection.

- [ ] **Step 5: Format and rerun the HTTP filter**

```powershell
clang-format -i lib\TbUiLib\src\mcp\McpHttpServer.cpp lib\TbUiLib\test\src\tst_McpHttpServer.cpp
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
```

Expected: all existing and new `McpHttpServer` sections pass.

- [ ] **Step 6: Commit HTTP framing hardening**

```powershell
git add lib\TbUiLib\src\mcp\McpHttpServer.cpp lib\TbUiLib\test\src\tst_McpHttpServer.cpp
git commit -m "Bound MCP HTTP request framing" -m "Reject oversized, duplicated, and unsupported request framing before authentication so malformed local clients cannot retain unbounded input."
```

### Task 3: Bound HTTP Request Lifetimes and Connections

**Files:**

- Modify: `lib/TbUiLib/include/ui/mcp/McpHttpServer.h`
- Modify: `lib/TbUiLib/src/mcp/McpHttpServer.cpp:149-205`
- Modify: `lib/TbUiLib/src/mcp/McpHttpServer.cpp:223-233`
- Modify: `lib/TbUiLib/src/mcp/McpHttpServer.cpp:235-422`
- Test: `lib/TbUiLib/test/src/tst_McpHttpServer.cpp`

- [ ] **Step 1: Add failing timeout and connection-budget tests**

Add sections using short injected deadlines and small connection limits:

```cpp
SECTION("times out an incomplete request")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{512, 512, 50, 32, 8}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));

  auto socket = QTcpSocket{};
  socket.connectToHost("127.0.0.1", httpServer.port());
  REQUIRE(socket.waitForConnected(1000));
  REQUIRE(socket.write("POST /mcp HTTP/1.1\r\n") > 0);
  REQUIRE(socket.waitForBytesWritten(1000));
  CHECK(readResponse(socket, 1000).statusCode == 408);
}

SECTION("times out when the declared body never arrives")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{512, 512, 50, 32, 8}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));

  auto socket = QTcpSocket{};
  socket.connectToHost("127.0.0.1", httpServer.port());
  REQUIRE(socket.waitForConnected(1000));
  auto request = makeHttpRequest("POST", "/mcp", {});
  request.replace("Content-Length: 0", "Content-Length: 16");
  REQUIRE(socket.write(request) == request.size());
  REQUIRE(socket.waitForBytesWritten(1000));
  CHECK(readResponse(socket, 1000).statusCode == 408);
}

SECTION("rejects aggregate bytes beyond the request budget")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{128, 32, 10'000, 32, 8}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));

  auto request = makeHttpRequest("POST", "/mcp", {});
  request += QByteArray(256, 'x');
  CHECK(sendRequest(httpServer.port(), request).statusCode == 413);
}

SECTION("rejects excess ordinary connections and recovers capacity")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{512, 512, 10'000, 1, 1}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));

  auto first = QTcpSocket{};
  first.connectToHost("127.0.0.1", httpServer.port());
  REQUIRE(first.waitForConnected(1000));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

  auto second = QTcpSocket{};
  second.connectToHost("127.0.0.1", httpServer.port());
  REQUIRE(second.waitForConnected(1000));
  CHECK(readResponse(second).statusCode == 503);

  first.disconnectFromHost();
  if (first.state() != QAbstractSocket::UnconnectedState)
  {
    REQUIRE(first.waitForDisconnected(1000));
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  CHECK(
    sendRequest(
      httpServer.port(),
      makeHttpRequest("POST", "/mcp", jsonRequest(30, "initialize")))
      .statusCode
    == 200);
}

SECTION("bounds authenticated SSE connections")
{
  auto bridgeServer = makeBridgeServer();
  auto httpServer = McpHttpServer{
    bridgeServer, McpHttpServerLimits{512, 512, 10'000, 2, 1}};
  const auto config = makeConfig(mcp::McpMode::ReadOnly);
  REQUIRE(bridgeServer.start(config));
  REQUIRE(httpServer.start(config));

  auto first = QTcpSocket{};
  first.connectToHost("127.0.0.1", httpServer.port());
  REQUIRE(first.waitForConnected(1000));
  const auto request = makeHttpRequest("GET", "/mcp");
  REQUIRE(first.write(request) == request.size());
  REQUIRE(first.waitForBytesWritten(1000));
  CHECK(readResponse(first).statusCode == 200);

  CHECK(sendRequest(httpServer.port(), request).statusCode == 503);
}
```

- [ ] **Step 2: Run the HTTP filter and verify timeout/capacity failures**

Run the `McpHttpServer` wrapper command from Task 2.

Expected: the incomplete request has no 408 response and excess connections are not
rejected with 503.

- [ ] **Step 3: Add HTTP connection and deadline state**

In `McpHttpServer.h`, add Qt includes for `QHash` and `QSet`, forward-declare `QTimer`,
and add:

```cpp
  QSet<QTcpSocket*> m_connections;
  QSet<QTcpSocket*> m_sseConnections;
  QHash<QTcpSocket*, QTimer*> m_requestDeadlines;

  int ordinaryConnectionCount() const;
  void startRequestDeadline(QTcpSocket& socket);
  void completeRequest(QTcpSocket& socket);
  void removeConnection(QTcpSocket& socket);
```

In `start`, call:

```cpp
m_server->setMaxPendingConnections(
  m_limits.maxOrdinaryConnections + m_limits.maxSseConnections);
```

Implement the helpers in `McpHttpServer.cpp` using a single-shot `QTimer` parented to
each socket:

```cpp
int McpHttpServer::ordinaryConnectionCount() const
{
  return m_connections.size() - m_sseConnections.size();
}

void McpHttpServer::startRequestDeadline(QTcpSocket& socket)
{
  auto* timer = new QTimer{&socket};
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, socket = &socket]() {
    if (!m_connections.contains(socket)
        || socket->property("tbMcpHttpRequestComplete").toBool())
    {
      return;
    }
    socket->setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      *socket,
      408,
      reasonPhrase(408),
      "application/json",
      jsonBody("HTTP request timed out"));
    socket->disconnectFromHost();
  });
  m_requestDeadlines.insert(&socket, timer);
  timer->start(m_limits.incompleteRequestTimeoutMs);
}

void McpHttpServer::completeRequest(QTcpSocket& socket)
{
  socket.setProperty("tbMcpHttpRequestComplete", true);
  if (auto* timer = m_requestDeadlines.value(&socket))
  {
    timer->stop();
  }
}

void McpHttpServer::removeConnection(QTcpSocket& socket)
{
  m_requestDeadlines.remove(&socket);
  m_sseConnections.remove(&socket);
  m_connections.remove(&socket);
  socket.deleteLater();
}
```

Include `QTimer`. In `handleNewConnection`, parent each accepted socket first and
connect its `disconnected` signal before checking capacity. The disconnected lambda
must call `removeConnection` for a tracked socket and `deleteLater` for a rejected,
untracked socket. If `ordinaryConnectionCount() >= maxOrdinaryConnections`, write 503
and disconnect without inserting it. Otherwise insert it, set
`setReadBufferSize(maxHeaderBytes + 4 + maxBodyBytes + 1)`, start its deadline, and
continue with the already installed disconnected handler.

In `handleSocketReadyRead`, replace the direct property write after `socket.read` with
`completeRequest(socket)`. Before `writeSseStream`, reject with 503 when
`m_sseConnections.size() >= maxSseConnections`; otherwise insert the socket into
`m_sseConnections` and keep it open.

In `stop`, disconnect sockets as today, then clear all three containers. Timers are
socket children and must not be deleted separately.

- [ ] **Step 4: Format and rerun HTTP tests**

```powershell
clang-format -i lib\TbUiLib\include\ui\mcp\McpHttpServer.h lib\TbUiLib\src\mcp\McpHttpServer.cpp lib\TbUiLib\test\src\tst_McpHttpServer.cpp
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
```

Expected: all HTTP tests pass without waiting for the 10-second production deadline.

- [ ] **Step 5: Commit HTTP lifetime budgets**

```powershell
git add lib\TbUiLib\include\ui\mcp\McpHttpServer.h lib\TbUiLib\src\mcp\McpHttpServer.cpp lib\TbUiLib\test\src\tst_McpHttpServer.cpp
git commit -m "Bound MCP HTTP connection lifetimes" -m "Expire incomplete requests and cap ordinary and SSE connections so unauthenticated or abandoned loopback clients cannot accumulate socket state."
```

### Task 4: Bound Local Bridge Framing and Connections

**Files:**

- Modify: `lib/TbUiLib/include/ui/mcp/McpBridgeServer.h`
- Modify: `lib/TbUiLib/src/mcp/McpBridgeServerTransport.cpp:194-276`
- Modify: `lib/TbUiLib/src/mcp/McpBridgeServerTransport.cpp:594-645`
- Test: `lib/TbUiLib/test/src/tst_McpBridgeServer.cpp:20-60`
- Test: `lib/TbUiLib/test/src/tst_McpBridgeServer.cpp:628-682`

- [ ] **Step 1: Add local-socket test helpers**

Add Qt includes for `QCoreApplication`, `QElapsedTimer`, and `QLocalSocket`. Add these
helpers before the bridge test case:

```cpp
mcp::McpBridgeResponse readBridgeResponse(
  QLocalSocket& socket, const int timeoutMs = 2000)
{
  auto timer = QElapsedTimer{};
  timer.start();
  while (timer.elapsed() < timeoutMs && !socket.canReadLine())
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    socket.waitForReadyRead(10);
  }
  REQUIRE(socket.canReadLine());
  auto parseError = QJsonParseError{};
  const auto document =
    QJsonDocument::fromJson(socket.readLine().trimmed(), &parseError);
  REQUIRE(parseError.error == QJsonParseError::NoError);
  REQUIRE(document.isObject());
  auto error = QString{};
  const auto response = mcp::bridgeResponseFromJson(document.object(), &error);
  INFO(error.toStdString());
  REQUIRE(response);
  return *response;
}

QString uniqueBridgePipeName()
{
  return QString{"trenchbroom-mcp-transport-test-%1"}.arg(
    QUuid::createUuid().toString(QUuid::WithoutBraces));
}
```

- [ ] **Step 2: Add failing local framing, timeout, and capacity tests**

Add a dedicated test case tagged with `[McpBridgeServer][McpBridgeTransport]`:

```cpp
TEST_CASE(
  "McpBridgeServer bounds local transport input",
  "[McpBridgeServer][McpBridgeTransport]")
{
  const auto makeServer = [](const McpBridgeTransportLimits limits) {
    return McpBridgeServer{
      [](const QString&, const QJsonObject&) {
        return McpBridgeToolResult::success(QJsonObject{{"application", "TrenchBroom"}});
      },
      limits};
  };

  SECTION("accepts a fragmented valid request")
  {
    auto server = makeServer(McpBridgeTransportLimits{512, 10'000, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(
      mcp::McpBridgeConfig{pipeName, "secret", mcp::McpMode::ReadOnly}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));
    auto line = QJsonDocument{mcp::toJson(mcp::McpBridgeRequest{
      "1", "secret", "tb_status", {}, mcp::McpMode::ReadOnly})}
                  .toJson(QJsonDocument::Compact)
                + "\n";
    const auto split = line.size() / 2;
    REQUIRE(socket.write(line.left(split)) == split);
    REQUIRE(socket.waitForBytesWritten(1000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    CHECK_FALSE(socket.canReadLine());
    REQUIRE(socket.write(line.mid(split)) == line.size() - split);
    REQUIRE(socket.waitForBytesWritten(1000));
    CHECK(readBridgeResponse(socket).ok);
  }

  SECTION("rejects a request line beyond the configured budget")
  {
    auto server = makeServer(McpBridgeTransportLimits{64, 10'000, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(
      mcp::McpBridgeConfig{pipeName, "secret", mcp::McpMode::ReadOnly}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));
    REQUIRE(socket.write(QByteArray(65, 'x')) == 65);
    REQUIRE(socket.waitForBytesWritten(1000));
    const auto response = readBridgeResponse(socket);
    CHECK_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::InvalidRequest);
    CHECK(response.error->message.contains("too large"));
  }

  SECTION("times out an incomplete request line")
  {
    auto server = makeServer(McpBridgeTransportLimits{512, 50, 4});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(
      mcp::McpBridgeConfig{pipeName, "secret", mcp::McpMode::ReadOnly}));
    auto socket = QLocalSocket{};
    socket.connectToServer(pipeName);
    REQUIRE(socket.waitForConnected(1000));
    REQUIRE(socket.write("{") == 1);
    REQUIRE(socket.waitForBytesWritten(1000));
    const auto response = readBridgeResponse(socket, 1000);
    CHECK_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(response.error->message.contains("timed out"));
  }

  SECTION("rejects excess local connections and recovers capacity")
  {
    auto server = makeServer(McpBridgeTransportLimits{512, 10'000, 1});
    const auto pipeName = uniqueBridgePipeName();
    REQUIRE(server.start(
      mcp::McpBridgeConfig{pipeName, "secret", mcp::McpMode::ReadOnly}));
    auto first = QLocalSocket{};
    first.connectToServer(pipeName);
    REQUIRE(first.waitForConnected(1000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

    auto second = QLocalSocket{};
    second.connectToServer(pipeName);
    REQUIRE(second.waitForConnected(1000));
    const auto rejected = readBridgeResponse(second);
    CHECK_FALSE(rejected.ok);
    REQUIRE(rejected.error);
    CHECK(rejected.error->message.contains("connection limit"));

    first.disconnectFromServer();
    if (first.state() != QLocalSocket::UnconnectedState)
    {
      REQUIRE(first.waitForDisconnected(1000));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    auto third = QLocalSocket{};
    third.connectToServer(pipeName);
    REQUIRE(third.waitForConnected(1000));
    const auto line = QJsonDocument{mcp::toJson(mcp::McpBridgeRequest{
      "3", "secret", "tb_status", {}, mcp::McpMode::ReadOnly})}
                        .toJson(QJsonDocument::Compact)
                      + "\n";
    REQUIRE(third.write(line) == line.size());
    REQUIRE(third.waitForBytesWritten(1000));
    CHECK(readBridgeResponse(third).ok);
  }
}
```

- [ ] **Step 3: Run the bridge transport filter and verify failures**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "[McpBridgeTransport]"
```

Expected: fragmented valid input passes, while oversize, timeout, and connection-limit
sections fail because the current local server waits indefinitely or accepts all
connections.

- [ ] **Step 4: Add local connection and deadline state**

In `McpBridgeServer.h`, add Qt includes for `QHash` and `QSet`, forward-declare
`QTimer`, and add:

```cpp
  QSet<QLocalSocket*> m_connections;
  QHash<QLocalSocket*, QTimer*> m_requestDeadlines;

  void startRequestDeadline(QLocalSocket& socket);
  void restartRequestDeadline(QLocalSocket& socket);
  void rejectAndDisconnect(QLocalSocket& socket, const QString& message) const;
  void removeConnection(QLocalSocket& socket);
```

In `start`, call `m_server->setMaxPendingConnections(m_transportLimits.maxConnections)`.
In `stop`, disconnect all tracked sockets before resetting the server, then clear the
connection and deadline containers.

Implement helpers in `McpBridgeServerTransport.cpp` with `QTimer` included:

```cpp
void McpBridgeServer::startRequestDeadline(QLocalSocket& socket)
{
  auto* timer = new QTimer{&socket};
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, socket = &socket]() {
    if (!m_connections.contains(socket))
    {
      return;
    }
    rejectAndDisconnect(*socket, "MCP bridge request timed out");
  });
  m_requestDeadlines.insert(&socket, timer);
  timer->start(m_transportLimits.incompleteRequestTimeoutMs);
}

void McpBridgeServer::restartRequestDeadline(QLocalSocket& socket)
{
  if (auto* timer = m_requestDeadlines.value(&socket))
  {
    timer->start(m_transportLimits.incompleteRequestTimeoutMs);
  }
}

void McpBridgeServer::rejectAndDisconnect(
  QLocalSocket& socket, const QString& message) const
{
  writeResponse(
    socket,
    mcp::McpBridgeResponse::failure(
      {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, message}));
  socket.disconnectFromServer();
}

void McpBridgeServer::removeConnection(QLocalSocket& socket)
{
  m_requestDeadlines.remove(&socket);
  m_connections.remove(&socket);
  socket.deleteLater();
}
```

In `handleNewConnection`, parent the socket first and install a disconnected lambda
that calls `removeConnection` for tracked sockets and `deleteLater` for rejected,
untracked sockets. Reject with "MCP bridge connection limit reached" when
`m_connections.size() >= maxConnections`. Otherwise insert the socket, set its read
buffer size to `maxRequestBytes + 1`, start its deadline, and connect `readyRead`.

At the start of `handleSocketReadyRead`, when no newline is available and
`bytesAvailable() > maxRequestBytes`, reject with "MCP bridge request too large". For
each readable line, remove one trailing newline before checking
`line.size() > maxRequestBytes`; reject and return when oversized. Parse the bounded
line as today, write exactly one response, and restart the deadline after each complete
line so persistent valid clients receive a fresh 10-second idle/request window.

- [ ] **Step 5: Format and run all bridge tests**

```powershell
clang-format -i lib\TbUiLib\include\ui\mcp\McpBridgeServer.h lib\TbUiLib\src\mcp\McpBridgeServerTransport.cpp lib\TbUiLib\test\src\tst_McpBridgeServer.cpp
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "[McpBridgeServer]"
```

Expected: all existing bridge behavior plus the new transport cases pass.

- [ ] **Step 6: Commit local transport hardening**

```powershell
git add lib\TbUiLib\include\ui\mcp\McpBridgeServer.h lib\TbUiLib\src\mcp\McpBridgeServerTransport.cpp lib\TbUiLib\test\src\tst_McpBridgeServer.cpp
git commit -m "Bound MCP local bridge connections" -m "Limit newline-delimited bridge requests, expire incomplete input, and cap active local clients before token parsing."
```

### Task 5: Full Verification and Real TrenchBroom Acceptance

**Files:**

- Verify only; no planned source changes.

- [ ] **Step 1: Run focused library and transport tests serially**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "Mcp*"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpStdioClientTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpStdioClientTest.exe -TestFilter "McpBridgeClient"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "[McpBridgeServer]"
```

Expected: every command exits 0 with no failed Catch2 assertion.

- [ ] **Step 2: Check for a running TrenchBroom before linking**

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*TrenchBroom*' } | Select-Object Id,ProcessName,Path
```

Expected: no TrenchBroom process. If a user-owned process is running, stop and ask
before terminating it.

- [ ] **Step 3: Build the Release application**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TrenchBroom
```

Expected: `build-release-codex\app\TrenchBroom\TrenchBroom.exe` links successfully.

- [ ] **Step 4: Run real disposable-map reliability acceptance**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\mcp-reliability-acceptance.ps1
```

Expected: script exits 0, guarded mutations/undo/module/review scenarios pass, and the
reported new crash-log count is zero. Keep generated maps, manifests, and captures in
`build-release-codex`; do not stage them.

- [ ] **Step 5: Run static completion checks**

```powershell
git diff --check
rg -n "^(<<<<<<<|=======$|>>>>>>>)( |$)" lib app CMakeLists.txt
git status --short
```

Expected: no whitespace errors, no conflict markers, and no uncommitted source changes.
Build outputs and acceptance artifacts remain ignored/untracked outside the commit.

- [ ] **Step 6: Review the three implementation commits**

```powershell
git log -4 --oneline --decorate
git show --stat --oneline HEAD~2..HEAD
```

Expected: one budget-seam commit, one HTTP framing/lifetime sequence as planned, and
one local bridge hardening commit. If Task 2 and Task 3 produced separate HTTP commits,
the range will contain four implementation commits; all must remain independently
buildable.

Do not create an extra "verification" commit when verification changes no tracked
files.
