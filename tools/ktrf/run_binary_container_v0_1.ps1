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
        .\tests\ktrf\test_binary_container_v0_1.py

    Write-Host ""
    Write-Host "[2/3] Container + STRS regression tests"
    Invoke-Checked python .\tests\ktrf\test_binary_container_v0_1.py

    Write-Host ""
    Write-Host "[3/3] Deterministic smoke artifact"
    $code = @'
import importlib.util, pathlib, sys
root = pathlib.Path.cwd()
p = root / "tools" / "ktrf" / "binary_container_v0_1.py"
spec = importlib.util.spec_from_file_location("ktrf_binary_smoke", p)
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)
strs, _ = mod.encode_strs(["Kotonoha", "KTRF", "overflow.school-days-hq"])
data = mod.build_container([
    mod.Section(b"META", b"binary-v0.1-smoke"),
    mod.Section(b"STRS", strs, item_count=len(mod.decode_strs(strs))),
])
out = root / "build" / "ktrf" / "binary-v0.1-smoke.ktnroute"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(data)
parsed = mod.parse_container(data, known_section_types=[b"META", b"STRS"])
print(f"magic={data[:4].decode('ascii')}")
print(f"bytes={len(data)}")
print("sections=" + ",".join(e.type_code.decode("ascii") for e in parsed.entries))
print(f"output={out}")
'@
    Invoke-Checked python -c $code

    Write-Host ""
    Write-Host "=============================================="
    Write-Host " KTRF BINARY CONTAINER v0.1: PASS"
    Write-Host "=============================================="
}
finally {
    Pop-Location
}
