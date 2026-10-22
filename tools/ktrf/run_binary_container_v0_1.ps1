param()

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Invoke-Checked {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Args
    )
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

Write-Host "=== KTRF binary container v0.1 ==="
Write-Host ""

Push-Location $Root
try {
    Write-Host "[1/3] Python syntax"
    Invoke-Checked python -m py_compile `
        .\tools\ktrf\binary_container_v0_1.py `
        .\tools\ktrf\smoke_binary_container_v0_1.py `
        .\tests\ktrf\test_binary_container_v0_1.py

    Write-Host ""
    Write-Host "[2/3] Container + STRS regression tests"
    Invoke-Checked python .\tests\ktrf\test_binary_container_v0_1.py

    Write-Host ""
    Write-Host "[3/3] Deterministic smoke artifact"
    Invoke-Checked python .\tools\ktrf\smoke_binary_container_v0_1.py

    Write-Host ""
    Write-Host "=============================================="
    Write-Host " KTRF BINARY CONTAINER v0.1: PASS"
    Write-Host "=============================================="
}
finally {
    Pop-Location
}
