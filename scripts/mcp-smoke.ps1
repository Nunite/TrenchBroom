param(
  [string] $Url = "",
  [string] $Token = "",
  [ValidateSet("None", "Current", "3D", "2D")]
  [string] $Capture = "None",
  [int] $TimeoutSec = 5,
  [switch] $Overlay,
  [switch] $ClearOverlay,
  [switch] $RawJson
)

$ErrorActionPreference = "Stop"

function ConvertTo-CompactJson {
  param([object] $Value)

  return $Value | ConvertTo-Json -Depth 64 -Compress
}

function Get-PropertyValue {
  param(
    [object] $Object,
    [string] $Name
  )

  if ($null -eq $Object) {
    return $null
  }

  $property = $Object.PSObject.Properties[$Name]
  if ($null -eq $property) {
    return $null
  }

  return $property.Value
}

function Show-JsonSnippet {
  param(
    [string] $Title,
    [object] $Value
  )

  Write-Host "  $Title $((ConvertTo-CompactJson $Value))"
}

function Invoke-McpRequest {
  param(
    [string] $Url,
    [string] $Token,
    [string] $Method,
    [object] $Params,
    [int] $TimeoutSec
  )

  $script:NextRequestId++
  $request = [ordered] @{
    jsonrpc = "2.0"
    id = $script:NextRequestId
    method = $Method
  }

  if ($null -ne $Params) {
    $request.params = $Params
  }

  $headers = @{
    Authorization = "Bearer $Token"
    Accept = "application/json, text/event-stream"
    "MCP-Protocol-Version" = "2025-06-18"
  }

  $response = Invoke-RestMethod `
    -Method Post `
    -Uri $Url `
    -Headers $headers `
    -ContentType "application/json" `
    -Body (ConvertTo-CompactJson $request) `
    -TimeoutSec $TimeoutSec

  $error = Get-PropertyValue $response "error"
  if ($null -ne $error) {
    throw "$Method failed: $($error.message)"
  }

  return $response
}

function Invoke-McpTool {
  param(
    [string] $Url,
    [string] $Token,
    [string] $Name,
    [object] $Arguments,
    [int] $TimeoutSec
  )

  if ($null -eq $Arguments) {
    $Arguments = @{}
  }

  return Invoke-McpRequest `
    -Url $Url `
    -Token $Token `
    -Method "tools/call" `
    -Params ([ordered] @{ name = $Name; arguments = $Arguments }) `
    -TimeoutSec $TimeoutSec
}

function Show-ToolResult {
  param(
    [string] $Name,
    [object] $Response,
    [switch] $RawJson
  )

  $result = Get-PropertyValue $Response "result"
  $isError = [bool] (Get-PropertyValue $result "isError")
  $structured = Get-PropertyValue $result "structuredContent"

  if ($isError) {
    $code = Get-PropertyValue $structured "code"
    $message = Get-PropertyValue $structured "message"
    if ([string]::IsNullOrWhiteSpace($code)) {
      $code = "Error"
    }
    if ([string]::IsNullOrWhiteSpace($message)) {
      $message = (Get-PropertyValue ((Get-PropertyValue $result "content") | Select-Object -First 1) "text")
    }

    Write-Host "$Name`: error [$code] $message" -ForegroundColor Red
    if ($RawJson -and $null -ne $structured) {
      Show-JsonSnippet -Title "structured:" -Value $structured
    }
    return $false
  }

  Write-Host "$Name`: ok" -ForegroundColor Green
  if ($RawJson -and $null -ne $structured) {
    Show-JsonSnippet -Title "structured:" -Value $structured
  }
  return $true
}

$configPath = Join-Path $env:APPDATA "TrenchBroom\MCP\config.json"
if (Test-Path $configPath) {
  $config = Get-Content -Path $configPath -Raw | ConvertFrom-Json
  if ([string]::IsNullOrWhiteSpace($Url)) {
    $hostName = if ([string]::IsNullOrWhiteSpace($config.httpHost)) { "127.0.0.1" } else { $config.httpHost }
    $port = if ($null -eq $config.httpPort) { 37666 } else { $config.httpPort }
    $Url = "http://$hostName`:$port/mcp"
  }
  if ([string]::IsNullOrWhiteSpace($Token)) {
    $Token = $config.token
  }

  Write-Host "MCP config: $configPath"
  Write-Host "Config mode: $($config.mode)"
} else {
  throw "MCP config does not exist: $configPath. Start TrenchBroom once and enable MCP in Preferences > MCP."
}

