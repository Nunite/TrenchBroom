param(
  [string] $TrenchBroomExe = "build-release-codex\app\TrenchBroom\TrenchBroom.exe",
  [string] $SourceMap = "build-release-codex\app\TrenchBroom\map_test\unnamed.map",
  [string] $WorkDir = "build-release-codex\codex-mcp-real-tests",
  [switch] $KeepOpen
)

$ErrorActionPreference = "Stop"

function Invoke-McpTool {
  param(
    [string] $Tool,
    [object] $Arguments = @{}
  )

  $json = $Arguments | ConvertTo-Json -Depth 100 -Compress
  $raw = & (Join-Path $PSScriptRoot "mcp-call.ps1") `
    -Tool $Tool `
    -ArgumentsJson $json `
    -RawStructured
  return $raw | ConvertFrom-Json
}

function New-SaddleHeightmap {
  param([string] $Path)

  Add-Type -AssemblyName System.Drawing
  $bitmap = New-Object System.Drawing.Bitmap 16, 16
  try {
    for ($y = 0; $y -lt 16; ++$y) {
      for ($x = 0; $x -lt 16; ++$x) {
        $nx = ($x - 7.5) / 7.5
        $ny = ($y - 7.5) / 7.5
        $value = [Math]::Round(128 + 80 * ($nx * $nx - $ny * $ny))
        $value = [Math]::Max(0, [Math]::Min(255, $value))
        $color = [System.Drawing.Color]::FromArgb($value, $value, $value)
        $bitmap.SetPixel($x, $y, $color)
      }
    }
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  } finally {
    $bitmap.Dispose()
  }
}

function Get-CaptureSummary {
  param([object] $Capture)

  $path = [string] $Capture.path
  $exists = Test-Path -LiteralPath $path
  $file = if ($exists) { Get-Item -LiteralPath $path } else { $null }
  $image = $null
  if ($exists) {
    try {
      Add-Type -AssemblyName System.Drawing
      $image = [System.Drawing.Image]::FromFile($path)
    } catch {
      $image = $null
    }
  }
  try {
    return [ordered] @{
      view = $Capture.view
      path = $path
      exists = $exists
      bytes = if ($file) { $file.Length } else { 0 }
      width = if ($image) { $image.Width } else { 0 }
      height = if ($image) { $image.Height } else { 0 }
    }
  } finally {
    if ($image) {
      $image.Dispose()
    }
  }
}

$resolvedExe = (Resolve-Path $TrenchBroomExe).Path
$resolvedSourceMap = (Resolve-Path $SourceMap).Path
$resolvedWorkDir = New-Item -ItemType Directory -Force -Path $WorkDir
$testMap = Join-Path $resolvedWorkDir.FullName "mcp-smoke-real.map"
$heightmapPath = Join-Path $resolvedWorkDir.FullName "mcp-smoke-saddle.png"
Copy-Item -LiteralPath $resolvedSourceMap -Destination $testMap -Force
New-SaddleHeightmap -Path $heightmapPath

$process = Start-Process `
  -FilePath $resolvedExe `
  -ArgumentList $testMap `
  -WorkingDirectory (Split-Path $resolvedExe) `
  -PassThru

try {
  Start-Sleep -Seconds 5
  $status = Invoke-McpTool -Tool "tb_status"
  if ($status.processId -ne $process.Id) {
    throw "tb_status.processId=$($status.processId) does not match launched PID $($process.Id)"
  }
  if ($status.activeDocumentPath -ne $testMap) {
    throw "activeDocumentPath mismatch. Expected '$testMap', got '$($status.activeDocumentPath)'"
  }

  $create = Invoke-McpTool -Tool "blockout_create_batch" -Arguments ([ordered] @{
    expectedDocumentPath = $testMap
    name = "mcp_smoke_scene_review"
    detail = "ids"
    select = $true
    operations = @(
      [ordered] @{ type = "box"; min = @(0, 0, 0); max = @(128, 128, 64); material = "__TB_empty" },
      [ordered] @{ type = "box"; min = @(160, 0, 0); max = @(288, 96, 48); material = "__TB_empty" },
      [ordered] @{ type = "box"; min = @(320, 32, 0); max = @(448, 160, 96); material = "__TB_empty" }
    )
  })

  $framingResults = @()
  foreach ($framing in @("overview_orbit", "top_fit", "side_profile")) {
    $review = Invoke-McpTool -Tool "viewport_capture_scene_review" -Arguments ([ordered] @{
      sceneName = "mcp-smoke-$framing"
      operationIds = @($create.operationId)
      framing = $framing
      layout = "fourPanes"
      views = @("current", "3d", "2d")
      highlight = $false
      clearSelectionBeforeCapture = $true
      returnBase64 = $false
    })
    $framingResults += [ordered] @{
      framing = $framing
      cameraControlled = $review.cameraControlled
      focusedObjectCount = $review.focusedObjectCount
      captureCount = $review.captureCount
      captures = @($review.captures | ForEach-Object { Get-CaptureSummary $_ })
    }
  }

  $heightmapArgs = [ordered] @{
    imagePath = $heightmapPath
    expectedDocumentPath = $testMap
    origin = @(1024, 0, 0)
    cellSize = 32
    heightScale = 128
    heightSteps = 16
    maxSize = 16
    maxBrushes = 400
    material = "__TB_empty"
    mode = "terraced_brushes"
    select = $true
    detail = "summary"
  }
  $preview = Invoke-McpTool -Tool "heightmap_preview_grayscale" -Arguments $heightmapArgs
  $import = Invoke-McpTool -Tool "heightmap_import_grayscale" -Arguments $heightmapArgs
  $validation = Invoke-McpTool -Tool "operation_validate" -Arguments ([ordered] @{
    operationId = $import.operationId
    detail = "summary"
  })
  $undo = Invoke-McpTool -Tool "history_undo_mcp"
  $history = Invoke-McpTool -Tool "history_status"

  [ordered] @{
    status = [ordered] @{
      processId = $status.processId
      activeDocumentPath = $status.activeDocumentPath
      documentFingerprint = $status.documentFingerprint
      documentEpoch = $status.documentEpoch
    }
    sceneReview = [ordered] @{
      operationId = $create.operationId
      brushCount = $create.brushCount
      framings = $framingResults
    }
    heightmap = [ordered] @{
      imagePath = $heightmapPath
      previewBrushCount = $preview.estimatedBrushCount
      importBrushCount = $import.brushCount
      previewOutputBounds = $preview.outputBounds
      importOutputBounds = $import.heightmap.outputBounds
      importTopLevelBounds = $import.bounds
      validation = $validation
      undo = $undo
    }
    historyAfterSmoke = $history
  } | ConvertTo-Json -Depth 100
} finally {
  if (-not $KeepOpen) {
    $liveProcess = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if ($liveProcess) {
      Stop-Process -Id $process.Id -Force
    }
  }
}
