param(
  [string] $BuildDir = "build-release-codex",
  [string] $Config = "Release",
  [string] $Suite = "mcp",
  [int] $Iterations = 200,
  [int] $Warmup = 20,
  [string] $Json = "",
  [string] $Baseline = "",
  [double] $MaxRegressionPercent = 15.0,
  [string] $VsDevCmd = "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
  [switch] $NoBuild,
  [switch] $List
)

$ErrorActionPreference = "Stop"

function Join-CommandArgs {
  param([string[]] $Parts)

  return ($Parts | ForEach-Object {
      if ($_ -match '[\s"&|<>^]') {
        '"' + ($_ -replace '"', '\"') + '"'
      } else {
        $_
      }
    }) -join " "
}

$resolvedBuildDir = Resolve-Path -Path $BuildDir -ErrorAction SilentlyContinue
if (-not $resolvedBuildDir) {
  throw "Build directory not found: $BuildDir"
}

if ([string]::IsNullOrWhiteSpace($Json)) {
  $Json = Join-Path $resolvedBuildDir "perf\latest.json"
}

if (-not $NoBuild) {
  if (-not (Test-Path $VsDevCmd)) {
    throw "VsDevCmd not found: $VsDevCmd"
  }

  $cmakeArgs = @(
    "cmake",
    "--build", $resolvedBuildDir.Path,
    "--target", "TbPerf",
    "--config", $Config,
    "--parallel"
  )
  $buildCommand = "call `"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && $(Join-CommandArgs $cmakeArgs)"
  Write-Host "==> Build TbPerf ($Config)"
  cmd.exe /d /s /c $buildCommand
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

$perfExe = Join-Path $resolvedBuildDir "app\TbPerf\TbPerf.exe"
if (-not (Test-Path $perfExe)) {
  throw "TbPerf executable not found: $perfExe"
}

$perfArgs = @()
if ($List) {
  $perfArgs += "--list"
} else {
  if (-not [string]::IsNullOrWhiteSpace($Suite)) {
    $perfArgs += @("--suite", $Suite)
  }
  $perfArgs += @("--iterations", [string] $Iterations)
  $perfArgs += @("--warmup", [string] $Warmup)
  $perfArgs += @("--json", $Json)
  if (-not [string]::IsNullOrWhiteSpace($Baseline)) {
    $perfArgs += @("--baseline", $Baseline)
    $perfArgs += @("--max-regression-percent", [string] $MaxRegressionPercent)
  }
}

Write-Host "==> Run TbPerf"
& $perfExe @perfArgs
exit $LASTEXITCODE
