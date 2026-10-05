param(
    [string]$BuildDir = "build-win"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cert = Join-Path $repo "docs\school-days-routing-validation\certified"

New-Item -ItemType Directory -Force -Path $cert | Out-Null

Push-Location $repo

try {
    Write-Host ""
    Write-Host "=== 1/6 git diff --check ==="

    $diffLog = Join-Path $cert "GIT_DIFF_CHECK_CERTIFIED.txt"

    # PowerShell 5.1 converts native stderr output into ErrorRecord objects.
    # Git may legitimately emit CRLF/LF conversion warnings on stderr even
    # when `git diff --check` succeeds. Run through cmd.exe so the audit is
    # governed by Git's real process exit code rather than stderr text.
    $diffOutput = & $env:ComSpec /d /s /c `
        "git -c core.safecrlf=false diff --check 2>&1"
    $diffExit = $LASTEXITCODE

    $diffOutput | Set-Content $diffLog -Encoding UTF8

    if ($diffExit -ne 0) {
        throw "git diff --check failed with exit code $diffExit"
    }

    Add-Content $diffLog "GIT_DIFF_CHECK=PASS"


    Write-Host ""
    Write-Host "=== 2/6 build ==="

    $buildLog = Join-Path $cert "BUILD_ROUTING_CERTIFIED.txt"

    & cmake --build $BuildDir --config Debug 2>&1 |
        Tee-Object -FilePath $buildLog

    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }


    Write-Host ""
    Write-Host "=== 3/6 CTest ==="

    $ctestLog = Join-Path $cert "CTEST_ROUTING_CERTIFIED.txt"

    & ctest `
        --test-dir $BuildDir `
        -C Debug `
        --output-on-failure 2>&1 |
        Tee-Object -FilePath $ctestLog

    if ($LASTEXITCODE -ne 0) {
        throw "CTest failed"
    }


    Write-Host ""
    Write-Host "=== 4/6 22-ending deterministic witness suite ==="

    $quickLog = Join-Path $cert "QUICK_22_ENDINGS_CERTIFIED.txt"

    & powershell `
        -ExecutionPolicy Bypass `
        -File ".\tools\schooldays-routing-verifier\run_quick.ps1" 2>&1 |
        Tee-Object -FilePath $quickLog

    if ($LASTEXITCODE -ne 0) {
        throw "22-ending quick verifier failed"
    }


    Write-Host ""
    Write-Host "=== 5/6 terminal/callback structural audit ==="

    $callbackLog = Join-Path $cert "TERMINAL_CALLBACK_AUDIT_CERTIFIED.txt"

    & python `
        ".\tools\schooldays-routing-verifier\audit_terminal_callbacks.py" 2>&1 |
        Tee-Object -FilePath $callbackLog

    if ($LASTEXITCODE -ne 0) {
        throw "Terminal callback audit failed"
    }


    Write-Host ""
    Write-Host "=== 6/6 structural freeze ==="

    & python `
        ".\tools\schooldays-routing-verifier\freeze_route_model.py" `
        --root "." `
        --out "docs/school-days-routing-validation/certified"

    if ($LASTEXITCODE -ne 0) {
        throw "Freeze audit failed"
    }


    Write-Host ""
    Write-Host "======================================"
    Write-Host " SCHOOL DAYS ROUTING FREEZE: PASS"
    Write-Host "======================================"
    Write-Host ""
    Write-Host "Certified artifacts:"
    Write-Host $cert
}
finally {
    Pop-Location
}
