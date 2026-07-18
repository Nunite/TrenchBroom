param(
  [string] $TrenchBroomExe = "build-release-codex\app\TrenchBroom\TrenchBroom.exe",
  [string] $StdioExe = "build-release-codex\app\TrenchBroomMcp\trenchbroom-mcp.exe",
  [string] $SourceMap = "lib\TbUiLib\test\fixture\ui\MapDocument\emptyValveMap.map",
  [string] $WorkDir = "build-release-codex\codex-mcp-reliability-acceptance"
)

$ErrorActionPreference = "Stop"

function Assert-True {
  param([bool] $Condition, [string] $Message)
  if (-not $Condition) {
    throw $Message
  }
}

function ConvertTo-CompactJson {
  param([object] $Value)
  return $Value | ConvertTo-Json -Depth 100 -Compress
}

function Get-CrashLogs {
  param([string] $AcceptanceDir)

  $paths = @()
  $documentsDir = Join-Path $env:USERPROFILE "Documents"
  if (Test-Path $documentsDir) {
    $paths += Get-ChildItem $documentsDir -File -Filter "*crash*.txt" -ErrorAction SilentlyContinue
  }
  if (Test-Path $AcceptanceDir) {
    $paths += Get-ChildItem $AcceptanceDir -File -Filter "*crash*.txt" -Recurse -ErrorAction SilentlyContinue
  }
  return @($paths | ForEach-Object { $_.FullName } | Sort-Object -Unique)
}

