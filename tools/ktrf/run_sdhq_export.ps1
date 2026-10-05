param(
    [string]$Python = "python",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$exporter = Join-Path $repo "tools\ktrf\export_sdhq_to_ir.py"
$validator = Join-Path $repo "tools\ktrf\validate_ir.py"
$profileValidator = Join-Path $repo "tools\ktrf\validate_sdhq_ir.py"
$schema = Join-Path $repo "schemas\ktrf-routing-ir.schema.json"
$tests = Join-Path $repo "tests\ktrf\test_export_sdhq_ir.py"
$generated = Join-Path $repo "src\SchoolDaysRouteData.generated.inc"

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repo "build\ktrf\school-days-hq.routing.json"
}
elseif (![System.IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $repo $Output
}

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

Write-Host "=== School Days HQ -> KTRF Canonical Routing IR ==="
Write-Host ""

Write-Host "[1/5] Python syntax"
Run-Native $Python -m py_compile `
    $exporter `
    $validator `
    $profileValidator `
    $tests

Write-Host ""
Write-Host "[2/5] Export frozen oracle"
Run-Native $Python $exporter `
    --generated $generated `
    --output $Output

Write-Host ""
Write-Host "[3/5] Generic KTRF Schema + semantic validation"
Run-Native $Python $validator $Output --schema $schema

Write-Host ""
Write-Host "[4/5] School Days HQ profile conformance"
Run-Native $Python $profileValidator $Output

Write-Host ""
Write-Host "[5/5] Exporter regression tests"
Run-Native $Python $tests

Write-Host ""
Write-Host "=============================================="
Write-Host " SCHOOL DAYS HQ KTRF IR EXPORT: PASS"
Write-Host "=============================================="
Write-Host ""
Write-Host "Output: $Output"
