#!/usr/bin/env python3
"""Generate the checked-in Visual Studio solution for Kotonoha (Python 3.8+).

Two modes:

  python3 Scripts/generate_visualstudio.py --export-from-cmake <build-dir>
      Refresh ProjectManifest.json source lists from a configured CMake
      "Visual Studio" build tree. CMake remains the source of truth for which
      translation units a Windows application actually compiles.

  python3 Scripts/generate_visualstudio.py
      Write Kotonoha.sln, Directory.Build.props, the per-target .vcxproj
      files and the shared resources from ProjectManifest.json.

  python3 Scripts/generate_visualstudio.py --check
      Read-only verification that every declared target has a project, every
      source exists and every platform/configuration pair is wired in the
      solution. Used by tools/ValidateBuildMetadata.py.
"""
import argparse
import json
import os
import re
import sys
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

VS = Path(__file__).resolve().parents[1]
ROOT = VS.parents[1]
MANIFEST = VS / 'ProjectManifest.json'
NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'
CPPS = ('.cpp', '.cc', '.c')

# CMake application target -> generated .vcxproj inside a CMake VS build tree.
CMAKE_APPS = {
    'KotonohaExample': 'games/example/Kotonoha.vcxproj',
    'Amagami': 'games/amagami/Amagami.vcxproj',
    'ZeroNoTsukaima': 'games/zero-no-tsukaima/ZeroNoTsukaima.vcxproj',
    'SnowRadish': 'games/snow-radish-vacation/SnowRadish.vcxproj',
    'SummerRadish': 'games/summer-radish-vacation/SummerRadish.vcxproj',
    'MEO': 'games/meo/MEO.vcxproj',
    'DokiDoki': 'games/doki-doki-literature-club/DokiDoki.vcxproj',
    'KatawaShoujo': 'games/katawa-shoujo/KatawaShoujo.vcxproj',
    'SchoolDays': 'games/school-days/SchoolDays.vcxproj',
    'RealLive': 'games/reallive/RealLive.vcxproj',
    'Kirikiri': 'games/kirikiri/Kirikiri.vcxproj',
    'LucaSystem': 'games/luca-system/LucaSystem.vcxproj',
    'Pmail': 'games/pmail/Pmail.vcxproj',
    'PonscripterGame': 'games/ponscripter/PonscripterGame.vcxproj',
    'N2SystemGame': 'games/n2system/N2SystemGame.vcxproj',
    'ArtemisGame': 'games/artemis/ArtemisGame.vcxproj',
    'KotonohaLauncher': 'launcher/KotonohaLauncher.vcxproj',
}
CMAKE_LIBS = {
    'KotonohaEngine': 'lib/KotonohaEngine.vcxproj',
    'KotonohaImGui': 'lib/imgui.vcxproj',
}
# Targets that are helper executables/tests and must never be pulled into an
# application's direct source list when walking ProjectReference edges.
UTILITY_SUFFIXES = ('Tests', 'Test', 'Check', 'Inspect', 'Probe', 'Extract',
                    'Run')
UTILITY_NAMES = ('ALL_BUILD', 'ZERO_CHECK', 'INSTALL', 'PACKAGE', 'RUN_TESTS',
                 'Continuous', 'Experimental', 'Nightly', 'NightlyMemoryCheck')

