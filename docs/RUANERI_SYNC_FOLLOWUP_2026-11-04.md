# Ruaneri sync follow-up (2026-11-04)

The Kotonoha v3 branch `kotonoha-v3-ruaneri-sync-transitions` now also contains
Ruaneri `main` through `9e751c8` (including `762f798`, `52e1959`, the
School Days transition PR, `1306863`, and `9e751c8`). The
`kotonoha-main` backup branch was not changed. The separate School Days
transition proposal is [Ruaneri-Portela/Kotonoha#8](https://github.com/Ruaneri-Portela/Kotonoha/pull/8),
which Ruaneri merged into `main`. It was limited to `games/school-days`.

## Merge reconciliation

- School Days retains its v3 display defaults, startup diagnostics, and
  translated script overlay while accepting the upstream ISO path handling.
- The upstream Retina subtitle check was retained. The test's clock label was
  updated to the upstream `Node clock` wording.
- The shared splash screen retains v3's entry and exit fade while drawing the
  upscaled texture introduced upstream.
- File discovery now checks the upstream directory layout and retains the v3
  fallback for existing game paths and Android data.
- The launcher stores a portable name when that name resolves to the same
  directory; external game installations retain their full path. A regression
  test covers the external case.
- A Snow Radish fallback path now uses its explicit game identifier after the
  merged code removed the former local variable.
- The new ISO Unicode test uses a UTF-8 literal so it works under the Windows
  MSVC code page. The ISO decoder was already producing the correct bytes.
- Windows packaging includes the selected Python interpreter's directory
  when scanning Doki Doki's runtime dependencies. This shared build-system
  change was kept separate from the game merge.

## Verification completed

- Release build `build/school-days-dev-control2`: succeeded when built
  serially. The first parallel build hit vcpkg DLL copy contention (exit 32).
- `ctest --test-dir build/school-days-dev-control2 -C Release
  --output-on-failure --timeout 120`: **55/55 passed** after the merge.
- Release `SchoolDays.exe` in `build/school-days-dev-control4`: rebuilt and
  ran five frames from node 0 with the local original School Days HQ data;
  exited with code 0 using the software renderer.
- Separate Release application builds succeeded for Zero no Tsukaima, Snow
  Radish, Summer Radish, Doki Doki, the example, and School Days. The Doki
  Doki build completed after the Python runtime directory fix.
- The upstream transition PR built against `52e1959`. Its deterministic
  transition test and retail-asset runtime test both passed, covering EXIT
  cancel and confirmation, TOPMENU confirmation, and the delayed quit event.

RealLive configuration remains unverified here because its CMake configuration
requires an Iconv installation not present in this Windows environment.
Amagami's Visual Studio project reports a case-insensitive target dependency
cycle between `Amagami` and `amagami`; it was not rebuilt. These are build
coverage limits, not evidence of runtime behavior. The original commercial
assets were used only locally and are not included in either branch.

## After the upstream PR merge

- The PR's School Days runtime and timing tests were added to the v3 test
  configuration. The v3 `openPanel(14)` debug action was corrected to use the
  same TOPMENU confirmation as the actual menu-bar action, instead of
  returning immediately to the title. Existing v3 transition rendering was
  retained when resolving the duplicated upstream implementation.
- Release `build/school-days-dev-control2` compiled, and its complete test
  suite passed **57/57**, including the new `SchoolDays.ExitRuntime` test with
  local retail assets.
- Release `build/school-days-dev-control4/games/school-days/Release/SchoolDays.exe`
  rebuilt and ran five frames from node 0 against the original installation,
  exiting with code 0. FFmpeg emitted a WMV pixel-format probe warning during
  the short smoke run; the media test in the 57-test suite passed.
- The final merged revision also compiled the Release executables for Zero no
  Tsukaima, Snow Radish, Summer Radish, Doki Doki, the example and School Days
  in a separate multi-game configuration. RealLive and Amagami retain the Windows
  configuration limits described above.