function Get-HttpStatus {
  param([string] $Url, [string] $Authorization = "")

  $headers = @{
    Accept = "application/json, text/event-stream"
    "MCP-Protocol-Version" = "2025-06-18"
  }
  if (-not [string]::IsNullOrWhiteSpace($Authorization)) {
    $headers.Authorization = $Authorization
  }
  $body = ConvertTo-CompactJson ([ordered] @{
    jsonrpc = "2.0"
    id = "auth-check"
    method = "initialize"
    params = [ordered] @{
      protocolVersion = "2025-06-18"
      capabilities = @{}
      clientInfo = [ordered] @{ name = "mcp-reliability-acceptance"; version = "1" }
    }
  })

  try {
    $response = Invoke-WebRequest -Method Post -Uri $Url -Headers $headers `
      -ContentType "application/json" -Body $body -TimeoutSec 10
    return [int] $response.StatusCode
  } catch {
    if ($null -ne $_.Exception.Response) {
      return [int] $_.Exception.Response.StatusCode
    }
    throw
  }
}

function Invoke-McpRpc {
  param(
    [string] $Url,
    [string] $Token,
    [string] $Method,
    [object] $Params = @{},
    [int] $TimeoutSec = 30
  )

  $script:NextRequestId++
  $request = [ordered] @{
    jsonrpc = "2.0"
    id = $script:NextRequestId
    method = $Method
    params = $Params
  }
  return Invoke-RestMethod -Method Post -Uri $Url -Headers @{
    Accept = "application/json, text/event-stream"
    Authorization = "Bearer $Token"
    "MCP-Protocol-Version" = "2025-06-18"
  } -ContentType "application/json" -Body (ConvertTo-CompactJson $request) `
    -TimeoutSec $TimeoutSec
}

function Invoke-McpTool {
  param(
    [string] $Url,
    [string] $Token,
    [string] $Tool,
    [object] $Arguments = @{},
    [int] $TimeoutSec = 30,
    [switch] $AllowError
  )

  $response = Invoke-McpRpc -Url $Url -Token $Token -Method "tools/call" `
    -Params ([ordered] @{ name = $Tool; arguments = $Arguments }) -TimeoutSec $TimeoutSec
  if ($null -ne $response.error) {
    throw "$Tool JSON-RPC failure: $($response.error.message)"
  }
  $result = $response.result
  $structured = $result.structuredContent
  if ([bool] $result.isError -and -not $AllowError) {
    throw "$Tool failed: $($structured.message)"
  }
  return $structured
}

function Wait-McpStatus {
  param([string] $Url, [string] $Token, [int] $ProcessId, [int] $TimeoutSec = 30)

  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  do {
    try {
      $status = Invoke-McpTool -Url $Url -Token $Token -Tool "tb_status"
      if ([int] $status.processId -eq $ProcessId) {
        return $status
      }
    } catch {
    }
    Start-Sleep -Milliseconds 300
  } while ((Get-Date) -lt $deadline)
  throw "MCP bridge did not become ready for PID $ProcessId"
}

function Get-StateSnapshot {
  param([string] $Url, [string] $Token)

  $map = Invoke-McpTool -Url $Url -Token $Token -Tool "map_snapshot"
  $history = Invoke-McpTool -Url $Url -Token $Token -Tool "history_list"
  $modules = Invoke-McpTool -Url $Url -Token $Token -Tool "module_list" `
    -Arguments ([ordered] @{ includeStale = $true; idsMode = "count" })
  $status = Invoke-McpTool -Url $Url -Token $Token -Tool "tb_status"
  return [ordered] @{
    brushCount = [int] $map.brushCount
    entityCount = [int] $map.entityCount
    historyCount = [int] $history.count
    moduleCount = [int] $modules.count
    sessionCounts = $status.sessionState.counts
  }
}

function Assert-StateEqual {
  param([object] $Expected, [object] $Actual, [string] $Context)

  foreach ($name in @("brushCount", "entityCount", "historyCount", "moduleCount")) {
    Assert-True ($Expected.$name -eq $Actual.$name) `
      "$Context changed ${name}: expected $($Expected.$name), got $($Actual.$name)"
  }
  foreach ($name in @("operationRecords", "irPreviews", "objectRegistryRecords")) {
    Assert-True ($Expected.sessionCounts.$name -eq $Actual.sessionCounts.$name) `
      "$Context changed session $name"
  }
}

function Invoke-StdioLongTool {
  param(
    [string] $Executable,
    [string] $ExpectedDocumentPath,
    [string] $ExpectedDocumentFingerprint
  )

  $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $Executable
  $startInfo.UseShellExecute = $false
  $startInfo.RedirectStandardInput = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.CreateNoWindow = $true
  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $startInfo
  Assert-True ($process.Start()) "Could not start stdio MCP client"

  try {
    $initialize = [ordered] @{
      jsonrpc = "2.0"
      id = 1
      method = "initialize"
      params = [ordered] @{
        protocolVersion = "2025-06-18"
        capabilities = @{}
        clientInfo = [ordered] @{ name = "mcp-reliability-acceptance"; version = "1" }
      }
    }
    $process.StandardInput.WriteLine((ConvertTo-CompactJson $initialize))
    $process.StandardInput.Flush()
    $initializeResponse = $process.StandardOutput.ReadLine() | ConvertFrom-Json
    Assert-True ($null -eq $initializeResponse.error) "stdio initialize failed"

    $script = @'
import json
import time
time.sleep(6)
print(json.dumps({"operations": [{"type": "box", "min": [640, 0, 0], "max": [704, 64, 16]}]}))
'@
    $call = [ordered] @{
      jsonrpc = "2.0"
      id = 2
      method = "tools/call"
      params = [ordered] @{
        name = "python_generate_blockout"
        arguments = [ordered] @{
          name = "MCP: stdio long timeout acceptance"
          script = $script
          timeoutMs = 15000
          expectedDocumentPath = $ExpectedDocumentPath
          expectedDocumentFingerprint = $ExpectedDocumentFingerprint
          detail = "summary"
        }
      }
    }
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process.StandardInput.WriteLine((ConvertTo-CompactJson $call))
    $process.StandardInput.Flush()
    $callResponse = $process.StandardOutput.ReadLine() | ConvertFrom-Json
    $timer.Stop()
    Assert-True ($null -eq $callResponse.error) "stdio long tool returned JSON-RPC error"
    Assert-True (-not [bool] $callResponse.result.isError) `
      "stdio long tool failed: $($callResponse.result.structuredContent.message)"
    Assert-True ($timer.Elapsed.TotalSeconds -ge 5.5) `
      "stdio long tool returned after only $($timer.ElapsedMilliseconds) ms"
    return [ordered] @{
      elapsedMs = $timer.ElapsedMilliseconds
      operationId = $callResponse.result.structuredContent.operationId
    }
  } finally {
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(3000)) {
      $process.Kill($true)
    }
    $process.Dispose()
  }
}

