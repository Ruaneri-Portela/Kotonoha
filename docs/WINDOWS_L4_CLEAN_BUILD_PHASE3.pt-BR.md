# Windows L4: build limpa do School Days e baseline CTest da Fase 3

Escopo: somente Windows x64. O procedimento nao executa o programa retail nem modifica GPKs originais, saves particulares, configuracoes pessoais ou staging instalado. Nao versionar arquivos comerciais. Os metadados de arte podem ser rastreados; a arte privada local nao deve ser publicada.

## Pre-requisitos

Utilize Visual Studio 2026 Build Tools com MSVC, CMake, toolchain do vcpkg e dependencias `x64-windows-static-md` disponiveis no cache local. O exemplo exige uma **copia privada** da pasta `vcpkg_installed` previamente validada em um build Windows L4. Nao altere a instalacao original nem o checkout compartilhado do vcpkg.

Substitua `<vcpkg>` pelo checkout local do vcpkg e `<cache-binario>` pelo diretorio dos arquivos binarios em cache. No PowerShell, na raiz do repositorio:

```powershell
$repo = (Get-Location).Path
$vcpkg = '<vcpkg>'
$archiveCache = '<cache-binario>'
$previous = Join-Path $repo 'build/school-days-l4-two-dll-staticmd/vcpkg_installed'
$dependencies = Join-Path $repo 'build/l4-private-deps/vcpkg_installed'
$build = Join-Path $repo 'build/school-days-l4-clean'
$manifest = Join-Path $repo 'games/school-days/tools/two-dll-vcpkg'

# Copie uma unica vez, para destino privado novo. Nao sobrescreva copias existentes.
if (Test-Path $dependencies) { throw 'Dependencias privadas ja existem' }
New-Item -ItemType Directory -Force (Split-Path $dependencies) | Out-Null
Copy-Item -LiteralPath $previous -Destination $dependencies -Recurse

$releaseLib = Join-Path $dependencies 'x64-windows-static-md/lib'
$pkgconf = Join-Path $dependencies 'x64-windows/tools/pkgconf/pkgconf.exe'
$env:VCPKG_ROOT = $vcpkg
$env:VCPKG_DEFAULT_BINARY_CACHE = $archiveCache
$env:VCPKG_BINARY_SOURCES = "clear;files,$archiveCache,read"
$env:PKG_CONFIG_LIBDIR = Join-Path $releaseLib 'pkgconfig'
Remove-Item Env:PKG_CONFIG_PATH -ErrorAction SilentlyContinue

cmake -S . -B $build -G 'Visual Studio 18 2026' -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
  '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' `
  "-DVCPKG_MANIFEST_DIR=$manifest" `
  "-DVCPKG_INSTALLED_DIR=$dependencies" `
  '-DVCPKG_MANIFEST_INSTALL=OFF' `
  "-DPKG_CONFIG_EXECUTABLE=$pkgconf" `
  '-DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=OFF' `
  "-Dharfbuzz_LIBRARY=$releaseLib/harfbuzz.lib" `
  "-Dplutosvg_LIBRARY=$releaseLib/plutosvg.lib" `
  "-Dplutovg_LIBRARY=$releaseLib/plutovg.lib" `
  '-DKOTONOHA_BUILD_SCHOOL_DAYS=ON' `
  '-DKOTONOHA_BUILD_AIR=OFF' '-DKOTONOHA_BUILD_AMAGAMI=OFF' `
  '-DKOTONOHA_BUILD_ZERO=OFF' '-DKOTONOHA_BUILD_SNOW=OFF' `
  '-DKOTONOHA_BUILD_SUMMER=OFF' '-DKOTONOHA_BUILD_DDLC=OFF' `
  '-DKOTONOHA_BUILD_EXAMPLE=OFF' '-DKOTONOHA_BUILD_MACOS_BUNDLES=OFF' `
  '-DKOTONOHA_STATIC_ENGINE=ON' '-DKOTONOHA_STATIC_IMGUI=ON' `
  '-DKOTONOHA_DEV_CONTROL=OFF' '-DKOTONOHA_DISTRIBUTION_BUILD=OFF' `
  '-DKOTONOHA_ENABLE_PLAYER_DUBBING=OFF' `
  '-DKOTONOHA_SCHOOL_DAYS_RETAIL_ONLY=OFF' `
  '-DKOTONOHA_BUNDLE_LOCAL_GAME_ASSETS=ON' `
  '-DSDL_SHARED=ON' '-DSDL_STATIC=OFF' '-DBUILD_TESTING=ON'

cmake --build $build --config Release --parallel 1
```

O parametro `VCPKG_MANIFEST_DIR` usa o manifesto **do School Days**, pois o manifesto da raiz e diferente. Com `VCPKG_MANIFEST_INSTALL=OFF`, a configuracao usa os pacotes privados instalados: nao baixa nem instala dependencias. Se precisar restaurar pacotes em outra etapa, utilize apenas o cache binario indicado e as restricoes `--only-binarycaching --no-downloads` do vcpkg.

### Motivo da selecao explicita das bibliotecas Release

No configure limpo que falhou, o Find do SDL3_ttf encontrou `debug/lib/harfbuzz.lib` mesmo para o alvo Release (`/MD`). `dumpbin /symbols` identificou `__imp__CrtDbgReport` apenas no HarfBuzz Debug, que pede `MSVCRTD`. A biblioteca Release pede `MSVCRT`. A escolha explicita de `lib/harfbuzz.lib`, `lib/plutosvg.lib` e `lib/plutovg.lib` impede misturar Debug e Release. As bibliotecas originais e restauradas tinham o mesmo SHA-256: o defeito estava na resolucao dos caminhos, nao no conteudo dos arquivos.

## Validacao e baseline CTest conhecido

Compare o SHA-256 do arquivo local `games/school-days/Release/assets/SchoolDaysHQ-Launcher.png` com `games/school-days/game-assets/launcher-art/SchoolDaysHQ-Launcher.png`, e todos os arquivos do staging `games/school-days/Release/display-options/` com os correspondentes em `games/school-days/game-assets/display-options/`. Arte privada ausente deve gerar aviso; nao substitua silenciosamente por arte comercial.

```powershell
python -B tools/ValidateBuildMetadata.py
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
ctest --test-dir $build -C Release --output-on-failure --parallel 4 --timeout 15
ctest --test-dir $build -C Release --output-on-failure --parallel 1 --timeout 30 `
  -R '^(Kotonoha\.PackageAssets|Kotonoha\.BundleContents|SchoolDaysAssetsListTest|SchoolDaysDisplayPolicyTest|SchoolDaysConfigMenuDisplayTest|SchoolDays\.DisplayNarrativeMarkup|SchoolDays\.TranslationLayout)$'
python -B games/school-days/tools/stress_l4.py --build-dir $build --cycles 0 --smokes 36 --timeout 25
```

Em 2026-11-14, o CTest completo apresentou **47/74 aprovados**, com **17 timeouts** e **10 nao executados por dependencia**. O build L4 original apresentou **exatamente os mesmos 27 testes com os mesmos nomes e status**, sob os mesmos drivers SDL dummy, timeout de 15 segundos por teste e paralelismo. E um baseline conhecido, **nao um CTest integral aprovado**. Os sete testes focados em assets/configuracao passaram **7/7**. O stress com software/headless isolado passou **36/36**, com `--documents` e `--config` especificos por fixture sintetica. `--cycles 0` evita repetir a suite CTest ja executada separadamente. Nada disso comprova GPU real nem QA visual/manual de jogo.
