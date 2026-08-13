param(
  [string] $BuildDir = "build-release-codex"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
if (-not (Test-Path -LiteralPath $buildPath -PathType Container)) {
  throw "Build directory does not exist: $buildPath"
}

$runningApp = Get-Process -Name "TrenchBroom" -ErrorAction SilentlyContinue
if ($runningApp) {
  throw "TrenchBroom is running. Close it before rebuilding: PID $($runningApp.Id -join ', ')"
}

# These translation units construct SweepTool or interpret SweepParameters directly.
# Updating their timestamps avoids stale class-layout objects without cleaning the build tree.
$sources = @(
  "lib\TbAppLib\src\SweepTool.cpp",
  "lib\TbAppLib\src\SweepToolUtils.cpp",
  "lib\TbAppLib\test\src\tst_SweepTool.cpp",
  "lib\TbAppLib\test\src\tst_SweepToolUtils.cpp",
  "lib\TbUiLib\src\MapViewToolBox.cpp",
  "lib\TbUiLib\src\SweepToolPage.cpp"
)

$timestamp = [DateTime]::UtcNow
foreach ($source in $sources) {
  $path = Join-Path $repoRoot $source
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Expected Sweep source does not exist: $path"
  }
  [System.IO.File]::SetLastWriteTimeUtc($path, $timestamp)
}

$buildScript = Join-Path $PSScriptRoot "build-filtered.ps1"
& $buildScript -BuildDir $BuildDir -Target "TbAppLibTest" -TestFilter "Sweep*"
if ($LASTEXITCODE -ne 0) {
  throw "Sweep tests failed with exit code $LASTEXITCODE"
}

& $buildScript -BuildDir $BuildDir -Target "TrenchBroom"
if ($LASTEXITCODE -ne 0) {
  throw "TrenchBroom build failed with exit code $LASTEXITCODE"
}
