param()

$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceRoots = @(
  (Join-Path $repositoryRoot "app\TrenchBroom\src"),
  (Join-Path $repositoryRoot "lib\TbUiLib\src")
)
$allowedFiles = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
[void] $allowedFiles.Add("app/TrenchBroom/src/ApplicationStyle.cpp")
[void] $allowedFiles.Add("lib/TbUiLib/src/ColorButton.cpp")

$violations = @()
foreach ($sourceRoot in $sourceRoots) {
  foreach ($match in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Include *.cpp,*.h |
      Select-String -Pattern 'setStyleSheet\s*\(') {
    $relativePath = $match.Path.Substring($repositoryRoot.Length + 1).Replace('\', '/')
    if (-not $allowedFiles.Contains($relativePath)) {
      $violations += "$relativePath`:$($match.LineNumber):$($match.Line.Trim())"
    }
  }
}

if ($violations.Count -gt 0) {
  throw @"
Found feature-local setStyleSheet() calls. Move reusable appearance to ThemeTokens,
ApplicationStyle, or base.qss. Runtime data visualization requires an explicit allowlist entry:
$($violations -join "`n")
"@
}

Write-Host "UI style governance: no feature-local setStyleSheet() calls"
