param(
  [string]$BlenderExe = "D:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$addonPath = Join-Path $repoRoot "tools\blender\tb_brush_sync_addon.py"
$wadPath = Join-Path $repoRoot "lib\TbMdlLib\test\fixture\mdl\LoadMipTexture\hl.wad"
if (!(Test-Path -LiteralPath $addonPath)) {
  throw "Missing Blender brush sync addon: $addonPath"
}
if (!(Test-Path -LiteralPath $wadPath)) {
  throw "Missing WAD fixture: $wadPath"
}
if (!(Test-Path -LiteralPath $BlenderExe)) {
  throw "Missing Blender executable: $BlenderExe"
}

$workDir = Join-Path $env:TEMP ("tb-blender-brush-sync-smoke-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $workDir | Out-Null
$requestPath = Join-Path $workDir "request.json"
$responsePath = Join-Path $workDir "response.json"
$driverPath = Join-Path $workDir "driver.py"
$successPath = Join-Path $workDir "success.txt"

$payload = @{
  schema = "tb.blenderBrushSync.v1"
  sessionId = "smoke-session"
  wadPaths = @($wadPath)
  brushes = @(
    @{
      id = "brush0"
      vertices = @(
        @(-16.0, -16.0, -16.0), @(16.0, -16.0, -16.0),
        @(16.0, 16.0, -16.0), @(-16.0, 16.0, -16.0),
        @(-16.0, -16.0, 16.0), @(16.0, -16.0, 16.0),
        @(16.0, 16.0, 16.0), @(-16.0, 16.0, 16.0)
      )
      faces = @(
        @{
          id = "face0"
          vertices = @(0, 1, 2, 3)
          material = "bongs2"
          loops = @(
            @{vertex = 0; uv = @(0.0, 0.0)},
            @{vertex = 1; uv = @(64.0, 0.0)},
            @{vertex = 2; uv = @(64.0, 64.0)},
            @{vertex = 3; uv = @(0.0, 64.0)}
          )
        }
      )
    }
  )
} | ConvertTo-Json -Depth 8
Set-Content -LiteralPath $requestPath -Value $payload -Encoding UTF8

@"
import importlib.util
import sys

import bpy

addon_path = r'''$addonPath'''
request_path = r'''$requestPath'''
response_path = r'''$responsePath'''
success_path = r'''$successPath'''

spec = importlib.util.spec_from_file_location("tb_brush_sync_addon", addon_path)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

count = module.import_request_file(request_path)
assert count == 1, count
obj = bpy.data.objects["TB Brush brush0"]
material = bpy.data.materials["bongs2"]
assert material.node_tree is not None, "WAD material should use nodes"
image_nodes = [
    node for node in material.node_tree.nodes
    if node.bl_idname == "ShaderNodeTexImage" and node.image
]
assert len(image_nodes) == 1, image_nodes
assert image_nodes[0].image.has_data, "WAD image was not loaded"
assert image_nodes[0].image.size[0] == 128, image_nodes[0].image.size[:]
assert image_nodes[0].image.size[1] == 128, image_nodes[0].image.size[:]
uv_layer = obj.data.uv_layers[module.UV_LAYER_NAME]
uvs = [tuple(uv_layer.data[loop_index].uv) for loop_index in obj.data.polygons[0].loop_indices]
assert uvs[1] == (0.5, 0.0), uvs
assert uvs[2] == (0.5, 0.5), uvs
mat = bpy.data.materials.new("new_sync")
obj.data.materials.clear()
obj.data.materials.append(mat)
obj.data.polygons[0].material_index = 0
uv_layer = obj.data.uv_layers[module.UV_LAYER_NAME]
for offset, loop_index in enumerate(obj.data.polygons[0].loop_indices):
    uv_layer.data[loop_index].uv = (float(offset) * 8.0, 32.0)

response = module.export_response(response_path)
assert len(response["faces"]) == 1, response
with open(success_path, "w", encoding="utf-8") as f:
    f.write("ok")
"@ | Set-Content -LiteralPath $driverPath -Encoding UTF8

& $BlenderExe --factory-startup --background --python $driverPath
if ($LASTEXITCODE -ne 0) {
  throw "Blender brush sync smoke failed with exit code $LASTEXITCODE"
}
if (!(Test-Path -LiteralPath $successPath)) {
  throw "Blender brush sync smoke did not complete its Python assertions"
}

$result = Get-Content -Raw -LiteralPath $responsePath | ConvertFrom-Json
if ($result.schema -ne "tb.blenderBrushSync.v1") {
  throw "Unexpected response schema: $($result.schema)"
}
if ($result.sessionId -ne "smoke-session") {
  throw "Unexpected session id: $($result.sessionId)"
}
if ($result.faces.Count -ne 1) {
  throw "Expected 1 output face, got $($result.faces.Count)"
}
$face = $result.faces[0]
if ($face.brushId -ne "brush0" -or $face.faceId -ne "face0") {
  throw "Unexpected face identity: $($face.brushId)/$($face.faceId)"
}
if ($face.material -ne "new_sync") {
  throw "Expected material new_sync, got $($face.material)"
}
if ($face.loops.Count -ne 4) {
  throw "Expected 4 loops, got $($face.loops.Count)"
}
if ($face.loops[0].vertex -ne 0) {
  throw "Expected first loop vertex id 0, got $($face.loops[0].vertex)"
}

Write-Host "TB Blender brush sync smoke passed: $($result.faces.Count) face"