if ([string]::IsNullOrWhiteSpace($Url) -or [string]::IsNullOrWhiteSpace($Token)) {
  throw "MCP Url and Token are required."
}

Write-Host "MCP HTTP URL: $Url"

$script:NextRequestId = 0
$hadToolError = $false

$initialize = Invoke-McpRequest `
  -Url $Url `
  -Token $Token `
  -Method "initialize" `
  -Params ([ordered] @{
    protocolVersion = "2025-06-18"
    capabilities = @{}
    clientInfo = [ordered] @{ name = "trenchbroom-mcp-smoke"; version = "0.2.0" }
  }) `
  -TimeoutSec $TimeoutSec
Write-Host "initialize: ok" -ForegroundColor Green
if ($RawJson) {
  Show-JsonSnippet -Title "result:" -Value $initialize.result
}

$toolsList = Invoke-McpRequest `
  -Url $Url `
  -Token $Token `
  -Method "tools/list" `
  -Params @{} `
  -TimeoutSec $TimeoutSec
$tools = @($toolsList.result.tools)
Write-Host "tools/list: ok ($($tools.Count) tools)" -ForegroundColor Green
if ($RawJson) {
  Show-JsonSnippet -Title "tools:" -Value $toolsList.result
} elseif ($tools.Count -gt 0) {
  $toolNames = @($tools | ForEach-Object { $_.name }) -join ", "
  Write-Host "  tools: $toolNames"
}

$status = Invoke-McpTool -Url $Url -Token $Token -Name "tb_status" -Arguments @{} -TimeoutSec $TimeoutSec
$statusOk = Show-ToolResult -Name "tb_status" -Response $status -RawJson:$RawJson
if (-not $statusOk) {
  $hadToolError = $true
}

$doctor = Invoke-McpTool -Url $Url -Token $Token -Name "tb_doctor" -Arguments @{} -TimeoutSec $TimeoutSec
$doctorOk = Show-ToolResult -Name "tb_doctor" -Response $doctor -RawJson:$RawJson
if (-not $doctorOk) {
  $hadToolError = $true
}

if ($Overlay) {
  $overlayArgs = [ordered] @{
    pointMarkers = @(
      [ordered] @{
        position = @(0, 0, 64)
        label = "MCP origin"
      }
    )
    boundsMarkers = @(
      [ordered] @{
        min = @(-64, -64, 0)
        max = @(64, 64, 128)
        label = "MCP smoke bounds"
      }
    )
    labels = @(
      [ordered] @{
        position = @(0, 0, 144)
        text = "MCP smoke"
      }
    )
  }

  $overlayResult = Invoke-McpTool -Url $Url -Token $Token -Name "overlay_set" -Arguments $overlayArgs -TimeoutSec $TimeoutSec
  $overlayOk = Show-ToolResult -Name "overlay_set" -Response $overlayResult -RawJson:$RawJson
  if (-not $overlayOk) {
    $hadToolError = $true
  }
}

if ($Capture -ne "None") {
  $captureTool = switch ($Capture) {
    "Current" { "viewport_capture_current" }
    "3D" { "viewport_capture_3d" }
    "2D" { "viewport_capture_2d" }
  }

  $captureResult = Invoke-McpTool -Url $Url -Token $Token -Name $captureTool -Arguments ([ordered] @{ returnBase64 = $false }) -TimeoutSec $TimeoutSec
  $captureOk = Show-ToolResult -Name $captureTool -Response $captureResult -RawJson:$RawJson
  if (-not $captureOk) {
    $hadToolError = $true
  } else {
    $structured = Get-PropertyValue $captureResult.result "structuredContent"
    $path = Get-PropertyValue $structured "path"
    if (-not [string]::IsNullOrWhiteSpace($path)) {
      Write-Host "  capture: $path"
    }
  }
}

if ($ClearOverlay) {
  $clearResult = Invoke-McpTool -Url $Url -Token $Token -Name "overlay_clear" -Arguments @{} -TimeoutSec $TimeoutSec
  $clearOk = Show-ToolResult -Name "overlay_clear" -Response $clearResult -RawJson:$RawJson
  if (-not $clearOk) {
    $hadToolError = $true
  }
}

if ($hadToolError) {
  Write-Host ""
  Write-Host "Smoke finished with MCP tool errors. If this reports Forbidden, set mode to ReadOnly or Edit in TrenchBroom Preferences > MCP." -ForegroundColor Yellow
  exit 2
}

Write-Host ""
Write-Host "MCP HTTP smoke completed." -ForegroundColor Green
