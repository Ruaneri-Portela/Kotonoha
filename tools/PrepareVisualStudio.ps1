# Prepara o projeto Visual Studio independente do Kotonoha.
# Windows: apenas PowerShell (.ps1). macOS/Linux usam Python/sh.
#
# Modos:
#   Generate  - (re)gera .sln, .vcxproj, .vcxproj.filters e ProjectManifest.json
#   Version   - gera o cabecalho de versao a partir do Git
#   Install   - instala dependencias vcpkg para x86, x64 e arm64
#   Build     - compila a solucao para todas as arquiteturas
#   All       - Generate + Version + Install + Build
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Generate
#   powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Build -Architectures x64
param(
    [ValidateSet('Generate', 'Version', 'Install', 'Build', 'All')]
    [string]$Mode = 'All',
    [string[]]$Architectures = @('x86', 'x64', 'arm64'),
    [ValidateSet('Debug', 'Release')]
    [string[]]$Configurations = @('Release'),
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $PSScriptRoot
$VS = Join-Path $Repo 'buildsystems\visualstudio'
$Generated = Join-Path $VS 'generated'
$ManifestPath = Join-Path $VS 'ProjectManifest.json'

$PlatformMap = @{ 'x86' = 'Win32'; 'x64' = 'x64'; 'arm64' = 'ARM64' }
$TripletMap = @{ 'x86' = 'x86-windows'; 'x64' = 'x64-windows'; 'arm64' = 'arm64-windows' }

function StableGuid([string]$key) {
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $bytes = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes("KotonohaVisualStudio/$key"))
        return ([System.Guid]::new($bytes)).ToString('B').ToUpperInvariant()
    } finally { $md5.Dispose() }
}

function XmlEscape([string]$value) {
    return [System.Security.SecurityElement]::Escape($value)
}

function EnsureDir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
}

function WriteUtf8([string]$path, [string]$content) {
    EnsureDir (Split-Path -Parent $path)
    [System.IO.File]::WriteAllText($path, $content, [System.Text.UTF8Encoding]::new($false))
}

# ---------------------------------------------------------------------------
# Catalogo de alvos. Cada modulo e um projeto estatico independente; cada jogo
# e um executavel que compila seus proprios fontes e referencia os modulos.
# ---------------------------------------------------------------------------
$VMs = @(
    @{ Name = 'VM_Amagami';       Root = 'modules\vms\amagami';        Lang = 17 },
    @{ Name = 'VM_Artemis';       Root = 'modules\vms\artemis';        Lang = 17 },
    @{ Name = 'VM_Kag';           Root = 'modules\vms\kag';            Lang = 17 },
    @{ Name = 'VM_Meo';           Root = 'modules\vms\meo';            Lang = 17 },
    @{ Name = 'VM_N2System';      Root = 'modules\vms\n2system';       Lang = 17 },
    @{ Name = 'VM_Pmail';         Root = 'modules\vms\pmail';          Lang = 17 },
    @{ Name = 'VM_Ponscripter';   Root = 'modules\vms\ponscripter';    Lang = 17 },
    @{ Name = 'VM_RealLive';      Root = 'modules\vms\reallive';       Lang = 17 },
    @{ Name = 'VM_RenPy';         Root = 'modules\vms\renpy';          Lang = 20 },
    @{ Name = 'VM_SnowRadish';    Root = 'modules\vms\snow-radish';    Lang = 17 },
    @{ Name = 'VM_SummerRadish';  Root = 'modules\vms\summer-radish';  Lang = 17 },
    @{ Name = 'VM_Tjs2';          Root = 'modules\vms\tjs2';           Lang = 17 }
)