GAME_ORDER = [
    'KotonohaExample', 'Amagami', 'ZeroNoTsukaima', 'SnowRadish',
    'SummerRadish', 'MEO', 'DokiDoki', 'KatawaShoujo', 'SchoolDays',
    'RealLive', 'Kirikiri', 'LucaSystem', 'Pmail', 'PonscripterGame',
    'N2SystemGame', 'ArtemisGame',
]
DISPLAY_NAMES = {
    'KotonohaEngine': 'Kotonoha Engine',
    'KotonohaImGui': 'Kotonoha ImGui',
    'KotonohaExample': 'Kotonoha Example',
    'Amagami': 'Amagami',
    'ZeroNoTsukaima': 'Zero no Tsukaima',
    'SnowRadish': 'Snow Radish',
    'SummerRadish': 'Summer Radish',
    'MEO': 'MEO',
    'DokiDoki': 'Doki Doki Literature Club',
    'KatawaShoujo': 'Katawa Shoujo',
    'SchoolDays': 'School Days',
    'RealLive': 'RealLive',
    'Kirikiri': 'Kirikiri',
    'LucaSystem': 'LucaSystem',
    'Pmail': 'Pure Mail',
    'PonscripterGame': 'Ponscripter',
    'N2SystemGame': 'N2System',
    'ArtemisGame': 'Artemis',
    'KotonohaLauncher': 'Kotonoha',
}
GAME_SLUGS = {
    'KotonohaExample': 'example',
    'Amagami': 'amagami',
    'ZeroNoTsukaima': 'zero-no-tsukaima',
    'SnowRadish': 'snow-radish-vacation',
    'SummerRadish': 'summer-radish-vacation',
    'MEO': 'meo',
    'DokiDoki': 'doki-doki-literature-club',
    'KatawaShoujo': 'katawa-shoujo',
    'SchoolDays': 'school-days',
    'RealLive': 'reallive',
    'Kirikiri': 'kirikiri',
    'LucaSystem': 'luca-system',
    'Pmail': 'pmail',
    'PonscripterGame': 'ponscripter',
    'N2SystemGame': 'n2system',
    'ArtemisGame': 'artemis',
}


def project_guid(name):
    """Stable GUID derived from the project name; regeneration is idempotent."""
    value = uuid.uuid5(uuid.NAMESPACE_URL, 'kotonoha/visualstudio/' + name)
    return '{' + str(value).upper() + '}'


def win_path(path):
    return str(path).replace('/', '\\')


def rel_source(source, target_name):
    """Repo-relative source path expressed from its project directory."""
    project_dir = VS / 'Projects' / target_name
    return os.path.relpath(ROOT / source, project_dir).replace('/', '\\')


def project_path(target):
    return VS / 'Projects' / target / (target + '.vcxproj')


def read_sources(build, project):
    tree = ET.parse(build / project)
    out = []
    for node in tree.getroot().findall(f'.//{NS}ClCompile'):
        inc = node.get('Include')
        if not inc or not inc.lower().endswith(CPPS):
            continue
        path = Path(inc)
        if not path.is_absolute():
            continue
        try:
            out.append(path.resolve().relative_to(ROOT).as_posix())
        except ValueError:
            pass
    return out


def is_utility(name):
    return name in UTILITY_NAMES or name.endswith(UTILITY_SUFFIXES)


def project_name(path):
    node = ET.parse(path).getroot().find(f'.//{NS}ProjectName')
    return node.text if node is not None else path.stem


def collect_linked_sources(build, project, seen=None):
    """Direct sources of a CMake app plus every static library it links."""
    if seen is None:
        seen = set()
    path = build / project
    if path in seen:
        return []
    seen.add(path)
    sources = read_sources(build, project)
    if not is_utility(project_name(path)):
        tree = ET.parse(path)
        for ref in tree.getroot().findall(f'.//{NS}ProjectReference'):
            inc = ref.get('Include')
            if inc and Path(inc).exists():
                child = Path(inc).relative_to(build).as_posix()
                sources.extend(collect_linked_sources(build, child, seen))
    return sources


def unique(items):
    seen = set()
    out = []
    for item in items:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def export_from_cmake(build):
    """Refresh the manifest source lists from a configured CMake VS tree."""
    build = Path(build).resolve()
    if not (build / 'CMakeCache.txt').is_file():
        raise RuntimeError(f'{build} is not a configured CMake build directory')
    data = json.loads(MANIFEST.read_text(encoding='utf-8'))
    targets = {t['name']: t for t in data['targets']}
    for name, project in CMAKE_LIBS.items():
        targets[name]['sources'] = unique(read_sources(build, project))
    for name, project in CMAKE_APPS.items():
        targets[name]['sources'] = unique(collect_linked_sources(build, project))
    data['generated_from'] = ('CMake "Visual Studio" generator; refresh with '
                              '--export-from-cmake after adding sources')
    MANIFEST.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n',
                        encoding='utf-8')
    for name, project in list(CMAKE_LIBS.items()) + list(CMAKE_APPS.items()):
        print(f'[Kotonoha] {name}: {len(targets[name]["sources"])} sources')


def load_manifest():
    data = json.loads(MANIFEST.read_text(encoding='utf-8'))
    return data


