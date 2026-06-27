param(
  [string] $TrenchBroomExe = "build-release-codex\app\TrenchBroom\TrenchBroom.exe",
  [string] $SourceMap = "build-release-codex\app\TrenchBroom\map_test\unnamed.map",
  [string] $WorkDir = "build-release-codex\codex-mcp-real-tests",
  [switch] $KeepOpen,
  [switch] $ReviewScenes
)

$ErrorActionPreference = "Stop"

function Invoke-McpTool {
  param(
    [string] $Tool,
    [object] $Arguments = @{},
    [int] $TimeoutSec = 10
  )

  $json = $Arguments | ConvertTo-Json -Depth 100 -Compress
  $raw = & (Join-Path $PSScriptRoot "mcp-call.ps1") `
    -Tool $Tool `
    -ArgumentsJson $json `
    -TimeoutSec $TimeoutSec `
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

function Wait-McpStatus {
  param(
    [int] $ExpectedProcessId,
    [string] $ExpectedDocumentPath = "",
    [int] $TimeoutMs = 20000
  )

  $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
  $lastStatus = $null
  do {
    $lastStatus = Invoke-McpTool -Tool "tb_status"
    $documentMatches = [string]::IsNullOrWhiteSpace($ExpectedDocumentPath) -or
      $lastStatus.activeDocumentPath -eq $ExpectedDocumentPath
    if ($lastStatus.processId -eq $ExpectedProcessId -and $documentMatches) {
      return $lastStatus
    }
    Start-Sleep -Milliseconds 500
  } while ((Get-Date) -lt $deadline)

  if ($lastStatus.processId -ne $ExpectedProcessId) {
    throw "tb_status.processId=$($lastStatus.processId) does not match launched PID $ExpectedProcessId"
  }
  if (-not [string]::IsNullOrWhiteSpace($ExpectedDocumentPath)) {
    throw "activeDocumentPath mismatch. Expected '$ExpectedDocumentPath', got '$($lastStatus.activeDocumentPath)'"
  }
  throw "MCP bridge did not become ready for PID $ExpectedProcessId"
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
      viewPlane = $Capture.viewPlane
      viewObjectName = $Capture.viewObjectName
    }
  } finally {
    if ($image) {
      $image.Dispose()
    }
  }
}

function Assert-ReviewQuality {
  param(
    [object] $Review,
    [string] $Name
  )

  if (-not $Review.reviewId) {
    throw "$Name did not return reviewId"
  }
  if ([int] $Review.targetObjectCount -le 0) {
    throw "$Name did not resolve any target objects"
  }
  if ([int] $Review.captureCount -le 0) {
    throw "$Name did not produce captures"
  }
  $qualityViews = @($Review.quality | ForEach-Object { [string] $_.view })
  foreach ($capture in @($Review.captures)) {
    if (-not $qualityViews.Contains([string] $capture.view)) {
      throw "$Name capture '$($capture.view)' is missing quality metadata"
    }
  }
  foreach ($quality in @($Review.quality)) {
    if (-not [bool] $quality.valid) {
      $warnings = @($quality.warnings) -join ", "
      throw "$Name capture '$($quality.view)' failed quality: $warnings"
    }
    if (($quality.view -like "*2d*") -and ([int] $quality.height -lt 360)) {
      throw "$Name capture '$($quality.view)' is too short: $($quality.height)"
    }
  }
}

