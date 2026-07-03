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
$blendPath = Join-Path $workDir "scene.blend"
$outputPath = Join-Path $workDir "output.json"

$payload = @{
  vertices = @(
    @(-24.0, 0.0, 16.0), @(24.0, 0.0, 20.0),
    @(8.9, 48.0, 15.0), @(56.9, 48.0, 19.0),
    @(37.8, 96.0, 12.2), @(85.8, 96.0, 16.2),
    @(57.1, 144.0, 8.0), @(105.1, 144.0, 12.0)
  )
  triangles = @(
    @{ id = "tri0"; vertices = @(0, 2, 1); loops = @(@{vertex=0; uv=@(0, 0)}, @{vertex=2; uv=@(0, 64)}, @{vertex=1; uv=@(128, 0)}) },
    @{ id = "tri1"; vertices = @(1, 2, 3); loops = @(@{vertex=1; uv=@(128, 0)}, @{vertex=2; uv=@(0, 64)}, @{vertex=3; uv=@(128, 64)}) },
    @{ id = "tri2"; vertices = @(2, 4, 3); loops = @(@{vertex=2; uv=@(0, 64)}, @{vertex=4; uv=@(0, 128)}, @{vertex=3; uv=@(128, 64)}) },
    @{ id = "tri3"; vertices = @(3, 4, 5); loops = @(@{vertex=3; uv=@(128, 64)}, @{vertex=4; uv=@(0, 128)}, @{vertex=5; uv=@(128, 128)}) },
    @{ id = "tri4"; vertices = @(4, 6, 5); loops = @(@{vertex=4; uv=@(0, 128)}, @{vertex=6; uv=@(0, 192)}, @{vertex=5; uv=@(128, 128)}) },
    @{ id = "tri5"; vertices = @(5, 6, 7); loops = @(@{vertex=5; uv=@(128, 128)}, @{vertex=6; uv=@(0, 192)}, @{vertex=7; uv=@(128, 192)}) }
  )
} | ConvertTo-Json -Depth 8
Set-Content -LiteralPath $inputPath -Value $payload -Encoding UTF8

& $BlenderExe --factory-startup --background --python $processor -- export $inputPath $blendPath
if ($LASTEXITCODE -ne 0) {
  throw "Blender UV bridge export failed with exit code $LASTEXITCODE"
}
& $BlenderExe --factory-startup --background --python $processor -- read $blendPath $outputPath
if ($LASTEXITCODE -ne 0) {
  throw "Blender UV bridge read failed with exit code $LASTEXITCODE"
}

$result = Get-Content -Raw -LiteralPath $outputPath | ConvertFrom-Json
if (!$result.ok) {
  throw "Blender UV bridge returned failure: $($result.error)"
}
if ($result.triangles.Count -ne 6) {
  throw "Expected 6 output triangles, got $($result.triangles.Count)"
}
if ($result.triangles[0].loops.Count -ne 3) {
  throw "Expected triangle UV loops in output"
}

Write-Host "TB Blender UV bridge smoke passed: $($result.triangles.Count) triangles"
