param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$validator = Join-Path $repo "tools\ktrf\validate_ir.py"
$example = Join-Path $repo "examples\ktrf\minimal-routing-ir.json"
$tests = Join-Path $repo "tests\ktrf\test_validate_ir.py"

function Run-Native {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Args
    )

    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

Write-Host "=== KTRF v0.1 validation ==="
Write-Host ""

Write-Host "[1/3] Python syntax"
Run-Native $Python -m py_compile $validator $tests

Write-Host ""
Write-Host "[2/3] Minimal Routing IR: Schema + semantic validation"
Run-Native $Python $validator $example

Write-Host ""
Write-Host "[3/3] Semantic validator regression tests"
Run-Native $Python $tests

Write-Host ""
Write-Host "KTRF VALIDATION PASS"