$resolvedExe = (Resolve-Path $TrenchBroomExe).Path
$resolvedStdioExe = (Resolve-Path $StdioExe).Path
$resolvedSourceMap = (Resolve-Path $SourceMap).Path
$resolvedWorkDir = New-Item -ItemType Directory -Force -Path $WorkDir
$mapA = Join-Path $resolvedWorkDir.FullName "reliability-a.map"
$mapB = Join-Path $resolvedWorkDir.FullName "reliability-b.map"
$irPath = Join-Path $resolvedWorkDir.FullName "atomic.ir.json"
$tamperPath = Join-Path $resolvedWorkDir.FullName "tamper.ir.json"
Copy-Item -LiteralPath $resolvedSourceMap -Destination $mapA -Force
Copy-Item -LiteralPath $resolvedSourceMap -Destination $mapB -Force

$configPath = Join-Path $env:APPDATA "TrenchBroom\MCP\config.json"
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
Assert-True ($config.mode -eq "Edit") "MCP config mode must be Edit for acceptance"
$toolProfile = if ([string]::IsNullOrWhiteSpace($config.toolProfile)) { "Modeling" } else { [string] $config.toolProfile }
Assert-True ($toolProfile -eq "Modeling") `
  "MCP config toolProfile must be Modeling for the 47-tool acceptance baseline"
Assert-True (-not [string]::IsNullOrWhiteSpace($config.token)) "MCP config token is missing"
$url = "http://127.0.0.1:$($config.httpPort)/mcp"
$token = [string] $config.token
$script:NextRequestId = 0
$crashLogsBefore = Get-CrashLogs -AcceptanceDir $resolvedWorkDir.FullName

$process = Start-Process -FilePath $resolvedExe -ArgumentList $mapA `
  -WorkingDirectory (Split-Path $resolvedExe) -WindowStyle Hidden -PassThru

