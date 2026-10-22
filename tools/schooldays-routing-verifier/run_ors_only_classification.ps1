$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script = Join-Path $PSScriptRoot "classify_ors_only.py"

Write-Host "=== School Days ORS-only Classification ==="
python $script --repo $repo

if ($LASTEXITCODE -ne 0) {
    throw "ORS-only classification failed with exit code $LASTEXITCODE"
}