function Invoke-ReviewBundle {
  param(
    [string] $Name,
    [string] $OperationId,
    [string[]] $Views = @("overview_3d", "detail_3d"),
    [string] $OutputRoot
  )

  $sceneOutputRoot = if ($OutputRoot) {
    New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot $Name)
  } else {
    $null
  }
  $reviewArgs = [ordered] @{
    sceneName = $Name
    operationIds = @($OperationId)
    views = $Views
    layout = "twoPanes"
    isolateMode = "hide_others"
    min2dHeight = 360
    returnBase64 = $false
  }
  if ($sceneOutputRoot) {
    $reviewArgs.outputDir = $sceneOutputRoot.FullName
  }
  $review = Invoke-McpTool -Tool "render_review_operation" -Arguments $reviewArgs -TimeoutSec 45
  $actualViews = @($review.captures | ForEach-Object { [string] $_.view })
  foreach ($view in $Views) {
    $normalized = if ($view -eq "overview_3d" -or $view -eq "detail_3d") { "3d" } else { $view }
    if (-not $actualViews.Contains($normalized)) {
      throw "$Name did not capture requested review view '$view'"
    }
  }
  $topCapture = @($review.captures | Where-Object { $_.view -eq "top_2d_fit" } | Select-Object -First 1)
  if ($topCapture.Count -gt 0 -and [string] $topCapture[0].viewPlane -ne "xy") {
    throw "$Name top_2d_fit used viewPlane '$($topCapture[0].viewPlane)', expected 'xy'"
  }
  $sideCapture = @($review.captures | Where-Object { $_.view -eq "side_2d_fit" } | Select-Object -First 1)
  if ($sideCapture.Count -gt 0 -and [string] $sideCapture[0].viewPlane -ne "xz") {
    throw "$Name side_2d_fit used viewPlane '$($sideCapture[0].viewPlane)', expected 'xz'"
  }
  Assert-ReviewQuality -Review $review -Name $Name
  return [ordered] @{
    name = $Name
    reviewId = $review.reviewId
    targetObjectCount = $review.targetObjectCount
    targetBounds = $review.targetBounds
    qualityValid = $review.qualityValid
    warnings = $review.warnings
    outputDir = $review.outputDir
    captures = @($review.captures | ForEach-Object { Get-CaptureSummary $_ })
    quality = $review.quality
  }
}

$resolvedExe = (Resolve-Path $TrenchBroomExe).Path
$resolvedSourceMap = (Resolve-Path $SourceMap).Path
$resolvedWorkDir = New-Item -ItemType Directory -Force -Path $WorkDir
$reviewOutputRoot = New-Item -ItemType Directory -Force -Path (Join-Path $resolvedWorkDir.FullName "reviews")
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
  $status = Wait-McpStatus -ExpectedProcessId $process.Id
  if ($status.activeDocumentPath -ne $testMap) {
    $open = Invoke-McpTool -Tool "documents_open_verified" -Arguments ([ordered] @{
      path = $testMap
      waitMs = 10000
      activate = $true
    }) -TimeoutSec 30
  }
  $status = Wait-McpStatus -ExpectedProcessId $process.Id -ExpectedDocumentPath $testMap

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

  $reviewSceneResults = @()
  if ($ReviewScenes) {
    $blockGroup = Invoke-McpTool -Tool "blockout_create_batch" -Arguments ([ordered] @{
      expectedDocumentPath = $testMap
      name = "review_block_group"
      detail = "ids"
      select = $true
      operations = @(
        [ordered] @{ type = "box"; min = @(-256, -256, 0); max = @(-96, -96, 96); material = "__TB_empty" },
        [ordered] @{ type = "box"; min = @(-64, -256, 0); max = @(96, -96, 160); material = "__TB_empty" },
        [ordered] @{ type = "box"; min = @(128, -256, 0); max = @(288, -96, 64); material = "__TB_empty" },
        [ordered] @{ type = "box"; min = @(-256, -64, 96); max = @(288, 32, 128); material = "__TB_empty" }
      )
    })
    $reviewSceneResults += Invoke-ReviewBundle -Name "review-block-group" -OperationId $blockGroup.operationId -OutputRoot $reviewOutputRoot.FullName
  }

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
    reviewScenes = $reviewSceneResults
    reviewOutputRoot = $reviewOutputRoot.FullName
    historyAfterSmoke = $history
  } | ConvertTo-Json -Depth 100
} finally {
  if (-not $KeepOpen) {
    $liveProcess = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if ($liveProcess) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
  }
}
