param(
  [string]$BlenderExe = "D:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$processor = Join-Path $repoRoot "tools\blender\tb_uv_bridge.py"
if (!(Test-Path -LiteralPath $processor)) {
  throw "Missing Blender UV bridge processor: $processor"
}
if (!(Test-Path -LiteralPath $BlenderExe)) {
  throw "Missing Blender executable: $BlenderExe"
}

$workDir = Join-Path $env:TEMP ("tb-blender-uv-smoke-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $workDir | Out-Null
$inputPath = Join-Path $workDir "input.json"
$outputPath = Join-Path $workDir "output.json"

$payload = @{
  vertices = @(
    @(-24.0, 0.0, 16.0), @(24.0, 0.0, 20.0),
    @(8.9, 48.0, 15.0), @(56.9, 48.0, 19.0),
    @(37.8, 96.0, 12.2), @(85.8, 96.0, 16.2),
    @(57.1, 144.0, 8.0), @(105.1, 144.0, 12.0)
  )
  triangles = @(
    @{ id = "tri0"; vertices = @(0, 2, 1) },
    @{ id = "tri1"; vertices = @(1, 2, 3) },
    @{ id = "tri2"; vertices = @(2, 4, 3) },
    @{ id = "tri3"; vertices = @(3, 4, 5) },
    @{ id = "tri4"; vertices = @(4, 6, 5) },
    @{ id = "tri5"; vertices = @(5, 6, 7) }
  )
  activeQuad = @{
    corners = @(0, 2, 3, 1)
    uvs = @(
      @(0.0, 0.0),
      @(0.0, 64.0),
      @(128.0, 64.0),
      @(128.0, 0.0)
    )
  }
} | ConvertTo-Json -Depth 8
Set-Content -LiteralPath $inputPath -Value $payload -Encoding UTF8

& $BlenderExe --factory-startup --background --python $processor -- $inputPath $outputPath
if ($LASTEXITCODE -ne 0) {
  throw "Blender UV bridge smoke failed with exit code $LASTEXITCODE"
}

$result = Get-Content -Raw -LiteralPath $outputPath | ConvertFrom-Json
if (!$result.ok) {
  throw "Blender UV bridge returned failure: $($result.error)"
}
if ($result.triangles.Count -ne 6) {
  throw "Expected 6 output triangles, got $($result.triangles.Count)"
}
if ($result.mismatchCount -ne 0) {
  throw "Expected continuous shared edge UVs, got $($result.mismatchCount) mismatches"
}

Write-Host "TB Blender UV bridge smoke passed: $($result.triangles.Count) triangles, $($result.mismatchCount) mismatches"