$Formats = @(
    @{ Name = 'Format_CZ';        Root = 'modules\formats\image\cz' },
    @{ Name = 'Format_TLG';       Root = 'modules\formats\image\tlg' },
    @{ Name = 'Format_TIM2';      Root = 'modules\formats\image\tim2' },
    @{ Name = 'Format_TEX';       Root = 'modules\formats\image\tex' },
    @{ Name = 'Format_CRGB';      Root = 'modules\formats\image\crgb' },
    @{ Name = 'Format_EVD';       Root = 'modules\formats\image\evd' },
    @{ Name = 'Format_UIAtlas';   Root = 'modules\formats\image\ui-atlas' },
    @{ Name = 'Format_ADPCM';     Root = 'modules\formats\audio\adpcm' },
    @{ Name = 'Format_CRI';       Root = 'modules\formats\audio\cri' },
    @{ Name = 'Format_SPU';       Root = 'modules\formats\audio\spu' },
    @{ Name = 'Format_BlockPCM';  Root = 'modules\formats\audio\pcm-block' },
    @{ Name = 'Format_SMFD';      Root = 'modules\formats\audio\smfd' },
    @{ Name = 'Format_ANM';       Root = 'modules\formats\animation\anm' },
    @{ Name = 'Format_PSB';       Root = 'modules\formats\data\psb' }
)

$Providers = @(
    @{ Name = 'Provider_Adv';         Root = 'modules\providers\adv' },
    @{ Name = 'Provider_Amagami';     Root = 'modules\providers\amagami' },
    @{ Name = 'Provider_Artemis';     Root = 'modules\providers\artemis' },
    @{ Name = 'Provider_Cmp';         Root = 'modules\providers\cmp' },
    @{ Name = 'Provider_Gpk';         Root = 'modules\providers\gpk' },
    @{ Name = 'Provider_N2';          Root = 'modules\providers\n2' },
    @{ Name = 'Provider_Pak';         Root = 'modules\providers\pak' },
    @{ Name = 'Provider_Pmail';       Root = 'modules\providers\pmail' },
    @{ Name = 'Provider_Ponscripter'; Root = 'modules\providers\ponscripter' },
    @{ Name = 'Provider_RealLive';    Root = 'modules\providers\reallive' },
    @{ Name = 'Provider_Rpa';         Root = 'modules\providers\rpa'; Lang = 20 },
    @{ Name = 'Provider_Xp3';         Root = 'modules\providers\xp3' },
    @{ Name = 'Provider_Zero';        Root = 'modules\providers\zero' }
)

# Jogos: "Extra" lista diretorios de port reutilizados (sem main.cpp).
$Games = @(
    @{ Name = 'Game_KotonohaExample'; Slug = 'example';                      Extra = @() },
    @{ Name = 'Game_Amagami';         Slug = 'amagami';                      Extra = @() },
    @{ Name = 'Game_ZeroNoTsukaima';  Slug = 'zero-no-tsukaima';             Extra = @() },
    @{ Name = 'Game_SnowRadish';      Slug = 'snow-radish-vacation';         Extra = @() },
    @{ Name = 'Game_SummerRadish';    Slug = 'summer-radish-vacation';       Extra = @() },
    @{ Name = 'Game_DokiDoki';        Slug = 'doki-doki-literature-club';    Extra = @(); Lang = 20 },
    @{ Name = 'Game_SchoolDays';      Slug = 'school-days';                  Extra = @() },
    @{ Name = 'Game_RealLive';        Slug = 'reallive';                     Extra = @() },
    @{ Name = 'Game_Kirikiri';        Slug = 'kirikiri';                     Extra = @() },
    @{ Name = 'Game_LucaSystem';      Slug = 'luca-system';                  Extra = @() },
    @{ Name = 'Game_Pmail';           Slug = 'pmail';                        Extra = @() },
    @{ Name = 'Game_Ponscripter';     Slug = 'ponscripter';                  Extra = @() },
    @{ Name = 'Game_N2System';        Slug = 'n2system';                     Extra = @(@{ Root = 'games\ponscripter' }) },
    @{ Name = 'Game_KatawaShoujo';    Slug = 'katawa-shoujo';                Extra = @(@{ Root = 'games\doki-doki-literature-club' }); Lang = 20 },
    @{ Name = 'Game_MEO';             Slug = 'meo';                          Extra = @(@{ Root = 'games\summer-radish-vacation' }) },
    @{ Name = 'Game_Artemis';         Slug = 'artemis';                      Extra = @() }
)

$AllModules = @($VMs) + @($Formats) + @($Providers)
$AllTargets = @('KotonohaEngine') + @($AllModules | ForEach-Object { $_.Name }) +
              @($Games | ForEach-Object { $_.Name }) + @('KotonohaLauncherCore', 'KotonohaLauncher')

