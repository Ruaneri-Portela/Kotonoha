$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script = Join-Path $PSScriptRoot "classify_ors_only_v2.py"

Write-Host "=== School Days ORS-only Classification v2 ==="
python $script --repo $repo
if ($LASTEXITCODE -ne 0) {
    throw "ORS-only classification v2 failed with exit code $LASTEXITCODE"
}
