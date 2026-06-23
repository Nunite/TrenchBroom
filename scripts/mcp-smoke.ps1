param(
  [string] $BuildDir = "build-release-codex",
  [string] $McpExe = "",
  [ValidateSet("None", "Current", "3D", "2D")]
  [string] $Capture = "None",
  [int] $TimeoutMs = 5000,
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

function Read-JsonLine {
  param(
    [System.Diagnostics.Process] $Process,
    [int] $TimeoutMs
  )

  $readTask = $Process.StandardOutput.ReadLineAsync()
  if (-not $readTask.Wait($TimeoutMs)) {
    throw "Timed out waiting for trenchbroom-mcp response after $TimeoutMs ms"
  }

  $line = $readTask.Result
  if ([string]::IsNullOrWhiteSpace($line)) {
    throw "trenchbroom-mcp closed stdout without a response"
  }

  return $line | ConvertFrom-Json
}

function Invoke-McpRequest {
  param(
    [System.Diagnostics.Process] $Process,
    [string] $Method,
    [object] $Params,
    [int] $TimeoutMs
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

  $Process.StandardInput.WriteLine((ConvertTo-CompactJson $request))
  $Process.StandardInput.Flush()

  $response = Read-JsonLine -Process $Process -TimeoutMs $TimeoutMs
  $error = Get-PropertyValue $response "error"
  if ($null -ne $error) {
    throw "$Method failed: $($error.message)"
  }

  return $response
}

function Invoke-McpTool {
  param(
    [System.Diagnostics.Process] $Process,
    [string] $Name,
    [object] $Arguments,
    [int] $TimeoutMs
  )

  if ($null -eq $Arguments) {
    $Arguments = @{}
  }

  return Invoke-McpRequest `
    -Process $Process `
    -Method "tools/call" `
    -Params ([ordered] @{ name = $Name; arguments = $Arguments }) `
    -TimeoutMs $TimeoutMs
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

if ([string]::IsNullOrWhiteSpace($McpExe)) {
  $McpExe = Join-Path $BuildDir "app\TrenchBroomMcp\trenchbroom-mcp.exe"
}

if (-not (Test-Path $McpExe)) {
  throw "MCP executable not found: $McpExe. Build target trenchbroom-mcp first."
}

$resolvedMcpExe = (Resolve-Path $McpExe).Path
$configPath = Join-Path $env:APPDATA "TrenchBroom\MCP\config.json"

Write-Host "MCP executable: $resolvedMcpExe"
Write-Host "MCP config:     $configPath"
if (Test-Path $configPath) {
  try {
    $config = Get-Content -Path $configPath -Raw | ConvertFrom-Json
    Write-Host "Config mode:    $($config.mode)"
    Write-Host "Pipe name:      $($config.pipeName)"
  } catch {
    Write-Warning "Could not parse MCP config: $($_.Exception.Message)"
  }
} else {
  Write-Warning "MCP config does not exist yet. Run TrenchBroom once or enable MCP in preferences."
}

$process = $null
$script:NextRequestId = 0
$hadToolError = $false

try {
  $psi = [System.Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $resolvedMcpExe
  $psi.WorkingDirectory = Split-Path -Parent $resolvedMcpExe
  $psi.UseShellExecute = $false
  $psi.RedirectStandardInput = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.CreateNoWindow = $true

  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $psi
  [void] $process.Start()

  $initialize = Invoke-McpRequest `
    -Process $process `
    -Method "initialize" `
    -Params ([ordered] @{
      protocolVersion = "2025-06-18"
      capabilities = @{}
      clientInfo = [ordered] @{ name = "trenchbroom-mcp-smoke"; version = "0.1.0" }
    }) `
    -TimeoutMs $TimeoutMs
  Write-Host "initialize: ok" -ForegroundColor Green
  if ($RawJson) {
    Show-JsonSnippet -Title "result:" -Value $initialize.result
  }

  $toolsList = Invoke-McpRequest `
    -Process $process `
    -Method "tools/list" `
    -Params @{} `
    -TimeoutMs $TimeoutMs
  $tools = @($toolsList.result.tools)
  Write-Host "tools/list: ok ($($tools.Count) tools)" -ForegroundColor Green
  $configError = Get-PropertyValue $toolsList.result "configError"
  if (-not [string]::IsNullOrWhiteSpace($configError)) {
    Write-Warning "tools/list config error: $configError"
  }
  if ($RawJson) {
    Show-JsonSnippet -Title "tools:" -Value $toolsList.result
  } elseif ($tools.Count -gt 0) {
    $toolNames = @($tools | ForEach-Object { $_.name }) -join ", "
    Write-Host "  tools: $toolNames"
  }

  $status = Invoke-McpTool `
    -Process $process `
    -Name "tb_status" `
    -Arguments @{} `
    -TimeoutMs $TimeoutMs
  $statusOk = Show-ToolResult -Name "tb_status" -Response $status -RawJson:$RawJson
  if (-not $statusOk) {
    $hadToolError = $true
  }

  $doctor = Invoke-McpTool `
    -Process $process `
    -Name "tb_doctor" `
    -Arguments @{} `
    -TimeoutMs $TimeoutMs
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

    $overlayResult = Invoke-McpTool `
      -Process $process `
      -Name "overlay_set" `
      -Arguments $overlayArgs `
      -TimeoutMs $TimeoutMs
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

    $captureResult = Invoke-McpTool `
      -Process $process `
      -Name $captureTool `
      -Arguments ([ordered] @{ returnBase64 = $false }) `
      -TimeoutMs $TimeoutMs
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
    $clearResult = Invoke-McpTool `
      -Process $process `
      -Name "overlay_clear" `
      -Arguments @{} `
      -TimeoutMs $TimeoutMs
    $clearOk = Show-ToolResult -Name "overlay_clear" -Response $clearResult -RawJson:$RawJson
    if (-not $clearOk) {
      $hadToolError = $true
    }
  }
} finally {
  if ($null -ne $process) {
    if (-not $process.HasExited) {
      $process.StandardInput.Close()
      if (-not $process.WaitForExit(1000)) {
        $process.Kill()
        $process.WaitForExit()
      }
    }

    $stderr = $process.StandardError.ReadToEnd()
    if (-not [string]::IsNullOrWhiteSpace($stderr)) {
      Write-Warning "trenchbroom-mcp stderr: $stderr"
    }

    $process.Dispose()
  }
}

if ($hadToolError) {
  Write-Host ""
  Write-Host "Smoke finished with MCP tool errors. If this reports Forbidden, enable MCP and set mode to ReadOnly or Edit in TrenchBroom first." -ForegroundColor Yellow
  exit 2
}

Write-Host ""
Write-Host "MCP smoke completed." -ForegroundColor Green
