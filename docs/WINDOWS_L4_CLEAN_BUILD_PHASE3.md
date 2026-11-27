# Windows L4: clean School Days build and Phase 3 CTest baseline

Scope: Windows x64 only. These steps do not execute the retail program or alter retail GPKs, private saves, user settings, or installed staging. Never copy proprietary game archives into version control. The source tree contains optional artwork metadata; local artwork is not distributed.

## Prerequisites

Use Visual Studio 2026 Build Tools with MSVC, CMake, the vcpkg toolchain and its locally cached `x64-windows-static-md` dependencies. The example requires a **private copy** of a previously verified `vcpkg_installed` directory (from a working Windows L4 build). Do not modify the source installation or the shared vcpkg checkout.

Replace `<vcpkg>` with the local vcpkg checkout and `<cache-binario>` with the local binary archive directory. In PowerShell, at the repository root:

```powershell
$repo = (Get-Location).Path
$vcpkg = '<vcpkg>'
$archiveCache = '<cache-binario>'
$previous = Join-Path $repo 'build/school-days-l4-two-dll-staticmd/vcpkg_installed'
$dependencies = Join-Path $repo 'build/l4-private-deps/vcpkg_installed'
$build = Join-Path $repo 'build/school-days-l4-clean'
$manifest = Join-Path $repo 'games/school-days/tools/two-dll-vcpkg'

# Copy once to a new, private destination. Do not overwrite any existing copy.
if (Test-Path $dependencies) { throw 'Private dependencies already exist' }
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

Use the School Days-specific `VCPKG_MANIFEST_DIR` shown above: the repository root manifest is different. With `VCPKG_MANIFEST_INSTALL=OFF`, the configure must use the private installed packages; it does not download or install packages. If dependencies must be restored separately, use only the specified local binary cache and vcpkg's `--only-binarycaching --no-downloads` controls.

### Why the Release library overrides matter

In the failed clean configure, SDL3_ttf's CMake Find modules resolved `debug/lib/harfbuzz.lib` even when the target was Release (`/MD`). `dumpbin /symbols` found the unresolved `__imp__CrtDbgReport` only in the Debug HarfBuzz library, whose default runtime is `MSVCRTD`. The Release library has `MSVCRT`; the matching `lib/harfbuzz.lib`, `lib/plutosvg.lib` and `lib/plutovg.lib` overrides avoid mixing Debug and Release. The original and newly restored archives had identical SHA-256 values: this was path selection, not library corruption.

## Validation and known CTest baseline

Compare SHA-256 of locally staged `games/school-days/Release/assets/SchoolDaysHQ-Launcher.png` with `games/school-days/game-assets/launcher-art/SchoolDaysHQ-Launcher.png`, and compare every file under `games/school-days/Release/display-options/` with the corresponding `games/school-days/game-assets/display-options/` file. Missing private artwork should be reported, not silently replaced with commercial material.

```powershell
python -B tools/ValidateBuildMetadata.py
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
ctest --test-dir $build -C Release --output-on-failure --parallel 4 --timeout 15
ctest --test-dir $build -C Release --output-on-failure --parallel 1 --timeout 30 `
  -R '^(Kotonoha\.PackageAssets|Kotonoha\.BundleContents|SchoolDaysAssetsListTest|SchoolDaysDisplayPolicyTest|SchoolDaysConfigMenuDisplayTest|SchoolDays\.DisplayNarrativeMarkup|SchoolDays\.TranslationLayout)$'
python -B games/school-days/tools/stress_l4.py --build-dir $build --cycles 0 --smokes 36 --timeout 25
```

On 2026-11-14, the full CTest run was **47/74 passing**, with **17 timeouts** and **10 tests not run due to dependencies**. The original L4 build had **exactly the same 27 failing/not-run tests, with the same names and statuses**, under the same dummy SDL drivers, 15-second per-test timeout and parallelism. This is a known baseline, **not a green full-suite result**. The seven asset/configuration-focused tests passed **7/7**. The isolated software/headless stress passed **36/36**, with `--documents` and `--config` isolated per synthetic fixture. `--cycles 0` avoids rerunning the already separately checked CTest suite. Neither real-GPU testing nor visual/manual gameplay QA is implied.
