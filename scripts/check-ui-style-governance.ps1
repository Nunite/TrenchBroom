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

$styleSheetPath = Join-Path $repositoryRoot "app\TrenchBroom\resources\stylesheets\base.qss"
$foundationalSelectorPattern =
  'Q(?:LineEdit|ComboBox|SpinBox|DoubleSpinBox|CheckBox|RadioButton|Slider|PushButton|ToolButton)#[A-Za-z_]'
$selectorViolations = @(
  Select-String -LiteralPath $styleSheetPath -Pattern $foundationalSelectorPattern |
    ForEach-Object {
      $relativePath = $_.Path.Substring($repositoryRoot.Length + 1).Replace('\', '/')
      "$relativePath`:$($_.LineNumber):$($_.Line.Trim())"
    }
)

if ($selectorViolations.Count -gt 0) {
  throw @"
Found object-name selectors for foundational controls. Use the canonical default or an
approved tbControlRole; object names may identify compound surfaces, not restyle controls:
$($selectorViolations -join "`n")
"@
}

Write-Host (
  "UI style governance: no feature-local setStyleSheet() calls or foundational " +
  "object-name selectors")
