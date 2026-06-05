param(
  [string] $BuildDir = "build-release-codex",
  [string] $Target = "TrenchBroom",
  [string] $Config = "Release",
  [string] $TestFilter = "",
  [string] $VsDevCmd = "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
  [switch] $NoVsDevCmd,
  [switch] $NoFilter
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

function Should-ShowLine {
  param([string] $Line)

  if ($NoFilter) {
    return $true
  }

  if ([string]::IsNullOrWhiteSpace($Line)) {
    return $false
  }

  $looksLikeLocalizedIncludeTrace =
    ($Line -notmatch '(?i)\b(error|warning|fatal|failed)\b') -and
    ($Line -match '^\S.{0,16}:\s+') -and
    ($Line -match '(?i)(\\include\\|/include/|\\lib\\|/lib/)') -and
    ($Line -notmatch '^[A-Za-z]:\\')
  if ($looksLikeLocalizedIncludeTrace) {
    return $false
  }

  $noisePatterns = @(
    '^Note: including file:',
    '^Adding local dependency',
    '^Adding in plugin type ',
    '^Adding Qt6',
    '^Skipping plugin ',
    '^Direct dependencies:',
    '^All dependencies\s+:',
    '^To be deployed\s+:',
    '^Qt6.*\.dll is up to date\.$',
    '^opengl32sw\.dll is up to date\.$',
    '^d3dcompiler_47\.dll is up to date\.$',
    '^dxcompiler\.dll is up to date\.$',
    '^dxil\.dll is up to date\.$',
    '^q.*\.dll is up to date\.$',
    '^Creating qt_.*\.qm\.*$'
  )

  foreach ($pattern in $noisePatterns) {
    if ($Line -match $pattern) {
      return $false
    }
  }

  return $true
}

function Invoke-FilteredCommand {
  param(
    [string] $Title,
    [string] $Command,
    [string] $LogPath
  )

  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
  "" | Set-Content -Path $LogPath

  Write-Host "==> $Title"
  Write-Host "    log: $LogPath"

  $psi = [System.Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = "cmd.exe"
  $psi.Arguments = "/d /s /c `"$Command 2>&1`""
  $psi.WorkingDirectory = (Get-Location).Path
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $false
  $psi.CreateNoWindow = $true

  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $psi

  $shown = 0
  $hidden = 0
  $tail = [System.Collections.Generic.Queue[string]]::new()

  [void] $process.Start()
  while (-not $process.StandardOutput.EndOfStream) {
    $line = $process.StandardOutput.ReadLine()
    Add-Content -Path $LogPath -Value $line

    if ($tail.Count -ge 30) {
      [void] $tail.Dequeue()
    }
    $tail.Enqueue($line)

    if (Should-ShowLine $line) {
      Write-Host $line
      $shown++
    } else {
      $hidden++
    }
  }

  $process.WaitForExit()
  Write-Host "==> $Title finished with exit code $($process.ExitCode) ($shown shown, $hidden filtered)"

  if ($process.ExitCode -ne 0) {
    Write-Host ""
    Write-Host "Last log lines:"
    foreach ($line in $tail) {
      Write-Host $line
    }
    exit $process.ExitCode
  }
}

$resolvedBuildDir = Resolve-Path -Path $BuildDir -ErrorAction SilentlyContinue
if (-not $resolvedBuildDir) {
  throw "Build directory not found: $BuildDir"
}

$logDir = Join-Path $resolvedBuildDir "codex-logs"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

$cmakeArgs = @(
  "cmake",
  "--build", $resolvedBuildDir.Path,
  "--target", $Target,
  "--config", $Config,
  "--parallel"
)
$cmakeCommand = Join-CommandArgs $cmakeArgs

if (-not $NoVsDevCmd) {
  if (-not (Test-Path $VsDevCmd)) {
    throw "VsDevCmd not found: $VsDevCmd"
  }
  $cmakeCommand = "call `"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && $cmakeCommand"
}

Invoke-FilteredCommand `
  -Title "Build $Target ($Config)" `
  -Command $cmakeCommand `
  -LogPath (Join-Path $logDir "$stamp-build-$Target.log")

if ($TestFilter) {
  $testExe = Join-Path $resolvedBuildDir "lib\TbUiLib\test\TbUiLibTest.exe"
  if (-not (Test-Path $testExe)) {
    throw "Test executable not found: $testExe"
  }

  $testCommand = Join-CommandArgs @($testExe, $TestFilter)
  Invoke-FilteredCommand `
    -Title "Run TbUiLibTest $TestFilter" `
    -Command $testCommand `
    -LogPath (Join-Path $logDir "$stamp-test-$TestFilter.log")
}