try {
  $status = Wait-McpStatus -Url $url -Token $token -ProcessId $process.Id
  if ($status.activeDocumentPath -ne $mapA) {
    Invoke-McpTool -Url $url -Token $token -Tool "documents_open_verified" `
      -Arguments ([ordered] @{ path = $mapA; activate = $true; waitMs = 10000 }) | Out-Null
    $status = Wait-McpStatus -Url $url -Token $token -ProcessId $process.Id
  }
  Assert-True ($status.activeDocumentPath -eq $mapA) "Disposable map A is not active"

  Assert-True ((Get-HttpStatus -Url $url) -eq 401) "Missing bearer token was not rejected"
  Assert-True ((Get-HttpStatus -Url $url -Authorization "Bearer wrong-token") -eq 401) `
    "Wrong bearer token was not rejected"
  Assert-True ((Get-HttpStatus -Url $url -Authorization "Bearer $token") -eq 200) `
    "Correct bearer token was rejected"
  $tools = Invoke-McpRpc -Url $url -Token $token -Method "tools/list"
  Assert-True (@($tools.result.tools).Count -eq 53) `
    "Modeling tools/list count changed: $(@($tools.result.tools).Count)"

  $problemBaseline = Invoke-McpTool -Url $url -Token $token -Tool "problems_check"
  Assert-True ($null -ne $problemBaseline.totalCount) "problems_check totalCount missing"
  Assert-True ($null -ne $problemBaseline.returnedCount) "problems_check returnedCount missing"
  Assert-True ($null -ne $problemBaseline.truncated) "problems_check truncated missing"

  $coarseCurveIr = [ordered] @{
    schemaVersion = 1
    moduleId = "reliability-curve-quality"
    operations = @([ordered] @{
      type = "arc_ramp"
      center = @(1600, 0, 0)
      radius = 256
      width = 96
      startAngle = 0
      turnDegrees = 90
      rise = 128
      segments = 4
      thickness = 16
    })
  }
  $balancedCurve = Invoke-McpTool -Url $url -Token $token -Tool "ir_compile_preview" `
    -Arguments ([ordered] @{ ir = $coarseCurveIr })
  Assert-True ($balancedCurve.qualityStatus -eq "warning") `
    "Balanced coarse curve did not warn"
  Assert-True ([bool] $balancedCurve.acceptancePassed) `
    "Balanced coarse curve unexpectedly failed acceptance"
  Assert-True ($balancedCurve.curveQuality.suggestedSegments -gt 4) `
    "Curve preview did not suggest more segments"
  $coarseCurveIr["qualityPolicy"] = [ordered] @{ intent = "smooth" }
  $smoothCurve = Invoke-McpTool -Url $url -Token $token -Tool "ir_compile_preview" `
    -Arguments ([ordered] @{ ir = $coarseCurveIr })
  Assert-True ($smoothCurve.qualityStatus -eq "failed") `
    "Smooth coarse curve did not fail quality"
  Assert-True (-not [bool] $smoothCurve.acceptancePassed) `
    "Smooth coarse curve unexpectedly passed acceptance"

  $fingerprintA = [string] $status.documentFingerprint
  $guard = [ordered] @{
    expectedDocumentPath = $mapA
    expectedDocumentFingerprint = $fingerprintA
  }
  $atomicIr = [ordered] @{
    schemaVersion = 1
    name = "MCP: atomic geometry and entity acceptance"
    moduleId = "reliability-atomic"
    defaultMetadata = [ordered] @{ moduleId = "reliability-atomic"; generatedBy = "mcp-reliability-acceptance" }
    select = $false
    selectEntities = $false
    operations = @([ordered] @{ type = "box"; min = @(0, 0, 0); max = @(128, 128, 16); metadata = [ordered] @{ part = "floor" } })
    entities = @([ordered] @{ classname = "light"; origin = @(64, 64, 96); properties = [ordered] @{ _light = "255 255 255 200" } })
  }
  Set-Content -LiteralPath $irPath -Value ($atomicIr | ConvertTo-Json -Depth 100)
  $baseline = Get-StateSnapshot -Url $url -Token $token
  $preview = Invoke-McpTool -Url $url -Token $token -Tool "ir_compile_preview_from_file" `
    -Arguments ([ordered] @{ path = $irPath; detail = "summary" })
  Assert-True ($preview.schemaVersion -eq 1) "IR preview did not normalize schemaVersion 1"
  $applyArgs = [ordered] @{
    path = $irPath
    previewId = $preview.previewId
    expectedDocumentPath = $mapA
    expectedDocumentFingerprint = $fingerprintA
    idsMode = "count"
  }
  $apply = Invoke-McpTool -Url $url -Token $token -Tool "ir_apply_from_file" `
    -Arguments $applyArgs -TimeoutSec 120
  Assert-True ([bool] $apply.atomic) "IR apply did not report atomic=true"
  Assert-True (-not [string]::IsNullOrWhiteSpace($apply.parentOperationId)) `
    "IR apply did not return parentOperationId"
  Assert-True (@($apply.childOperationIds).Count -eq 2) "IR apply did not return two child operations"
  $afterApply = Get-StateSnapshot -Url $url -Token $token
  Assert-True ($afterApply.brushCount -eq $baseline.brushCount + 1) "Atomic IR brush was not created"
  Assert-True ($afterApply.entityCount -eq $baseline.entityCount + 1) "Atomic IR entity was not created"
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null
  $afterUndo = Get-StateSnapshot -Url $url -Token $token
  Assert-True ($afterUndo.brushCount -eq $baseline.brushCount) "Atomic undo did not remove geometry"
  Assert-True ($afterUndo.entityCount -eq $baseline.entityCount) "Atomic undo did not remove entity"
  Invoke-McpTool -Url $url -Token $token -Tool "history_redo_mcp" | Out-Null
  $afterRedo = Get-StateSnapshot -Url $url -Token $token
  Assert-True ($afterRedo.brushCount -eq $afterApply.brushCount) "Atomic redo did not restore geometry"
  Assert-True ($afterRedo.entityCount -eq $afterApply.entityCount) "Atomic redo did not restore entity"
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null

  $replaceCreateIr = [ordered] @{
    schemaVersion = 1
    applyMode = "create"
    moduleId = "reliability-replace"
    defaultMetadata = [ordered] @{ moduleId = "reliability-replace" }
    select = $false
    operations = @([ordered] @{ type = "box"; min = @(0, 256, 0); max = @(64, 320, 16) })
  }
  Invoke-McpTool -Url $url -Token $token -Tool "ir_apply" -TimeoutSec 120 `
    -Arguments ([ordered] @{
      ir = $replaceCreateIr
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $fingerprintA
    }) | Out-Null
  $replaceInitial = Invoke-McpTool -Url $url -Token $token -Tool "module_inspect" `
    -Arguments ([ordered] @{ moduleId = "reliability-replace"; idsMode = "count" })
  Assert-True ($replaceInitial.moduleRevision -eq 1) "Replacement module did not start at revision 1"
  Assert-True ($replaceInitial.canonicalObjectCount -eq 1) "Replacement module initial count is wrong"

  $replacementIr = [ordered] @{
    schemaVersion = 1
    applyMode = "replace_module"
    moduleId = "reliability-replace"
    defaultMetadata = [ordered] @{ moduleId = "reliability-replace" }
    select = $false
    operations = @(
      [ordered] @{ type = "box"; min = @(0, 256, 0); max = @(96, 320, 16) },
      [ordered] @{ type = "box"; min = @(0, 336, 0); max = @(96, 352, 32) }
    )
  }
  $replacePreview = Invoke-McpTool -Url $url -Token $token -Tool "ir_compile_preview" `
    -Arguments ([ordered] @{ ir = $replacementIr })
  Assert-True ($replacePreview.targetModuleRevision -eq 1) `
    "Replacement preview returned the wrong target revision"
  $replaceApply = Invoke-McpTool -Url $url -Token $token -Tool "ir_apply" -TimeoutSec 120 `
    -Arguments ([ordered] @{
      ir = $replacementIr
      expectedIrHash = $replacePreview.irHash
      expectedTargetModuleRevision = $replacePreview.targetModuleRevision
      expectedTargetModuleContentHash = $replacePreview.targetModuleContentHash
      expectedTargetCanonicalObjectIds = @($replacePreview.targetCanonicalObjectIds)
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $fingerprintA
    })
  Assert-True ($replaceApply.moduleRevision -eq 2) "Replacement did not advance module revision"
  Assert-True ($replaceApply.removedCanonicalObjectCount -eq 1) `
    "Replacement removed the wrong canonical object count"
  Assert-True ($replaceApply.createdCanonicalObjectCount -eq 2) `
    "Replacement created the wrong canonical object count"
  $replaceAppliedState = Invoke-McpTool -Url $url -Token $token -Tool "module_inspect" `
    -Arguments ([ordered] @{ moduleId = "reliability-replace"; idsMode = "count" })
  Assert-True ($replaceApply.moduleContentHash -eq $replaceAppliedState.moduleContentHash) `
    "Replacement response/session hash mismatch: response $($replaceApply.moduleContentHash), session $($replaceAppliedState.moduleContentHash)"
  Assert-True ($replaceAppliedState.canonicalObjectCount -eq 2) `
    "Replacement session lost objects immediately after apply: canonical $($replaceAppliedState.canonicalObjectCount), stored $($replaceAppliedState.storedReferenceCount), bounds $(ConvertTo-CompactJson $replaceAppliedState.bounds)"
  $replacementHash = [string] $replaceAppliedState.moduleContentHash
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null
  $replaceUndone = Invoke-McpTool -Url $url -Token $token -Tool "module_inspect" `
    -Arguments ([ordered] @{ moduleId = "reliability-replace"; idsMode = "count" })
  Assert-True ($replaceUndone.moduleRevision -eq 1) "Replacement undo did not restore revision 1"
  Assert-True ($replaceUndone.moduleContentHash -eq $replaceInitial.moduleContentHash) `
    "Replacement undo did not restore the original module hash"
  $replaceRedoResult = Invoke-McpTool -Url $url -Token $token -Tool "history_redo_mcp"
  Assert-True ($replaceRedoResult.operation.operationId -eq $replaceApply.undoOperationId) `
    "Replacement redo selected $($replaceRedoResult.operation.operationId) instead of $($replaceApply.undoOperationId)"
  $replaceRedone = Invoke-McpTool -Url $url -Token $token -Tool "module_inspect" `
    -Arguments ([ordered] @{ moduleId = "reliability-replace"; idsMode = "count" })
  $replaceRedoMap = Invoke-McpTool -Url $url -Token $token -Tool "map_snapshot"
  Assert-True ($replaceRedone.moduleRevision -eq 2) "Replacement redo did not restore revision 2"
  Assert-True ($replaceRedone.moduleContentHash -eq $replacementHash) `
    "Replacement redo hash mismatch: expected $replacementHash, got $($replaceRedone.moduleContentHash), canonical $($replaceRedone.canonicalObjectCount), map brushes $($replaceRedoMap.brushCount), bounds $(ConvertTo-CompactJson $replaceRedone.bounds)"
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null

  $strictMaterialBaseline = Get-StateSnapshot -Url $url -Token $token
  $strictMaterial = Invoke-McpTool -Url $url -Token $token -Tool "ir_apply" -AllowError `
    -Arguments ([ordered] @{
      ir = [ordered] @{
        schemaVersion = 1
        moduleId = "reliability-missing-material"
        material = "missing/reliability_material"
        requireMaterialAvailable = $true
        operations = @([ordered] @{ type = "box"; min = @(512, 256, 0); max = @(576, 320, 16) })
      }
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $fingerprintA
    })
  Assert-True (-not [bool] $strictMaterial.mutatedDocument) `
    "Strict missing material mutated the map"
  $strictMaterialAfter = Get-StateSnapshot -Url $url -Token $token
  Assert-StateEqual -Expected $strictMaterialBaseline -Actual $strictMaterialAfter `
    -Context "Strict material preflight"

  $failureBaseline = Get-StateSnapshot -Url $url -Token $token
  $invalid = Invoke-McpTool -Url $url -Token $token -Tool "ir_apply" -AllowError `
    -Arguments ([ordered] @{
      ir = [ordered] @{
        schemaVersion = 1
        moduleId = "reliability-invalid"
        defaultMetadata = [ordered] @{ moduleId = "reliability-invalid" }
        select = $false
        operations = @([ordered] @{ type = "box"; min = @(256, 0, 0); max = @(320, 64, 16) })
        entities = @([ordered] @{ classname = "mcp_missing_entity_class"; origin = @(288, 32, 64) })
      }
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $fingerprintA
    }) -TimeoutSec 120
  Assert-True ($invalid.failureStage -eq "entities") "Invalid entity did not fail at entities stage"
  Assert-True (-not [bool] $invalid.mutatedDocument) "Invalid IR reported document mutation"
  Assert-True (-not [bool] $invalid.partialMutation) "Invalid IR reported partial mutation"
  $failureAfter = Get-StateSnapshot -Url $url -Token $token
  Assert-StateEqual -Expected $failureBaseline -Actual $failureAfter -Context "Atomic failure"

  $tamperIr = [ordered] @{
    schemaVersion = 1
    moduleId = "reliability-tamper"
    operations = @([ordered] @{ type = "box"; min = @(384, 0, 0); max = @(448, 64, 16) })
  }
  Set-Content -LiteralPath $tamperPath -Value ($tamperIr | ConvertTo-Json -Depth 100)
  $tamperPreview = Invoke-McpTool -Url $url -Token $token -Tool "ir_compile_preview_from_file" `
    -Arguments ([ordered] @{ path = $tamperPath })
  $tamperIr.operations[0]["max"] = @(512, 64, 16)
  Set-Content -LiteralPath $tamperPath -Value ($tamperIr | ConvertTo-Json -Depth 100)
  $tamperResult = Invoke-McpTool -Url $url -Token $token -Tool "ir_apply_from_file" -AllowError `
    -Arguments ([ordered] @{
      path = $tamperPath
      previewId = $tamperPreview.previewId
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $fingerprintA
    })
  Assert-True (-not [bool] $tamperResult.mutatedDocument) "Modified preview file mutated the map"

  Invoke-McpTool -Url $url -Token $token -Tool "documents_open_verified" `
    -Arguments ([ordered] @{ path = $mapB; activate = $true; waitMs = 10000 }) | Out-Null
  $statusB = Invoke-McpTool -Url $url -Token $token -Tool "tb_status"
  Assert-True ($statusB.activeDocumentPath -eq $mapB) "Disposable map B is not active"
  $wrongTarget = Invoke-McpTool -Url $url -Token $token -Tool "blockout_create_batch" -AllowError `
    -Arguments ([ordered] @{
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $fingerprintA
      operations = @([ordered] @{ type = "box"; min = @(0, 0, 0); max = @(64, 64, 16) })
    })
  Assert-True (-not [bool] $wrongTarget.mutatedDocument) "Wrong-document guard mutated map B"

  Invoke-McpTool -Url $url -Token $token -Tool "documents_open_verified" `
    -Arguments ([ordered] @{ path = $mapA; activate = $true; waitMs = 10000 }) | Out-Null
  $status = Invoke-McpTool -Url $url -Token $token -Tool "tb_status"
  $stdio = Invoke-StdioLongTool -Executable $resolvedStdioExe `
    -ExpectedDocumentPath $mapA -ExpectedDocumentFingerprint $status.documentFingerprint
  Assert-True ($stdio.elapsedMs -ge 5500) "stdio request returned before the controlled delay"
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null

  $reviewCreate = Invoke-McpTool -Url $url -Token $token -Tool "blockout_create_batch" `
    -Arguments ([ordered] @{
      expectedDocumentPath = $mapA
      expectedDocumentFingerprint = $status.documentFingerprint
      name = "MCP: review resource acceptance"
      select = $false
      operations = @(
        [ordered] @{ type = "box"; min = @(768, 0, 0); max = @(832, 64, 32) },
        [ordered] @{ type = "box"; min = @(832, 0, 0); max = @(896, 64, 32) }
      )
    })
  $reviewAll = Invoke-McpTool -Url $url -Token $token -Tool "render_review_operation" `
    -Arguments ([ordered] @{
      operationIds = @($reviewCreate.operationId)
      views = @("top_plan")
      edgeMode = "all"
      combineViews = $false
      detail = "full"
      imageSize = @(900, 650)
      outputDir = (Join-Path $resolvedWorkDir.FullName "review-all")
    }) -TimeoutSec 120
  $review = Invoke-McpTool -Url $url -Token $token -Tool "render_review_operation" `
    -Arguments ([ordered] @{
      operationIds = @($reviewCreate.operationId)
      views = @("top_plan")
      edgeMode = "silhouette"
      combineViews = $false
      detail = "full"
      imageSize = @(900, 650)
      outputDir = (Join-Path $resolvedWorkDir.FullName "review-silhouette")
    }) -TimeoutSec 120
  Assert-True ($review.edgeInterpretation -eq "silhouette") `
    "Silhouette review did not report silhouette interpretation"
  Assert-True (-not [bool] $review.internalBrushEdgesDrawn) `
    "Silhouette review reported internal Brush edges"
  Assert-True (-not [bool] $review.visualReviewRequired) `
    "Review incorrectly became an acceptance requirement"
  Assert-True ($review.captures[0].edgeDensity -lt $reviewAll.captures[0].edgeDensity) `
    "Silhouette review did not reduce edge density"
  Assert-True ([Math]::Abs($review.captures[0].targetCoverage - $reviewAll.captures[0].targetCoverage) -lt 0.02) `
    "Silhouette review changed target coverage too much"
  Assert-True (-not [string]::IsNullOrWhiteSpace($review.resourceUri)) "Review resource URI missing"
  $resource = Invoke-McpRpc -Url $url -Token $token -Method "resources/read" `
    -Params ([ordered] @{ uri = $review.resourceUri })
  Assert-True ($null -eq $resource.error) "Review resource read failed"
  Assert-True (@($resource.result.contents).Count -eq 1) "Review resource content missing"

  $firstReviewResourceUri = [string] $review.resourceUri
  $lastReviewResourceUri = $firstReviewResourceUri
  for ($i = 0; $i -lt 129; ++$i) {
    $evictionReview = Invoke-McpTool -Url $url -Token $token `
      -Tool "render_review_operation" -Arguments ([ordered] @{
        operationIds = @($reviewCreate.operationId)
        views = @("top_plan")
        imageSize = @(320, 240)
        outputDir = (Join-Path $resolvedWorkDir.FullName "review-eviction\$i")
      }) -TimeoutSec 120
    $lastReviewResourceUri = [string] $evictionReview.resourceUri
  }
  $evictedResource = Invoke-McpRpc -Url $url -Token $token -Method "resources/read" `
    -Params ([ordered] @{ uri = $firstReviewResourceUri })
  $evictedResourceJson = $evictedResource.result.contents[0].text | ConvertFrom-Json
  Assert-True ([bool] $evictedResourceJson.evicted) `
    "Oldest review resource did not return an eviction hint"
  Assert-True ($evictedResourceJson.recoveryAction -eq "regenerate_review") `
    "Evicted review resource returned the wrong recovery action"
  $retainedResource = Invoke-McpRpc -Url $url -Token $token -Method "resources/read" `
    -Params ([ordered] @{ uri = $lastReviewResourceUri })
  $retainedResourceJson = $retainedResource.result.contents[0].text | ConvertFrom-Json
  Assert-True (-not [bool] $retainedResourceJson.evicted) `
    "Newest review resource was unexpectedly evicted"
  Invoke-McpTool -Url $url -Token $token -Tool "history_undo_mcp" | Out-Null

  $crashLogsAfter = Get-CrashLogs -AcceptanceDir $resolvedWorkDir.FullName
  $newCrashLogs = @($crashLogsAfter | Where-Object { $crashLogsBefore -notcontains $_ })
  Assert-True ($newCrashLogs.Count -eq 0) "New crash log(s): $($newCrashLogs -join ', ')"

  [ordered] @{
    passed = $true
    processId = $process.Id
    toolsListCount = @($tools.result.tools).Count
    parentOperationId = $apply.parentOperationId
    childOperationCount = @($apply.childOperationIds).Count
    balancedQualityStatus = $balancedCurve.qualityStatus
    smoothQualityStatus = $smoothCurve.qualityStatus
    replacedModuleRevision = $replaceApply.moduleRevision
    silhouetteEdgeDensity = $review.captures[0].edgeDensity
    allEdgeDensity = $reviewAll.captures[0].edgeDensity
    stdioElapsedMs = $stdio.elapsedMs
    reviewResourceUri = $review.resourceUri
    evictedReviewResourceUri = $firstReviewResourceUri
    retainedReviewResourceUri = $lastReviewResourceUri
    crashLogsBefore = $crashLogsBefore.Count
    crashLogsAfter = $crashLogsAfter.Count
  } | ConvertTo-Json -Depth 20
} finally {
  $live = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
  if ($null -ne $live) {
    Stop-Process -Id $process.Id -Force
  }
}