def check():
    data = load_manifest()
    errors = []
    names = [t['name'] for t in data['targets']]
    if len(names) != len(set(names)):
        errors.append('duplicate target name')
    expected = (['KotonohaEngine', 'KotonohaImGui'] +
                [m['name'] for m in data.get('module_libraries', [])] +
                GAME_ORDER +
                ['KotonohaLauncherCore', 'KotonohaLauncher'])
    if names != expected:
        errors.append(f'target order/names differ: {names}')
    for target in data['targets']:
        path = project_path(target['name'])
        if not path.is_file():
            errors.append(f'missing project: {path.name}')
            continue
        text = path.read_text(encoding='utf-8')
        for src in target['sources']:
            if not (ROOT / src).is_file():
                errors.append(f'{target["name"]}: missing source {src}')
            if rel_source(src, target['name']) not in text:
                errors.append(f'{target["name"]}: source not in project: {src}')
        filters = project_path(target['name']).with_suffix('.vcxproj.filters')
        if not filters.is_file():
            errors.append(f'missing filters: {filters.name}')
        for cfg in data['configurations']:
            for plat in data['platforms']:
                if f"'{cfg}|{plat}'" not in text:
                    errors.append(f'{target["name"]}: missing {cfg}|{plat}')
    sln = (VS / 'Kotonoha.sln').read_text(encoding='utf-8')
    for target in data['targets']:
        if project_guid(target['name']) not in sln:
            errors.append(f'solution missing {target["name"]}')
    if errors:
        for error in errors[:20]:
            print(f'[Kotonoha] VS check failed: {error}', file=sys.stderr)
        raise SystemExit(1)
    print(f'[Kotonoha] OK: {len(data["targets"])} Visual Studio projects; '
          f'{len(data["configurations"]) * len(data["platforms"])} '
          f'configuration/platform targets')


def xml_escape(value):
    return (str(value).replace('&', '&amp;').replace('<', '&lt;')
            .replace('>', '&gt;').replace('"', '&quot;'))


