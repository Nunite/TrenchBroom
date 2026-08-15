param(
  [string] $BuildDir = "build-release-codex",
  [string] $QtBin = "D:\Qtx\6.11.1\msvc2022_64\bin",
  [string[]] $Targets = @("welcome", "workbench", "outliner", "supporting"),
  [string[]] $Themes = @("light", "dark"),
  [string[]] $ScaleFactors = @("1", "1.5", "2"),
  [string] $MapPath = "lib\TbMdlLib\test\fixture\mdl\Map\initialMap.map",
  [string] $OutputDir = "",
  [int] $TimeoutSeconds = 45,
  [switch] $SkipBuild
)

$ErrorActionPreference = "Stop"

function Assert-UiSnapshotMetadata {
  param(
    [pscustomobject] $Metadata,
    [string] $ExpectedTarget,
    [string] $ExpectedTheme,
    [string] $ExpectedScale,
    [string] $ImagePath
  )

  if ($Metadata.status -ne "ok") {
    throw "Snapshot manifest did not report success: $ImagePath"
  }
  if ($Metadata.target -ne $ExpectedTarget) {
    throw "Expected target '$ExpectedTarget', got '$($Metadata.target)': $ImagePath"
  }
  if ($Metadata.theme -ne $ExpectedTheme) {
    throw "Expected theme '$ExpectedTheme', got '$($Metadata.theme)': $ImagePath"
  }
  if ($Metadata.scaleFactor -ne $ExpectedScale) {
    throw "Expected scale '$ExpectedScale', got '$($Metadata.scaleFactor)': $ImagePath"
  }
  if ($Metadata.sampledColorCount -lt 2 -or $Metadata.luminanceRange -lt 8) {
    throw "Snapshot appears visually blank: $ImagePath"
  }
  if (-not $Metadata.fontSupportsBasicLatin -or [string]::IsNullOrWhiteSpace($Metadata.fontFamily)) {
    throw "Snapshot font cannot render required acceptance text: $ImagePath"
  }
  if ($Metadata.sha256 -notmatch '^[0-9a-f]{64}$') {
    throw "Snapshot manifest has an invalid SHA-256: $ImagePath"
  }
  $actualSha256 =
    (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($Metadata.sha256 -ne $actualSha256) {
    throw "Snapshot image does not match its manifest SHA-256: $ImagePath"
  }

  $scale = [double]::Parse($ExpectedScale, [Globalization.CultureInfo]::InvariantCulture)
  $expectedWidth = [math]::Round([double] $Metadata.logicalSize.width * $scale)
  $expectedHeight = [math]::Round([double] $Metadata.logicalSize.height * $scale)
  if ([math]::Abs([double] $Metadata.pixelSize.width - $expectedWidth) -gt 2) {
    throw "Snapshot width does not reflect scale factor ${ExpectedScale}: $ImagePath"
  }
  if ([math]::Abs([double] $Metadata.pixelSize.height - $expectedHeight) -gt 2) {
    throw "Snapshot height does not reflect scale factor ${ExpectedScale}: $ImagePath"
  }
  if ([math]::Abs([double] $Metadata.devicePixelRatio - $scale) -gt 0.05) {
    throw "Snapshot device pixel ratio does not reflect scale factor ${ExpectedScale}: $ImagePath"
  }
}

function New-UiSnapshotContactSheet {
  param(
    [object[]] $Runs,
    [int] $ColumnCount,
    [string] $Path
  )

  Add-Type -AssemblyName System.Drawing

  $padding = 12
  $labelHeight = 28
  $cellWidth = 700
  $cellHeight = 500
  $rowCount = [math]::Ceiling($Runs.Count / $ColumnCount)
  $canvasWidth = $padding + $ColumnCount * ($cellWidth + $padding)
  $canvasHeight = $padding + $rowCount * ($labelHeight + $cellHeight + $padding)
  $bitmap = [Drawing.Bitmap]::new(
    $canvasWidth,
    $canvasHeight,
    [Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $graphics = [Drawing.Graphics]::FromImage($bitmap)
  $font = [Drawing.Font]::new("Segoe UI", 11, [Drawing.FontStyle]::Bold)
  $labelBrush = [Drawing.SolidBrush]::new([Drawing.Color]::White)

  try {
    $graphics.Clear([Drawing.Color]::FromArgb(30, 30, 30))
    $graphics.InterpolationMode =
      [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    for ($index = 0; $index -lt $Runs.Count; ++$index) {
      $run = $Runs[$index]
      $column = $index % $ColumnCount
      $row = [math]::Floor($index / $ColumnCount)
      $x = $padding + $column * ($cellWidth + $padding)
      $y = $padding + $row * ($labelHeight + $cellHeight + $padding)
      $label = "$($run.Target)  $($run.Theme)  $($run.Scale)x  $($run.PixelSize)"
      $graphics.DrawString($label, $font, $labelBrush, $x, $y)

      $source = [Drawing.Image]::FromFile($run.Image)
      try {
        $imageScale = [math]::Min(
          [double] $cellWidth / $source.Width,
          [double] $cellHeight / $source.Height)
        $imageWidth = [math]::Round($source.Width * $imageScale)
        $imageHeight = [math]::Round($source.Height * $imageScale)
        $imageX = $x + [math]::Floor(($cellWidth - $imageWidth) / 2)
        $imageY = $y + $labelHeight + [math]::Floor(($cellHeight - $imageHeight) / 2)
        $destination = [Drawing.Rectangle]::new(
          $imageX, $imageY, $imageWidth, $imageHeight)
        $graphics.DrawImage($source, $destination)
      } finally {
        $source.Dispose()
      }
    }

    $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
  } finally {
    $labelBrush.Dispose()
    $font.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
  }
}

$normalizedTargets = @($Targets | ForEach-Object { $_.ToLowerInvariant() })
foreach ($target in $normalizedTargets) {
  if ($target -notin @("welcome", "workbench", "outliner", "supporting")) {
    throw "Unsupported target '$target'. Expected welcome, workbench, outliner, or supporting."
  }
}
foreach ($theme in $Themes) {
  if ($theme -notin @("light", "dark")) {
    throw "Unsupported theme '$theme'. Expected light or dark."
  }
}
foreach ($scaleFactor in $ScaleFactors) {
  $parsedScale = 0.0
  if (
    -not [double]::TryParse(
      $scaleFactor,
      [Globalization.NumberStyles]::Float,
      [Globalization.CultureInfo]::InvariantCulture,
      [ref] $parsedScale) -or
    $parsedScale -le 0.0
  ) {
    throw "Invalid scale factor '$scaleFactor'."
  }
}

if (-not $SkipBuild) {
  & (Join-Path $PSScriptRoot "build-filtered.ps1") `
    -BuildDir $BuildDir `
    -Target TrenchBroom `
    -QtBin $QtBin
}

$resolvedBuildDir = Resolve-Path -Path $BuildDir
$resolvedQtBin = Resolve-Path -Path $QtBin
$resolvedMapPath = if (
  $normalizedTargets -contains "workbench" -or
  $normalizedTargets -contains "outliner" -or
  $normalizedTargets -contains "supporting") {
  Resolve-Path -Path $MapPath
} else {
  $null
}
$executable = Join-Path $resolvedBuildDir.Path "app\TrenchBroom\TrenchBroom.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "TrenchBroom executable not found: $executable"
}

$platformPluginDir = Join-Path (Split-Path -Parent $executable) "platforms"
$windowsPlugin = Join-Path $platformPluginDir "qwindows.dll"
if (-not (Test-Path -LiteralPath $windowsPlugin -PathType Leaf)) {
  throw "Deployed Qt Windows platform plugin not found: $windowsPlugin"
}

$acceptanceRoot = if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  Join-Path $resolvedBuildDir.Path "codex-logs\ui-theme-acceptance"
} else {
  [IO.Path]::GetFullPath($OutputDir)
}
$runDirectory = Join-Path $acceptanceRoot (Get-Date -Format "yyyyMMdd-HHmmss-fff")
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

$results = @()
foreach ($target in $normalizedTargets) {
  foreach ($theme in $Themes) {
    foreach ($scaleFactor in $ScaleFactors) {
      $scaleName = $scaleFactor.Replace(".", "_")
      $baseName = "$target-$theme-$scaleName"
      $imagePath = Join-Path $runDirectory "$baseName.png"
      $manifestPath = Join-Path $runDirectory "$baseName.json"

      $processInfo = [Diagnostics.ProcessStartInfo]::new()
      $processInfo.FileName = $executable
      $processInfo.Arguments =
        "--ui-snapshot `"$imagePath`" --ui-snapshot-theme $theme"
      if ($target -eq "workbench" -or $target -eq "outliner" -or $target -eq "supporting") {
        if ($target -ne "workbench") {
          $processInfo.Arguments += " --ui-snapshot-page $target"
        }
        $processInfo.Arguments += " `"$($resolvedMapPath.Path)`""
      }
      $processInfo.WorkingDirectory = Split-Path -Parent $executable
      $processInfo.UseShellExecute = $false
      $processInfo.CreateNoWindow = $true
      $processInfo.RedirectStandardOutput = $true
      $processInfo.RedirectStandardError = $true
      $processInfo.EnvironmentVariables["PATH"] =
        "$($resolvedQtBin.Path);$($processInfo.EnvironmentVariables['PATH'])"
      $processInfo.EnvironmentVariables["QT_QPA_PLATFORM"] = "windows"
      $processInfo.EnvironmentVariables["QT_QPA_PLATFORM_PLUGIN_PATH"] = $platformPluginDir
      $processInfo.EnvironmentVariables["QT_AUTO_SCREEN_SCALE_FACTOR"] = "0"
      $processInfo.EnvironmentVariables["QT_SCALE_FACTOR"] = $scaleFactor

      Write-Host "==> Capture $target theme=$theme scale=$scaleFactor"
      $process = [Diagnostics.Process]::new()
      $process.StartInfo = $processInfo
      [void] $process.Start()
      if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "UI snapshot timed out after $TimeoutSeconds seconds: $imagePath"
      }

      $stdout = $process.StandardOutput.ReadToEnd()
      $stderr = $process.StandardError.ReadToEnd()
      if ($process.ExitCode -ne 0) {
        throw @"
UI snapshot failed with exit code $($process.ExitCode): $imagePath
stdout:
$stdout
stderr:
$stderr
"@
      }
      if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
        throw "UI snapshot image was not created: $imagePath"
      }
      if ((Get-Item -LiteralPath $imagePath).Length -lt 1024) {
        throw "UI snapshot image is unexpectedly small: $imagePath"
      }
      if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "UI snapshot manifest was not created: $manifestPath"
      }

      $metadata = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
      Assert-UiSnapshotMetadata `
        -Metadata $metadata `
        -ExpectedTarget $target `
        -ExpectedTheme $theme `
        -ExpectedScale $scaleFactor `
        -ImagePath $imagePath

      $results += [pscustomobject]@{
        Target = $target
        Theme = $theme
        Scale = $scaleFactor
        LogicalSize = "$($metadata.logicalSize.width)x$($metadata.logicalSize.height)"
        PixelSize = "$($metadata.pixelSize.width)x$($metadata.pixelSize.height)"
        Colors = $metadata.sampledColorCount
        LuminanceRange = $metadata.luminanceRange
        Image = $imagePath
        Manifest = $manifestPath
        Sha256 = $metadata.sha256
      }
    }
  }
}

$contactSheetPath = Join-Path $runDirectory "contact-sheet.png"
New-UiSnapshotContactSheet `
  -Runs $results `
  -ColumnCount $ScaleFactors.Count `
  -Path $contactSheetPath

$reportPath = Join-Path $runDirectory "report.json"
$reportMapPath = if ($null -eq $resolvedMapPath) { $null } else { $resolvedMapPath.Path }
@{
  status = "ok"
  executable = $executable
  qtBin = $resolvedQtBin.Path
  mapPath = $reportMapPath
  contactSheet = $contactSheetPath
  runs = $results
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Host ""
$results | Format-Table Target, Theme, Scale, LogicalSize, PixelSize, Colors, LuminanceRange
Write-Host "Contact sheet: $contactSheetPath"
Write-Host "UI theme acceptance passed: $reportPath"
