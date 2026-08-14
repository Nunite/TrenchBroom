[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "Medium")]
param(
  [Parameter(Mandatory = $true)]
  [ValidateNotNullOrEmpty()]
  [string] $ObjectPath,
  [string] $BuildDir = "build-release-codex",
  [string] $Target = ""
)

$ErrorActionPreference = "Stop"

function Test-PathWithinRoot {
  param(
    [string] $Path,
    [string] $Root
  )

  $comparison = [System.StringComparison]::OrdinalIgnoreCase
  $rootWithSeparator = $Root.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
  ) + [System.IO.Path]::DirectorySeparatorChar

  return $Path.Equals($Root, $comparison) -or $Path.StartsWith($rootWithSeparator, $comparison)
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
  [System.IO.Path]::GetFullPath($BuildDir)
} else {
  [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDir))
}

if (-not (Test-PathWithinRoot -Path $buildRoot -Root $repositoryRoot)) {
  throw "Build directory must be inside the repository: $buildRoot"
}
if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
  throw "Build directory not found: $buildRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $buildRoot "CMakeCache.txt") -PathType Leaf)) {
  throw "Directory is not a configured CMake build tree: $buildRoot"
}

if ([System.IO.Path]::IsPathRooted($ObjectPath)) {
  throw "ObjectPath must be relative to the build directory: $ObjectPath"
}
if ($ObjectPath.IndexOfAny([char[]] "*?[]") -ge 0) {
  throw "ObjectPath must not contain wildcard characters: $ObjectPath"
}

$objectFullPath = [System.IO.Path]::GetFullPath((Join-Path $buildRoot $ObjectPath))
if (-not (Test-PathWithinRoot -Path $objectFullPath -Root $buildRoot)) {
  throw "Object path escapes the build directory: $objectFullPath"
}
if ([System.IO.Path]::GetExtension($objectFullPath) -ine ".obj") {
  throw "Only individual .obj files can be removed: $objectFullPath"
}
if (-not (Test-Path -LiteralPath $objectFullPath -PathType Leaf)) {
  throw "Object file not found: $objectFullPath"
}

$objectItem = Get-Item -LiteralPath $objectFullPath -Force
if (($objectItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
  throw "Refusing to remove a reparse point: $objectFullPath"
}

if (-not $PSCmdlet.ShouldProcess($objectFullPath, "Remove stale object file")) {
  return
}

Remove-Item -LiteralPath $objectFullPath -Force
if (Test-Path -LiteralPath $objectFullPath) {
  throw "Object file still exists after removal: $objectFullPath"
}
Write-Host "Removed stale object: $objectFullPath"

if (-not [string]::IsNullOrWhiteSpace($Target)) {
  $buildScript = Join-Path $PSScriptRoot "build-filtered.ps1"
  & $buildScript -BuildDir $buildRoot -Target $Target
}