def write_vcxproj(target, data):
    name = target['name']
    is_lib = target['kind'] == 'StaticLibrary'
    cfg_type = 'StaticLibrary' if is_lib else 'Application'
    lines = []
    add = lines.append
    add('<?xml version="1.0" encoding="utf-8"?>')
    add('<Project DefaultTargets="Build" ToolsVersion="17.0" '
        'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    add('  <ItemGroup Label="ProjectConfigurations">')
    for cfg in data['configurations']:
        for plat in data['platforms']:
            add(f'    <ProjectConfiguration Include="{cfg}|{plat}">')
            add(f'      <Configuration>{cfg}</Configuration>')
            add(f'      <Platform>{plat}</Platform>')
            add('    </ProjectConfiguration>')
    add('  </ItemGroup>')
    add('  <PropertyGroup Label="Globals">')
    add(f'    <ProjectGuid>{project_guid(name)}</ProjectGuid>')
    add('    <Keyword>Win32Proj</Keyword>')
    add('    <RootNamespace>' + re.sub(r'[^A-Za-z0-9_]', '_', name) + '</RootNamespace>')
    add(f'    <ProjectName>{name}</ProjectName>')
    add('    <VcpkgEnabled>false</VcpkgEnabled>')
    add('  </PropertyGroup>')
    add('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
    for cfg in data['configurations']:
        for plat in data['platforms']:
            add(f'  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'{cfg}|{plat}\'" Label="Configuration">')
            add(f'    <ConfigurationType>{cfg_type}</ConfigurationType>')
            add('    <UseDebugLibraries>' + ('true' if cfg == 'Debug' else 'false') + '</UseDebugLibraries>')
            add('    <CharacterSet>Unicode</CharacterSet>')
            add('    <WholeProgramOptimization>false</WholeProgramOptimization>')
            # SDL's checked-in .vcxproj files default VisualStudioVersion to
            # 10.0; pin the current toolset so our projects never inherit v100.
            add('    <PlatformToolset>$(DefaultPlatformToolset)</PlatformToolset>')
            add('  </PropertyGroup>')
    add('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    add('  <ImportGroup Label="ExtensionSettings" />')
    add('  <ImportGroup Label="PropertySheets" />')
    add('  <PropertyGroup Label="UserMacros" />')
    add('  <PropertyGroup>')
    add('    <OutDir>$(KotonohaOutputDir)$(Configuration)\\$(ProjectName)\\</OutDir>')
    add('    <IntDir>$(KotonohaIntermediateDir)$(ProjectName)\\$(Configuration)\\</IntDir>')
    add('    <TargetName>' + name + '</TargetName>')
    add(f'    <KotonohaDisplayName>{xml_escape(target["displayName"])}</KotonohaDisplayName>')
    add('  </PropertyGroup>')
    add('  <ItemGroup>')
    for source in target['sources']:
        add(f'    <ClCompile Include="{xml_escape(rel_source(source, name))}" />')
    if not is_lib:
        add('    <ResourceCompile Include="$(KotonohaVSRoot)Resources\\Kotonoha.rc" />')
    add('  </ItemGroup>')
    if not is_lib:
        add('  <ItemGroup>')
        for dep in target.get('references', []):
            add(f'    <ProjectReference Include="..\\{dep}\\{dep}.vcxproj">')
            add(f'      <Project>{project_guid(dep)}</Project>')
            add('      <ReferenceOutputAssembly>true</ReferenceOutputAssembly>')
            add('    </ProjectReference>')
        for native in ('SDL3', 'SDL3_ttf'):
            info = data['native_projects'][native]
            condition = ('' if native == 'SDL3'
                         else ' Condition="\'$(Platform)\' != \'ARM64\'"')
            add(f'    <ProjectReference Include="..\\..\\..\\..\\{info["path"]}"{condition}>')
            add(f'      <Project>{info["guid"]}</Project>')
            add('      <ReferenceOutputAssembly>true</ReferenceOutputAssembly>')
            add('    </ProjectReference>')
        add('  </ItemGroup>')
    add('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    add('  <ImportGroup Label="ExtensionTargets" />')
    add('</Project>')
    path = project_path(name)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def filter_guid(name):
    value = uuid.uuid5(uuid.NAMESPACE_URL, 'kotonoha/vs/filter/' + name)
    return '{' + str(value).upper() + '}'


def write_filters(target, data):
    """Generate Solution Explorer folders mirroring the repository layout."""
    name = target['name']
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<Project ToolsVersion="4.0" '
        'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">',
        '  <ItemGroup>',
    ]
    folders = set()
    for source in target['sources']:
        parts = Path(source).parts
        for i in range(1, len(parts)):
            folders.add('\\'.join(parts[:i]))
    for folder in sorted(folders):
        lines.append(f'    <Filter Include="{xml_escape(folder)}">')
        lines.append(f'      <UniqueIdentifier>{filter_guid(folder)}</UniqueIdentifier>')
        lines.append('    </Filter>')
    lines.append('  </ItemGroup>')
    lines.append('  <ItemGroup>')
    for source in target['sources']:
        folder = '\\'.join(Path(source).parts[:-1])
        rel = rel_source(source, name)
        lines.append(f'    <ClCompile Include="{xml_escape(rel)}">')
        if folder:
            lines.append(f'      <Filter>{xml_escape(folder)}</Filter>')
        lines.append('    </ClCompile>')
    lines.append('  </ItemGroup>')
    lines.append('</Project>')
    path = project_path(name).with_suffix('.vcxproj.filters')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


PROPS_TEMPLATE = r'''<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- Shared settings for every checked-in Visual Studio project.  CMake and
       Xcode own the canonical source lists; this file only mirrors their
       compiler, include and dependency choices for MSBuild. -->
  <PropertyGroup>
    <KotonohaVSRoot>$(MSBuildThisFileDirectory)</KotonohaVSRoot>
    <KotonohaRoot>$(MSBuildThisFileDirectory)..\..\</KotonohaRoot>
    <KotonohaOutputDir>$(KotonohaRoot)out\vs\</KotonohaOutputDir>
    <KotonohaIntermediateDir>$(KotonohaRoot)out\vs\obj\</KotonohaIntermediateDir>
    <KotonohaGeneratedDir>$(KotonohaVSRoot)generated\</KotonohaGeneratedDir>
    <KotonohaVcpkgInstalled Condition="'$(KotonohaVcpkgInstalled)' == ''">$(KotonohaRoot)vcpkg_installed\</KotonohaVcpkgInstalled>
    <!-- Tracker.exe re-escapes the quoted /D macros and breaks rc.exe. -->
    <TrackFileAccess>false</TrackFileAccess>
    <KotonohaVcpkgTriplet Condition="'$(Platform)' == 'Win32'">x86-windows</KotonohaVcpkgTriplet>
    <KotonohaVcpkgTriplet Condition="'$(Platform)' == 'x64'">x64-windows</KotonohaVcpkgTriplet>
    <KotonohaVcpkgTriplet Condition="'$(Platform)' == 'ARM64'">arm64-windows</KotonohaVcpkgTriplet>
    <KotonohaVcpkgPrefix>$(KotonohaVcpkgInstalled)$(KotonohaVcpkgTriplet)\</KotonohaVcpkgPrefix>
    <KotonohaVcpkgInclude>$(KotonohaVcpkgPrefix)include</KotonohaVcpkgInclude>
    <KotonohaVcpkgLib>$(KotonohaVcpkgPrefix)lib</KotonohaVcpkgLib>
    <KotonohaVcpkgBin Condition="'$(Configuration)' == 'Debug'">$(KotonohaVcpkgPrefix)debug\bin</KotonohaVcpkgBin>
    <KotonohaVcpkgBin Condition="'$(Configuration)' != 'Debug'">$(KotonohaVcpkgPrefix)bin</KotonohaVcpkgBin>
  </PropertyGroup>

  <PropertyGroup>
    <KotonohaIncludeDirs>$(KotonohaGeneratedDir);{include_dirs}$(KotonohaVcpkgInclude);$(KotonohaVcpkgInclude)\python3.14</KotonohaIncludeDirs>
    <KotonohaDefines>WIN32;_WINDOWS;KOTONOHA_ENABLE_UPSCALER=1;KOTONOHA_ENABLE_MCP=1;_CRT_SECURE_NO_WARNINGS</KotonohaDefines>
    <KotonohaLinkLibs>ass.lib;json-c.lib;avcodec.lib;avformat.lib;avutil.lib;swresample.lib;swscale.lib;avdevice.lib;avfilter.lib;iconv.lib;charset.lib;opengl32.lib;user32.lib;ws2_32.lib;kernel32.lib;gdi32.lib;winspool.lib;shell32.lib;ole32.lib;oleaut32.lib;uuid.lib;comdlg32.lib;advapi32.lib</KotonohaLinkLibs>
  </PropertyGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(KotonohaIncludeDirs);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>$(KotonohaDefines);%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <ExceptionHandling>Sync</ExceptionHandling>
      <RuntimeTypeInfo>true</RuntimeTypeInfo>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <WarningLevel>Level3</WarningLevel>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <ResourceCompile>
      <PreprocessorDefinitions>KOTONOHA_ICON_PATH=$([MSBuild]::NormalizePath('$(KotonohaRoot)', 'design/generated/generic/icon.ico').Replace('\','/'));KOTONOHA_DISPLAY_NAME=$(ProjectName);%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>$(KotonohaIncludeDirs);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ResourceCompile>
    <Link>
      <AdditionalLibraryDirectories>$(KotonohaVcpkgLib);$(KotonohaVcpkgLib)\$(Configuration);$(KotonohaVcpkgPrefix)debug\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>$(KotonohaLinkLibs);%(AdditionalDependencies)</AdditionalDependencies>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <SubSystem>Console</SubSystem>
      <AdditionalOptions Condition="'$(Platform)' == 'x64'">%(AdditionalOptions) /machine:x64</AdditionalOptions>
      <AdditionalOptions Condition="'$(Platform)' == 'Win32'">%(AdditionalOptions) /machine:x86</AdditionalOptions>
      <AdditionalOptions Condition="'$(Platform)' == 'ARM64'">%(AdditionalOptions) /machine:arm64</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>

  <!-- Win32/x64 link the native SDL3 / SDL3_ttf projects imported into the
       solution. The upstream SDL3_ttf .vcxproj has no ARM64 configuration,
       so ARM64 uses the vcpkg SDL libraries instead. -->
  <ItemDefinitionGroup Condition="'$(Platform)' == 'ARM64'">
    <Link>
      <AdditionalDependencies>SDL3.lib;SDL3_ttf.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)' == 'Debug'">
    <Link>
      <AdditionalDependencies>zd.lib;python314_d.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)' != 'Debug'">
    <Link>
      <AdditionalDependencies>z.lib;python314.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <PreprocessorDefinitions>NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
'''

TARGETS = r'''<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- Runs once per project build; the script is idempotent and keeps the
       header timestamp when Git metadata has not changed. -->
  <Target Name="KotonohaGenerateVersion" BeforeTargets="ClCompile">
    <Exec Command="python &quot;$(KotonohaVSRoot)Scripts\GenerateBuildVersion.py&quot; --out &quot;$(KotonohaVSRoot)generated&quot;" />
  </Target>
  <Target Name="KotonohaDeployVcpkgRuntime" AfterTargets="Build" Condition="'$(ConfigurationType)' == 'Application'">
    <Exec Condition="Exists('$(KotonohaVcpkgBin)')" Command="xcopy /Y /Q /I &quot;$(KotonohaVcpkgBin)\*.dll&quot; &quot;$(OutDir)&quot;" IgnoreExitCode="true" />
    <Exec Condition="'$(Platform)' != 'ARM64' And Exists('$(SolutionDir)$(Platform)\$(Configuration)\SDL3.dll')" Command="xcopy /Y /Q /I &quot;$(SolutionDir)$(Platform)\$(Configuration)\SDL3*.dll&quot; &quot;$(OutDir)&quot;" IgnoreExitCode="true" />
  </Target>
</Project>
'''

RC = r'''// Shared Win32 resource script. Keep the macros unquoted: MSBuild's rc.exe
// task wraps /D values in quotes and re-escaping them breaks rc.exe (RC1107).
#ifndef KOTONOHA_ICON_PATH
#define KOTONOHA_ICON_PATH design/generated/generic/icon.ico
#endif
#ifndef KOTONOHA_DISPLAY_NAME
#define KOTONOHA_DISPLAY_NAME Kotonoha
#endif
#define KOTONOHA_STR(x) #x
#define KOTONOHA_XSTR(x) KOTONOHA_STR(x)
#define VOS__WINDOWS32 0x00040004L
#define VFT_APP 0x1L
#define VFT2_UNKNOWN 0x0L

1 ICON KOTONOHA_ICON_PATH

VS_VERSION_INFO VERSIONINFO
 FILEVERSION 0,1,0,0
 PRODUCTVERSION 0,1,0,0
 FILEFLAGSMASK 0x3fL
 FILEOS VOS__WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "Kotonoha contributors"
            VALUE "FileDescription", KOTONOHA_XSTR(KOTONOHA_DISPLAY_NAME)
            VALUE "FileVersion", "0.1.0"
            VALUE "ProductName", KOTONOHA_XSTR(KOTONOHA_DISPLAY_NAME)
            VALUE "ProductVersion", "0.1.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
'''


SOLUTION_FOLDER_TYPE = '{2150E333-8FDC-42A3-9474-1A3956D46DE8}'
CPP_PROJECT_TYPE = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'

# Solution Explorer folders (name -> parent folder). Categories mirror the
# repository layout: lib/, games/, modules/ and launcher/.
SOLUTION_FOLDERS = {
    'lib': None,
    'games': None,
    'modules': None,
    'vms': 'modules',
    'providers': 'modules',
    'formats': 'modules',
    'launcher': None,
    'external': None,
}


def folder_guid(name):
    value = uuid.uuid5(uuid.NAMESPACE_URL, 'kotonoha/vs/folder/' + name)
    return '{' + str(value).upper() + '}'


def project_folder(target):
    name = target['name']
    if name in ('KotonohaEngine', 'KotonohaImGui'):
        return 'lib'
    if target.get('module'):
        return target['module'].split('/')[1]
    if target.get('game'):
        return 'games'
    if name in ('KotonohaLauncher', 'KotonohaLauncherCore'):
        return 'launcher'
    return None


def write_solution(data):
    lines = [
        'Microsoft Visual Studio Solution File, Format Version 12.00',
        '# Visual Studio Version 18',
        'VisualStudioVersion = 18.0.0.0',
        'MinimumVisualStudioVersion = 10.0.40219.1',
    ]
    for folder, parent in SOLUTION_FOLDERS.items():
        lines.append(f'Project("{SOLUTION_FOLDER_TYPE}") = "{folder}", "{folder}", '
                     f'"{folder_guid(folder)}"')
        lines.append('EndProject')
    children = {}
    for target in data['targets']:
        name = target['name']
        rel = 'Projects\\' + name + '\\' + name + '.vcxproj'
        lines.append(f'Project("{CPP_PROJECT_TYPE}") = '
                     f'"{name}", "{rel}", "{project_guid(name)}"')
        lines.append('EndProject')
        folder = project_folder(target)
        if folder:
            children[project_guid(name)] = folder
    for native, info in data['native_projects'].items():
        lines.append(f'Project("{CPP_PROJECT_TYPE}") = '
                     f'"{native}", "..\\..\\{info["path"]}", "{info["guid"]}"')
        lines.append('EndProject')
        children[info['guid']] = ('launcher' if native == 'KotonohaWinUI'
                                  else 'external')
    lines.append('Global')
    lines.append('\tGlobalSection(NestedProjects) = preSolution')
    for folder, parent in SOLUTION_FOLDERS.items():
        if parent:
            lines.append(f'\t\t{folder_guid(folder)} = {folder_guid(parent)}')
    for guid, folder in children.items():
        lines.append(f'\t\t{guid} = {folder_guid(folder)}')
    lines.append('\tEndGlobalSection')
    lines.append('\tGlobalSection(SolutionConfigurationPlatforms) = preSolution')
    combos = []
    for cfg in data['configurations']:
        for plat in data['platforms']:
            combos.append(f'{cfg}|{plat}')
            lines.append(f'\t\t{cfg}|{plat} = {cfg}|{plat}')
    lines.append('\tEndGlobalSection')
    lines.append('\tGlobalSection(ProjectConfigurationPlatforms) = postSolution')
    for target in data['targets']:
        guid = project_guid(target['name'])
        for combo in combos:
            lines.append(f'\t\t{guid}.{combo}.ActiveCfg = {combo}')
            lines.append(f'\t\t{guid}.{combo}.Build.0 = {combo}')
    for native, info in data['native_projects'].items():
        guid = info['guid']
        for combo in combos:
            cfg, plat = combo.split('|', 1)
            supported = plat in info['platforms']
            mapped = combo if supported else f'{cfg}|x64'
            lines.append(f'\t\t{guid}.{combo}.ActiveCfg = {mapped}')
            if supported:
                lines.append(f'\t\t{guid}.{combo}.Build.0 = {mapped}')
    # Solution folders need configuration mappings too; without them VS marks
    # the folder as "incompatible" even though the GUID is correct.
    for folder in SOLUTION_FOLDERS:
        guid = folder_guid(folder)
        for combo in combos:
            lines.append(f'\t\t{guid}.{combo}.ActiveCfg = {combo}')
            lines.append(f'\t\t{guid}.{combo}.Build.0 = {combo}')
    lines.append('\tEndGlobalSection')
    lines.append('\tGlobalSection(SolutionProperties) = preSolution')
    lines.append('\t\tHideSolutionNode = FALSE')
    lines.append('\tEndGlobalSection')
    lines.append('EndGlobal')
    (VS / 'Kotonoha.sln').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def props_text(data):
    dirs = ''.join(
        f'$(KotonohaRoot){directory.replace("/", "\\")};'
        for directory in data.get('include_dirs', []))
    return PROPS_TEMPLATE.format(include_dirs=dirs)


def generate():
    data = load_manifest()
    (VS / 'Directory.Build.props').write_text(props_text(data), encoding='utf-8')
    (VS / 'Directory.Build.targets').write_text(TARGETS, encoding='utf-8')
    (VS / 'Resources').mkdir(exist_ok=True)
    (VS / 'Resources/Kotonoha.rc').write_text(RC, encoding='utf-8')
    for target in data['targets']:
        write_vcxproj(target, data)
        write_filters(target, data)
    write_solution(data)
    print(f'[Kotonoha] Generated Kotonoha.sln with {len(data["targets"])} projects')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export-from-cmake', metavar='BUILD_DIR')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.export_from_cmake:
        export_from_cmake(args.export_from_cmake)
    elif args.check:
        check()
    else:
        generate()


if __name__ == '__main__':
    main()
