param(
  [string] $Url = "",
  [string] $Token = "",
  [string] $TrenchBroomExe = "",
  [string] $MapPath = "",
  [int] $TimeoutSec = 5,
  [switch] $Launch,
  [switch] $RawJson,
  [switch] $KeepOpen
)

$ErrorActionPreference = "Stop"

function ConvertTo-CompactJson {
  param([object] $Value)

  return $Value | ConvertTo-Json -Depth 100 -Compress
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

function Invoke-McpRequest {
  param(
    [string] $Url,
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
    Accept = "application/json, text/event-stream"
    Authorization = "Bearer $script:McpToken"
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
    [string] $Name,
    [object] $Arguments,
    [int] $TimeoutSec
  )

  if ($null -eq $Arguments) {
    $Arguments = @{}
  }

  return Invoke-McpRequest `
    -Url $Url `
    -Method "tools/call" `
    -Params ([ordered] @{ name = $Name; arguments = $Arguments }) `
    -TimeoutSec $TimeoutSec
}

function Assert-True {
  param(
    [bool] $Condition,
    [string] $Message
  )

  if (-not $Condition) {
    throw $Message
  }
}

function Get-StructuredContent {
  param([object] $Response)

  $result = Get-PropertyValue $Response "result"
  $isError = [bool] (Get-PropertyValue $result "isError")
  $structured = Get-PropertyValue $result "structuredContent"
  if ($isError) {
    $message = Get-PropertyValue $structured "message"
    if ([string]::IsNullOrWhiteSpace($message)) {
      $message = (Get-PropertyValue ((Get-PropertyValue $result "content") | Select-Object -First 1) "text")
    }
    throw "Tool returned error: $message"
  }

  return $structured
}

$configPath = Join-Path $env:APPDATA "TrenchBroom\MCP\config.json"
if (Test-Path $configPath) {
  $config = Get-Content -Path $configPath -Raw | ConvertFrom-Json
  if ([string]::IsNullOrWhiteSpace($Url)) {
    $hostName = if ([string]::IsNullOrWhiteSpace($config.httpHost)) { "127.0.0.1" } else { $config.httpHost }
    $port = if ($null -eq $config.httpPort) { 37666 } else { $config.httpPort }
    $Url = "http://$hostName`:$port/mcp"
  }
} elseif ([string]::IsNullOrWhiteSpace($Url)) {
  throw "MCP config does not exist: $configPath. Start TrenchBroom once and enable MCP."
}

if ([string]::IsNullOrWhiteSpace($Token)) {
  $Token = $env:TB_MCP_TOKEN
}
if ([string]::IsNullOrWhiteSpace($Token) -and $null -ne $config) {
  $Token = $config.token
}
if ([string]::IsNullOrWhiteSpace($Token)) {
  throw "MCP bearer token is required. Pass -Token, set TB_MCP_TOKEN, or start TrenchBroom once to create the MCP config."
}
$script:McpToken = $Token

if ([string]::IsNullOrWhiteSpace($TrenchBroomExe)) {
  $TrenchBroomExe = Join-Path "build-release-codex" "app\TrenchBroom\TrenchBroom.exe"
}
if ([string]::IsNullOrWhiteSpace($MapPath)) {
  $MapPath = Join-Path "build-release-codex" "app\TrenchBroom\map_test\unnamed.map"
}

$launchedProcess = $null
try {
  if ($Launch) {
    $resolvedExe = (Resolve-Path $TrenchBroomExe).Path
    $resolvedMap = (Resolve-Path $MapPath).Path
    $launchedProcess = Start-Process `
      -FilePath $resolvedExe `
      -ArgumentList $resolvedMap `
      -WorkingDirectory (Split-Path $resolvedExe) `
      -WindowStyle Hidden `
      -PassThru
    Write-Host "Started TrenchBroom PID $($launchedProcess.Id)"
    Start-Sleep -Seconds 3
  }

  Write-Host "MCP HTTP URL: $Url"
  $script:NextRequestId = 0

  $initialize = Invoke-McpRequest `
    -Url $Url `
    -Method "initialize" `
    -Params ([ordered] @{
      protocolVersion = "2025-06-18"
      capabilities = @{}
      clientInfo = [ordered] @{ name = "trenchbroom-mcp-kz-smoke"; version = "0.1.0" }
    }) `
    -TimeoutSec $TimeoutSec
  Assert-True ($initialize.result.protocolVersion -eq "2025-06-18") "initialize returned an unexpected protocol version."
  Write-Host "initialize: ok" -ForegroundColor Green

  $toolsList = Invoke-McpRequest -Url $Url -Method "tools/list" -Params @{} -TimeoutSec $TimeoutSec
  $toolNames = @($toolsList.result.tools | ForEach-Object { $_.name })
  foreach ($requiredTool in @(
      "shape_library_list",
      "brush_create_polygon_batch",
      "brush_metadata_get",
      "selection_by_metadata",
      "kz_distance_analyze_chain",
      "history_undo_mcp")) {
    Assert-True ($toolNames -contains $requiredTool) "tools/list is missing $requiredTool."
  }
  Write-Host "tools/list: ok ($($toolNames.Count) tools)" -ForegroundColor Green

  $status = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "tb_status" -Arguments @{} -TimeoutSec $TimeoutSec)
  Assert-True ([bool] $status.activeDocument) "tb_status reports no active document."
  Assert-True ($status.mode -eq "Edit") "tb_status mode must be Edit for this smoke test."
  $modifiedBefore = [bool] $status.activeDocumentModified
  Write-Host "tb_status: ok ($($status.activeDocumentFileName), modified=$modifiedBefore)" -ForegroundColor Green

  $shapes = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "shape_library_list" -Arguments @{} -TimeoutSec $TimeoutSec)
  $shapeNames = @($shapes.shapes | ForEach-Object { $_.name })
  foreach ($shapeName in @("diamond", "trapezoid", "chamfered_rect", "half_hex", "arrowhead", "slanted_plank")) {
    Assert-True ($shapeNames -contains $shapeName) "shape_library_list is missing $shapeName."
  }
  Write-Host "shape_library_list: ok ($($shapeNames -join ', '))" -ForegroundColor Green

  $createArgs = [ordered] @{
    transactionName = "MCP: KZ smoke polygon platforms"
    grid = 16
    select = $true
    detail = "ids"
    brushes = @(
      [ordered] @{
        points2d = @(@(4096, 0), @(4128, -32), @(4160, 0), @(4128, 32))
        minZ = 0
        maxZ = 16
        metadata = [ordered] @{
          routeId = "kz_smoke"
          movementType = "bhop"
          intent = "takeoff"
          outgoingDirection = @(1, 0, 0)
        }
      },
      [ordered] @{
        points2d = @(@(4256, -32), @(4320, -32), @(4336, 0), @(4320, 32), @(4256, 32), @(4240, 0))
        minZ = 16
        maxZ = 32
        metadata = [ordered] @{
          routeId = "kz_smoke"
          movementType = "bhop"
          intent = "landing"
          incomingDirection = @(1, 0, 0)
        }
      }
    )
  }
  $created = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "brush_create_polygon_batch" -Arguments $createArgs -TimeoutSec $TimeoutSec)
  $objectIds = @($created.changedObjectIds)
  Assert-True ($created.validation.valid -eq $true) "brush_create_polygon_batch validation failed."
  Assert-True ($created.brushCount -eq 2) "brush_create_polygon_batch did not create 2 brushes."
  Assert-True ($created.metadataCount -eq 2) "brush_create_polygon_batch did not store 2 metadata records."
  Assert-True ($objectIds.Count -eq 2) "brush_create_polygon_batch did not return 2 object ids."
  Write-Host "brush_create_polygon_batch: ok (operation=$($created.operationId), ids=$($objectIds -join ', '))" -ForegroundColor Green

  $metadata = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "brush_metadata_get" -Arguments ([ordered] @{ objectIds = $objectIds }) -TimeoutSec $TimeoutSec)
  Assert-True ($metadata.count -eq 2) "brush_metadata_get did not return 2 records."
  Assert-True ($metadata.staleCount -eq 0) "brush_metadata_get returned stale records."
  Write-Host "brush_metadata_get: ok" -ForegroundColor Green

  $selection = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "selection_by_metadata" -Arguments ([ordered] @{ routeId = "kz_smoke"; select = $true; detail = "ids" }) -TimeoutSec $TimeoutSec)
  Assert-True ($selection.count -eq 2) "selection_by_metadata did not find 2 records."
  Assert-True ($selection.selected -eq $true) "selection_by_metadata did not select the records."
  Write-Host "selection_by_metadata: ok" -ForegroundColor Green

  $distance = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "kz_distance_analyze_chain" -Arguments ([ordered] @{ objectIds = $objectIds; movementType = "bhop"; playerHull = @(32, 32, 72) }) -TimeoutSec $TimeoutSec)
  Assert-True ($distance.segmentCount -eq 1) "kz_distance_analyze_chain did not return 1 segment."
  Assert-True ($distance.segments[0].usedMetadataDirection -eq $true) "kz_distance_analyze_chain did not use metadata direction."
  Assert-True ($distance.segments[0].edgeGap -gt 0) "kz_distance_analyze_chain returned a non-positive edgeGap."
  Write-Host "kz_distance_analyze_chain: ok (edgeGap=$($distance.segments[0].edgeGap))" -ForegroundColor Green

  $undo = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "history_undo_mcp" -Arguments @{} -TimeoutSec $TimeoutSec)
  Assert-True ($undo.undone -eq $true) "history_undo_mcp did not undo the operation."
  Assert-True ($undo.operation.operationId -eq $created.operationId) "history_undo_mcp undid a different operation."
  Write-Host "history_undo_mcp: ok (skippedSelectionCommands=$($undo.skippedSelectionCommands))" -ForegroundColor Green

  $validateAfterUndo = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "operation_validate" -Arguments ([ordered] @{ operationId = $created.operationId }) -TimeoutSec $TimeoutSec)
  Assert-True ($validateAfterUndo.undone -eq $true) "operation_validate does not report the operation as undone."
  Assert-True ($validateAfterUndo.liveObjectCount -eq 0) "operation_validate still sees live smoke brushes after undo."

  $statusAfterUndo = Get-StructuredContent (Invoke-McpTool -Url $Url -Name "tb_status" -Arguments @{} -TimeoutSec $TimeoutSec)
  Assert-True ([bool] $statusAfterUndo.activeDocument) "tb_status after undo reports no active document."
  Assert-True ([bool] $statusAfterUndo.activeDocumentModified -eq $modifiedBefore) "Map dirty state did not return to its pre-smoke value."

  if ($RawJson) {
    [ordered] @{
      operationId = $created.operationId
      objectIds = $objectIds
      distance = $distance
      undo = $undo
      validateAfterUndo = $validateAfterUndo
    } | ConvertTo-Json -Depth 100
  }

  Write-Host ""
  Write-Host "KZ MCP smoke completed." -ForegroundColor Green
} finally {
  if ($Launch -and -not $KeepOpen -and $null -ne $launchedProcess) {
    $process = Get-Process -Id $launchedProcess.Id -ErrorAction SilentlyContinue
    if ($null -ne $process) {
      Stop-Process -Id $launchedProcess.Id -Force
      Write-Host "Stopped TrenchBroom PID $($launchedProcess.Id)"
    }
  }
}
