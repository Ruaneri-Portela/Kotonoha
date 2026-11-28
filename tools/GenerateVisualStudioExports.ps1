# Equivalente nativo do WINDOWS_EXPORT_ALL_SYMBOLS do CMake.
# Gera um .def com os simbolos externos definidos nos objetos compilados,
# para que KotonohaEngine.dll e KotonohaLauncherCore.dll exportem toda a API C++.
param(
    [Parameter(Mandatory = $true)][string]$ObjectDir,
    [Parameter(Mandatory = $true)][string]$OutputDef,
    [Parameter(Mandatory = $true)][string]$Library,
    [ValidateSet('Win32', 'x64', 'ARM64')][string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$arch = @{ 'Win32' = 'x86'; 'x64' = 'x64'; 'ARM64' = 'arm64' }[$Platform]
$vc = Join-Path $env:VCPKG_ROOT '..\..\VC\Tools\MSVC'
if (-not (Test-Path -LiteralPath $vc)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $install = & $vswhere -latest -products '*' -property installationPath 2>$null | Select-Object -First 1
    $vc = Join-Path $install 'VC\Tools\MSVC'
}
$toolset = Get-ChildItem -LiteralPath $vc -Directory | Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $toolset.FullName ('bin\Hostx64\' + $arch + '\dumpbin.exe')
if (-not (Test-Path -LiteralPath $dumpbin)) { throw "dumpbin.exe nao encontrado: $dumpbin" }

$symbols = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
foreach ($obj in Get-ChildItem -LiteralPath $ObjectDir -Filter '*.obj' -File -ErrorAction SilentlyContinue) {
    foreach ($line in (& $dumpbin '/symbols' $obj.FullName 2>$null)) {
        # COFF: index offset section type [extras] External | symbol
        if ($line -notmatch '^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(SECT\S+|ABS|UNDEF)\s+.+?\s+External\s*\|\s+(\S+)') { continue }
        if ($Matches[1] -eq 'UNDEF') { continue }
        $symbol = $Matches[2]
        # Simbolos internos do CRT/STL nao fazem parte da API publica das DLLs.
        if ($symbol.StartsWith('.') -or $symbol.StartsWith('__') -or
            $symbol.StartsWith('?_OptionsStorage') -or
            # Template/COMDAT instantiations (std::unique_ptr, _Compressed_pair,
            # ...) are inlined and are not part of the public C/C++ ABI; exporting
            # them makes the linker fail with LNK2001 on ARM64.
            $symbol.StartsWith('??$') -or
            $symbol -in @('__fltused', '__security_cookie', '__C_specific_handler')) { continue }
        # LNK4102: exportar thunks de destrutor pode gerar imagem invalida.
        if ($line -match 'deleting destructor|vtordisp|\[thunk\]') { continue }
        # No x86 o __cdecl adiciona '_' ao nome COFF; o .def deve usar o nome
        # sem decoracao (o LINK adiciona o '_' ao procurar o simbolo).
        # __stdcall (RUNDLL32) mantem o '_' e o sufixo '@16'; so __cdecl
        # deve ser convertido para o nome sem decoracao no .def.
        if ($arch -eq 'x86' -and $symbol -match '^_[^_]' -and
            $symbol -notmatch '@\d+$') {
            $symbol = $symbol.Substring(1)
        }
        [void]$symbols.Add($symbol)
    }
}
$content = "LIBRARY $Library`r`nEXPORTS`r`n"
foreach ($symbol in $symbols) { $content += "  $symbol`r`n" }
[System.IO.File]::WriteAllText($OutputDef, $content, [System.Text.UTF8Encoding]::new($false))
Write-Host "[Kotonoha] ${Library}: $($symbols.Count) simbolos exportados (WINDOWS_EXPORT_ALL_SYMBOLS)"
