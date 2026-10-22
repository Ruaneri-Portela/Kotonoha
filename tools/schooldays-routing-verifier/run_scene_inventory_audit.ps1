$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script = Join-Path $PSScriptRoot "audit_scene_inventory.py"

Write-Host "=== School Days Scene Inventory Audit ==="

python $script --repo $repo
if ($LASTEXITCODE -ne 0) {
    throw "Scene inventory audit failed with exit code $LASTEXITCODE"
}