# ---------------------------------------------------------------------------
# Geracao dos .vcxproj
# ---------------------------------------------------------------------------
function SourceItemXml([string]$include, [string[]]$exclude = @()) {
    $ex = if ($exclude.Count -gt 0) { ' Exclude="' + (XmlEscape ($exclude -join ';')) + '"' } else { '' }
    return '    <ClCompile Include="' + (XmlEscape $include) + '"' + $ex + ' />'
}

function New-Vcxproj {
    param(
        [string]$Name,
        [string]$Type,
        [string]$Group,
        [int]$Lang = 17,
        [string[]]$Sources = @(),
        [string[]]$References = @(),
        [string]$ExtraDefines = '',
        [switch]$AutoExport
    )

    $guid = StableGuid $Name
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
    [void]$sb.AppendLine('<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    [void]$sb.AppendLine('  <ItemGroup Label="ProjectConfigurations">')
    foreach ($platform in @('Win32', 'x64', 'ARM64')) {
        foreach ($config in @('Debug', 'Release')) {
            [void]$sb.AppendLine("    <ProjectConfiguration Include=`"$config|$platform`">")
            [void]$sb.AppendLine("      <Configuration>$config</Configuration>")
            [void]$sb.AppendLine("      <Platform>$platform</Platform>")
            [void]$sb.AppendLine('    </ProjectConfiguration>')
        }
    }
    [void]$sb.AppendLine('  </ItemGroup>')
    [void]$sb.AppendLine('  <PropertyGroup Label="Globals">')
    [void]$sb.AppendLine('    <VCProjectVersion>17.0</VCProjectVersion>')
    [void]$sb.AppendLine('    <Keyword>Win32Proj</Keyword>')
    [void]$sb.AppendLine("    <ProjectGuid>$guid</ProjectGuid>")
    [void]$sb.AppendLine("    <RootNamespace>$Name</RootNamespace>")
    [void]$sb.AppendLine("    <KotonohaGroup>$Group</KotonohaGroup>")
    [void]$sb.AppendLine('    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>')
    [void]$sb.AppendLine('  </PropertyGroup>')
    [void]$sb.AppendLine('  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />')
    [void]$sb.AppendLine('  <PropertyGroup Label="Configuration">')
    [void]$sb.AppendLine("    <ConfigurationType>$Type</ConfigurationType>")
    if ($Type -eq 'DynamicLibrary') {
        [void]$sb.AppendLine('    <GenerateImportLibrary>true</GenerateImportLibrary>')
        [void]$sb.AppendLine('    <ImportLibrary>$(OutDir)$(ProjectName).lib</ImportLibrary>')
    }
    [void]$sb.AppendLine('    <UseDebugLibraries>false</UseDebugLibraries>')
    [void]$sb.AppendLine('    <PlatformToolset>v145</PlatformToolset>')
    [void]$sb.AppendLine('    <CharacterSet>Unicode</CharacterSet>')
    [void]$sb.AppendLine('    <WholeProgramOptimization>false</WholeProgramOptimization>')
    [void]$sb.AppendLine('  </PropertyGroup>')
    [void]$sb.AppendLine('  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />')
    [void]$sb.AppendLine('  <ImportGroup Label="ExtensionSettings" />')
    [void]$sb.AppendLine('  <ImportGroup Label="Shared" />')
    [void]$sb.AppendLine('  <ImportGroup Label="PropertySheets">')
    [void]$sb.AppendLine('    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists(''$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props'')" Label="LocalAppDataPlatform" />')
    [void]$sb.AppendLine('  </ImportGroup>')
    [void]$sb.AppendLine('  <PropertyGroup Label="UserMacros" />')
    [void]$sb.AppendLine("  <PropertyGroup><LanguageStandard>stdcpp$Lang</LanguageStandard></PropertyGroup>")
    if ($ExtraDefines) {
        [void]$sb.AppendLine('  <ItemDefinitionGroup>')
        [void]$sb.AppendLine("    <ClCompile><PreprocessorDefinitions>%(PreprocessorDefinitions);$ExtraDefines</PreprocessorDefinitions></ClCompile>")
        [void]$sb.AppendLine('  </ItemDefinitionGroup>')
    }
    if ($AutoExport) {
        [void]$sb.AppendLine('  <ItemDefinitionGroup>')
        [void]$sb.AppendLine("    <Link><ModuleDefinitionFile>`$(IntDir)$Name.def</ModuleDefinitionFile></Link>")
        [void]$sb.AppendLine("    <ImpLib><ModuleDefinitionFile>`$(IntDir)$Name.def</ModuleDefinitionFile></ImpLib>")
        [void]$sb.AppendLine('  </ItemDefinitionGroup>')
        [void]$sb.AppendLine('  <Target Name="KotonohaGenerateExports" BeforeTargets="Link">')
        [void]$sb.AppendLine('    <Exec Command="powershell -NoProfile -ExecutionPolicy Bypass -File &quot;$(RepoRoot)tools\GenerateVisualStudioExports.ps1&quot; -ObjectDir &quot;$(IntDir).&quot; -OutputDef &quot;$(IntDir)' + $Name + '.def&quot; -Library ' + $Name + ' -Platform &quot;$(Platform)&quot;" />')
        [void]$sb.AppendLine('  </Target>')
    }
    [void]$sb.AppendLine('  <ItemGroup>')
    foreach ($source in $Sources) { [void]$sb.AppendLine($source) }
    [void]$sb.AppendLine('  </ItemGroup>')
    if ($References.Count -gt 0) {
        [void]$sb.AppendLine('  <ItemGroup>')
        foreach ($reference in $References) {
            [void]$sb.AppendLine("    <ProjectReference Include=`"$reference.vcxproj`">")
            [void]$sb.AppendLine("      <Project>$(StableGuid $reference)</Project>")
            [void]$sb.AppendLine('    </ProjectReference>')
        }
        [void]$sb.AppendLine('  </ItemGroup>')
    }
    [void]$sb.AppendLine('  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />')
    [void]$sb.AppendLine('</Project>')
    WriteUtf8 (Join-Path $VS "$Name.vcxproj") $sb.ToString()

    # Filtros simples: um filtro por diretorio de origem relativo ao repositorio.
    $fb = [System.Text.StringBuilder]::new()
    [void]$fb.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
    [void]$fb.AppendLine('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    [void]$fb.AppendLine('  <ItemGroup>')
    [void]$fb.AppendLine('    <Filter Include="Sources"><UniqueIdentifier>' + (StableGuid "$Name/filter/sources") + '</UniqueIdentifier></Filter>')
    [void]$fb.AppendLine('  </ItemGroup>')
    [void]$fb.AppendLine('  <ItemGroup>')
    $index = 0
    foreach ($source in $Sources) {
        if ($source -match 'Include="([^"]+)"') {
            $index++
            [void]$fb.AppendLine('    <ClCompile Include="' + (XmlEscape $Matches[1]) + '"><Filter>Sources</Filter></ClCompile>')
        }
    }
    [void]$fb.AppendLine('  </ItemGroup>')
    [void]$fb.AppendLine('</Project>')
    WriteUtf8 (Join-Path $VS "$Name.vcxproj.filters") $fb.ToString()
    return $guid
}

function Write-IncludeDirsProps {
    $roots = [System.Collections.Generic.List[string]]::new()
    $roots.Add((Join-Path $Repo 'lib\include'))
    $roots.Add((Join-Path $Repo 'lib\src'))
    foreach ($area in @('modules', 'games', 'launcher')) {
        $base = Join-Path $Repo $area
        if (-not (Test-Path -LiteralPath $base)) { continue }
        Get-ChildItem -LiteralPath $base -Recurse -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -in @('include', 'src') -and
                           $_.FullName -notmatch '[\\/](tests|tools|build|out|obj)[\\/]' } |
            ForEach-Object { $roots.Add($_.FullName) }
    }
    foreach ($sub in @('launcher\core', 'launcher\bridge',
                       'games\reallive\vendor', 'games\reallive\vendor\machine',
                       'games\reallive\vendor\libreallive')) {
        $roots.Add((Join-Path $Repo $sub))
    }
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
    [void]$sb.AppendLine('<!-- Gerado por tools/PrepareVisualStudio.ps1 -Mode Generate. -->')
    [void]$sb.AppendLine('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    [void]$sb.AppendLine('  <ItemGroup>')
    [void]$sb.AppendLine('    <KotonohaGeneratedIncludeDir Include="' +
        (XmlEscape ($roots -join ';')) + '" />')
    [void]$sb.AppendLine('  </ItemGroup>')
    [void]$sb.AppendLine('</Project>')
    WriteUtf8 (Join-Path $VS 'GeneratedIncludeDirs.props') $sb.ToString()
    return $roots.Count
}

function Invoke-Generate {
    EnsureDir $VS
    EnsureDir $Generated
    $includeCount = Write-IncludeDirsProps
    $manifestTargets = [System.Collections.Generic.List[object]]::new()
    $moduleNames = @($AllModules | ForEach-Object { $_.Name })

    # lib/ - motor principal
    # KOTONOHA_ENABLE_UPSCALER=0: Upscaler.cpp fornece a fachada; os backends
    # MetalFX/MPV ficam de fora, exatamente como no CMake com a opcao OFF.
    $engineGuid = New-Vcxproj -Name 'KotonohaEngine' -Type 'DynamicLibrary' -Group 'lib' -AutoExport -Sources @(
        (SourceItemXml -include '$(RepoRoot)lib\src\**\*.cpp' -exclude @(
            '$(RepoRoot)lib\src\media\upscaling\MetalFXUpscaler.cpp',
            '$(RepoRoot)lib\src\media\upscaling\MPVCompatibleUpscaler.cpp'))
    )
    $manifestTargets.Add(@{ name = 'KotonohaEngine'; type = 'DynamicLibrary'; group = 'lib'; guid = $engineGuid; sources = 85 })

    # Dear ImGui, SDL3 e SDL3_ttf vem inteiros do vcpkg (incluindo headers).

    # modules/ - um projeto por modulo
    foreach ($entry in $AllModules) {
        $lang = if ($entry.ContainsKey('Lang')) { [int]$entry.Lang } else { 17 }
        $excludes = @("`$(RepoRoot)" + $entry.Root + '\tests\**\*.cpp')
        if ($entry.Name -eq 'VM_RenPy') {
            $excludes += "`$(RepoRoot)" + $entry.Root + '\src\PythonDummy.cpp'
        }
        $sources = @(
            (SourceItemXml -include ("`$(RepoRoot)" + $entry.Root + '\src\*.cpp') -exclude $excludes)
        )
        $group = if ($VMs.Name -contains $entry.Name) { 'modules\vms' } elseif ($Formats.Name -contains $entry.Name) { 'modules\formats' } else { 'modules\providers' }
        $guid = New-Vcxproj -Name $entry.Name -Type 'StaticLibrary' -Group $group -Lang $lang -Sources $sources -References @('KotonohaEngine')
        $count = @(Get-ChildItem -Path (Join-Path $Repo ($entry.Root + '\src')) -Filter '*.cpp' -File -Recurse -ErrorAction SilentlyContinue).Count
        $manifestTargets.Add(@{ name = $entry.Name; type = 'StaticLibrary'; group = $group; guid = $guid; sources = $count })
    }

    # games/ - uma biblioteca estatica por port + um executavel com apenas main.cpp
    # Cada projeto tem seu proprio IntDir, portanto nomes de arquivos iguais em
    # jogos diferentes (Source.cpp, Archive.cpp, Game.cpp...) nunca colidem.
    $gameLibNames = @()
    foreach ($entry in $Games) {
        $lang = if ($entry.ContainsKey('Lang')) { [int]$entry.Lang } else { 17 }
        $libName = $entry.Name + 'Lib'
        $gameLibNames += $libName
        $sources = @(
            (SourceItemXml -include ("`$(RepoRoot)games\" + $entry.Slug + '\src\**\*.cpp') -exclude @("`$(RepoRoot)games\" + $entry.Slug + '\src\main.cpp'))
        )
        if ($entry.Slug -eq 'reallive') {
            $sources += (SourceItemXml '$(RepoRoot)games\reallive\vendor\libreallive\*.cc')
        }
        foreach ($extra in $entry.Extra) {
            $sources += (SourceItemXml -include ("`$(RepoRoot)" + $extra.Root + '\src\**\*.cpp') -exclude @("`$(RepoRoot)" + $extra.Root + '\src\main.cpp'))
        }
        $libGuid = New-Vcxproj -Name $libName -Type 'StaticLibrary' -Group 'games' -Lang $lang -Sources $sources -References (@('KotonohaEngine') + $moduleNames)
        $count = @(Get-ChildItem -Path (Join-Path $Repo ('games\' + $entry.Slug + '\src')) -Filter '*.cpp' -File -Recurse -ErrorAction SilentlyContinue).Count - 1
        $manifestTargets.Add(@{ name = $libName; type = 'StaticLibrary'; group = 'games'; guid = $libGuid; sources = $count; slug = $entry.Slug; lib_for = $entry.Name })

        $appGuid = New-Vcxproj -Name $entry.Name -Type 'Application' -Group 'games' -Lang $lang -Sources @(
            (SourceItemXml ("`$(RepoRoot)games\" + $entry.Slug + '\src\main.cpp'))
        ) -References (@($libName, 'KotonohaEngine') + $moduleNames)
        $manifestTargets.Add(@{ name = $entry.Name; type = 'Application'; group = 'games'; guid = $appGuid; sources = 1; slug = $entry.Slug; lib = $libName })
    }

    # launcher/ - nucleo compilado apenas uma vez e vinculado as bibliotecas
    # estaticas de todos os jogos (o mesmo grafo do CMake).
    $coreGuid = New-Vcxproj -Name 'KotonohaLauncherCore' -Type 'DynamicLibrary' -Group 'launcher' -Lang 20 `
        -AutoExport -ExtraDefines 'KOTONOHA_LAUNCHER_CORE_BUILD=1;KOTONOHA_LAUNCH_AMAGAMI=1;KOTONOHA_LAUNCH_ARTEMIS=1;KOTONOHA_LAUNCH_DDLC=1;KOTONOHA_LAUNCH_KATAWA=1;KOTONOHA_LAUNCH_KIRIKIRI=1;KOTONOHA_LAUNCH_LUCA_SYSTEM=1;KOTONOHA_LAUNCH_MEO=1;KOTONOHA_LAUNCH_N2SYSTEM=1;KOTONOHA_LAUNCH_PMAIL=1;KOTONOHA_LAUNCH_PONSCRIPTER=1;KOTONOHA_LAUNCH_REALLIVE=1;KOTONOHA_LAUNCH_SCHOOL_DAYS=1;KOTONOHA_LAUNCH_SNOW=1;KOTONOHA_LAUNCH_SUMMER=1;KOTONOHA_LAUNCH_ZERO=1' -Sources @(
        (SourceItemXml '$(RepoRoot)launcher\core\*.cpp')
    ) -References (@('KotonohaEngine') + $moduleNames + $gameLibNames)
    $coreCount = @(Get-ChildItem -Path (Join-Path $Repo 'launcher\core') -Filter '*.cpp' -File -ErrorAction SilentlyContinue).Count
    $manifestTargets.Add(@{ name = 'KotonohaLauncherCore'; type = 'DynamicLibrary'; group = 'launcher'; guid = $coreGuid; sources = $coreCount })

    $launcherGuid = New-Vcxproj -Name 'KotonohaLauncher' -Type 'Application' -Group 'launcher' -Sources @(
        (SourceItemXml '$(RepoRoot)launcher\app\main.cpp')
    ) -References (@('KotonohaEngine', 'KotonohaLauncherCore') + $moduleNames)
    $manifestTargets.Add(@{ name = 'KotonohaLauncher'; type = 'Application'; group = 'launcher'; guid = $launcherGuid; sources = 1 })

    # Solucao com pastas por area e 6 combinacoes config/plataforma
    $sln = [System.Text.StringBuilder]::new()
    [void]$sln.AppendLine('Microsoft Visual Studio Solution File, Format Version 12.00')
    [void]$sln.AppendLine('# Visual Studio Version 17')
    [void]$sln.AppendLine('VisualStudioVersion = 17.0.31903.59')
    [void]$sln.AppendLine('MinimumVisualStudioVersion = 10.0.40219.1')
    $folders = @('lib', 'modules', 'modules\vms', 'modules\formats', 'modules\providers', 'games', 'launcher')
    $folderGuids = @{}
    foreach ($folder in $folders) {
        $fg = StableGuid "folder/$folder"
        $folderGuids[$folder] = $fg
        [void]$sln.AppendLine("Project(`"{2150E333-8FDC-42A3-9474-1A3956D46DE8}`") = `"$($folder.Split('\')[-1])`", `"$folder`", `"$fg`"")
        [void]$sln.AppendLine('EndProject')
    }
    foreach ($target in $manifestTargets) {
        $path = "$($target.name).vcxproj"
        [void]$sln.AppendLine("Project(`"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}`") = `"$($target.name)`", `"$path`", `"$($target.guid)`"")
        [void]$sln.AppendLine('EndProject')
    }
    $tab = [char]9
    [void]$sln.AppendLine('Global')
    [void]$sln.AppendLine($tab + 'GlobalSection(SolutionConfigurationPlatforms) = preSolution')
    foreach ($config in @('Debug', 'Release')) {
        foreach ($platform in @('Win32', 'x64', 'ARM64')) {
            [void]$sln.AppendLine($tab + $tab + "$config|$platform = $config|$platform")
        }
    }
    [void]$sln.AppendLine($tab + 'EndGlobalSection')
    [void]$sln.AppendLine($tab + 'GlobalSection(ProjectConfigurationPlatforms) = postSolution')
    foreach ($target in $manifestTargets) {
        foreach ($config in @('Debug', 'Release')) {
            foreach ($platform in @('Win32', 'x64', 'ARM64')) {
                [void]$sln.AppendLine($tab + $tab + "$($target.guid).$config|$platform.ActiveCfg = $config|$platform")
                [void]$sln.AppendLine($tab + $tab + "$($target.guid).$config|$platform.Build.0 = $config|$platform")
            }
        }
    }
    [void]$sln.AppendLine($tab + 'EndGlobalSection')
    [void]$sln.AppendLine($tab + 'GlobalSection(SolutionProperties) = preSolution')
    [void]$sln.AppendLine($tab + $tab + 'HideSolutionNode = FALSE')
    [void]$sln.AppendLine($tab + 'EndGlobalSection')
    [void]$sln.AppendLine($tab + 'GlobalSection(NestedProjects) = preSolution')
    foreach ($target in $manifestTargets) {
        $parent = if ($folderGuids.ContainsKey($target.group)) { $folderGuids[$target.group] } else { $folderGuids['lib'] }
        [void]$sln.AppendLine($tab + $tab + "$($target.guid) = $parent")
    }
    foreach ($child in @('modules\vms', 'modules\formats', 'modules\providers')) {
        [void]$sln.AppendLine($tab + $tab + "$($folderGuids[$child]) = $($folderGuids['modules'])")
    }
    [void]$sln.AppendLine($tab + 'EndGlobalSection')
    [void]$sln.AppendLine('EndGlobal')
    WriteUtf8 (Join-Path $VS 'Kotonoha.sln') $sln.ToString()

    $manifest = @{
        project = 'buildsystems/visualstudio/Kotonoha.sln'
        configurations = @('Debug', 'Release')
        platforms = @('Win32', 'x64', 'ARM64')
        vcpkg_triplets = @{ 'Win32' = 'x86-windows'; 'x64' = 'x64-windows'; 'ARM64' = 'arm64-windows' }
        vcpkg_install_roots = @{
            'x86' = 'buildsystems/visualstudio/vcpkg/x86'
            'x64' = 'buildsystems/visualstudio/vcpkg/x64'
            'arm64' = 'buildsystems/visualstudio/vcpkg/arm64'
        }
        vcpkg_features = @('sdl-vcpkg', 'imgui-vcpkg')
        preparation = @{ script = 'tools/PrepareVisualStudio.ps1'; manifest = 'vcpkg.json'; overlays = 'vcpkg-overlays/ports' }
        targets = $manifestTargets.ToArray()
        games = @{}
        modules = @{ VM = @($VMs.Name); Formats = @($Formats.Name); Providers = @($Providers.Name) }
        source_files = 0
        include_directories = $includeCount
        note = 'Native Visual Studio solution: sources are compiled directly; no CMake invocation.'
    }
    foreach ($entry in $Games) { $manifest.games[$entry.Name] = $entry.Slug }
    $total = 0
    foreach ($target in $manifestTargets) { $total += [int]$target.sources }
    $manifest.source_files = $total
    $json = $manifest | ConvertTo-Json -Depth 8
    WriteUtf8 $ManifestPath ($json + [Environment]::NewLine)
    Write-Host "[Kotonoha] Gerados $($manifestTargets.Count) projetos em buildsystems/visualstudio"
}

function Invoke-Version {
    $branch = (& git -C $Repo rev-parse --abbrev-ref HEAD 2>$null); if (-not $branch) { $branch = 'unknown' }
    $commit = (& git -C $Repo rev-parse HEAD 2>$null); if (-not $commit) { $commit = 'unknown' }
    $short = (& git -C $Repo rev-parse --short HEAD 2>$null); if (-not $short) { $short = 'unknown' }
    $dirty = if (& git -C $Repo status --porcelain 2>$null) { 1 } else { 0 }
    $template = Get-Content -Raw (Join-Path $Repo 'buildsystems\cmake\BuildVersion.hpp.in')
    $header = $template.Replace('// Generated from Git during CMake configuration. Do not edit by hand.',
                                '// Generated by tools/PrepareVisualStudio.ps1. Do not edit by hand.')
    $header = $header.Replace('@CMAKE_PROJECT_VERSION@', '0.1.0')
    $header = $header.Replace('@KOTONOHA_VERSION_FULL@', "0.1.0+$short")
    $header = $header.Replace('@KOTONOHA_GIT_BRANCH@', $branch)
    $header = $header.Replace('@KOTONOHA_GIT_COMMIT@', $commit)
    $header = $header.Replace('@KOTONOHA_GIT_SHORT@', $short)
    $header = $header.Replace('@KOTONOHA_GIT_DIRTY@', [string]$dirty)
    WriteUtf8 (Join-Path $Generated 'Kotonoha\core\BuildVersion.hpp') $header
    Write-Host "[Kotonoha] BuildVersion.hpp atualizado ($short, dirty=$dirty)"
}

function Invoke-Install {
    if (-not $VcpkgRoot) {
        if ($env:VCPKG_ROOT) { $VcpkgRoot = $env:VCPKG_ROOT }
        else {
            $cmd = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
            if ($cmd) { $VcpkgRoot = Split-Path -Parent $cmd.Source }
        }
    }
    if (-not $VcpkgRoot -or -not (Test-Path -LiteralPath (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
        throw 'vcpkg nao encontrado. Defina VCPKG_ROOT ou instale o vcpkg via Visual Studio Installer.'
    }
    $exe = Join-Path $VcpkgRoot 'vcpkg.exe'
    foreach ($arch in $Architectures) {
        $triplet = $TripletMap[$arch]
        Write-Host "[Kotonoha] vcpkg install $triplet"
        & $exe install '--x-wait-for-lock' "--triplet=$triplet" "--x-manifest-root=$Repo" `
            "--x-install-root=$(Join-Path $VS ('vcpkg\' + $arch))" `
            "--overlay-ports=$(Join-Path $Repo 'vcpkg-overlays\ports')" `
            '--x-feature=sdl-vcpkg' '--x-feature=imgui-vcpkg'
        if ($LASTEXITCODE -ne 0) { throw "vcpkg falhou para $triplet (exit $LASTEXITCODE)" }
    }
}

function Invoke-Build {
    $msbuild = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $install = $null
    if (Test-Path -LiteralPath $msbuild) {
        $install = & $msbuild -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath 2>$null | Select-Object -First 1
    }
    if (-not $install) { $install = 'C:\Program Files\Microsoft Visual Studio\18\Community' }
    $msbuildExe = Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Path -LiteralPath $msbuildExe)) { throw "MSBuild nao encontrado: $msbuildExe" }
    foreach ($arch in $Architectures) {
        $platform = $PlatformMap[$arch]
        foreach ($config in $Configurations) {
            Write-Host "[Kotonoha] MSBuild $platform/$config"
            & $msbuildExe (Join-Path $VS 'Kotonoha.sln') "/p:Configuration=$config" "/p:Platform=$platform" '/m' '/nologo' '/verbosity:minimal'
            if ($LASTEXITCODE -ne 0) { throw "Build falhou para $platform/$config (exit $LASTEXITCODE)" }
        }
    }
}

switch ($Mode) {
    'Generate' { Invoke-Generate }
    'Version'  { Invoke-Version }
    'Install'  { Invoke-Install }
    'Build'    { Invoke-Build }
    'All'      { Invoke-Generate; Invoke-Version; Invoke-Install; Invoke-Build }
}
