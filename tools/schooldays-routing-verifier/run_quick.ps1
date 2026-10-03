$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$outDir = Join-Path $repo "build\routing-verifier"
$exe = Join-Path $outDir "SchoolDaysRoutingQuick.exe"
$log = Join-Path $outDir "quick-result.txt"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2022 C++ tools."
}

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vs)) {
    throw "Visual Studio C++ toolchain not found."
}

$vsDevCmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
$test = Join-Path $repo "tests\SchoolDaysFullRouterTest.cpp"
$router = Join-Path $repo "src\SchoolDaysRouter.cpp"
$include = Join-Path $repo "include"

$compile = '"{0}" -no_logo -arch=x64 -host_arch=x64 && cl /nologo /std:c++17 /EHsc /I"{1}" "{2}" "{3}" /Fe:"{4}"' -f `
    $vsDevCmd, $include, $test, $router, $exe

Write-Host "=== Building School Days quick routing verifier ==="
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "C++ quick verifier build failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "=== Running 22 ending witnesses ==="
$output = & $exe 2>&1
$exitCode = $LASTEXITCODE
$output | Tee-Object -FilePath $log

if ($exitCode -ne 0) {
    throw "Quick verifier failed with exit code $exitCode"
}

$passCount = @($output | Select-String '^Ending\d{2} PASS').Count
if ($passCount -ne 22) {
    throw "Expected 22 PASS lines, got $passCount"
}

Write-Host ""
Write-Host "QUICK VERIFIER PASS: 22/22 ending witnesses"
Write-Host "Result log: $log"
