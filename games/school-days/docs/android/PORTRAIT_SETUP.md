# School Days HQ on Android — portrait setup

The School Days launcher opens in portrait. On first use, select the folder that contains `Packs`, `game-assets` and `translations`. Android grants read access to that tree. **Disconnect folder** releases the selection without changing the files. Android Back closes Options before leaving setup.

## Windows reference and Android entry

The Windows launcher displays the user's `SchoolDaysHQ-Launcher.png` with the logo already in the picture. Its setup header crops a second view from the same texture. ImGui draws the buttons and panels. The music toggle controls the loop named by `TitleBGM` in `STARTSCRIPT.INI`. The main actions are Play, Options and Quit. Windows Options includes Display, Graphics, Audio, General, Language and Game Files. Engine settings and the retail preferences use separate storage. [Windows implementation](../../src/SchoolDaysLauncher.cpp)

The Android flavor uses the same light blue header, dark blue text and main actions. Setup looks for the PNG under `game-assets/launcher-art/`, `assets/`, then `launcher-art/` in the selected tree. The optional image fits the available width with its original proportions. The text header covers first use, missing artwork and decode errors. The local Windows image has no established redistribution permission for this APK.

Play opens file setup when the source cannot be used. Game Files checks the KTRF filename and the 29 HQ pack names plus L×H using directory metadata. Language lists complete HQ/L×H JSON pairs found under `translations/jsons`. Inside `Packs`, the launcher recognizes case variants such as `System.gpk`. Directory, route and translation names still use exact spelling. An unavailable provider or expired grant leaves the picker and retry controls accessible. With a complete tree, choose a detected language and press Play. Setup remembers the language and asks again if that pair is unavailable in a different tree. An existing private installation under `game_assets` with the route and packs can hand off to the existing bootstrap on explicit Play. A separate QA installation exercised this handoff using one-byte synthetic archive stubs. The native parser rejected them. Two consecutive Play attempts returned to setup with a localized error. SDL reads its activity-recreation and Back settings from application-level metadata.

## Engine and storage boundaries

- Generic Android Java code owns tree selection, persisted read grants, directory queries and inline startup feedback. The School Days folder layout and locale pairs belong to the game's Android source set.
- The packaged `GameActivity` retains the shared C++ runtime. The launcher and game have independent orientation policies. Gameplay follows the device setting in the manifest. SDL orientation, letterboxing, input, video surfaces and audio still need gameplay QA.
- On explicit Play, the game receives a read-only `IMount`. It maps the canonical pack names to the document URIs returned by the provider. SDL opens an independent seekable `FileHandle` for the index, decoded entries and raw media streams. The retail parser validates all 30 archives. A revoked grant, changed length or non-seekable source fails without importing the archive.
- Consumers that require a physical file use an app-private cache directory for the current launch. It contains the user-supplied KTRF, the selected HQ and L×H JSONs, a URI catalog and any of the six supported Display Options files found in `overlay/System/Option/`. The launcher passes the overlay to the shared asset resolver and discards the session on return. An interrupted process may leave temporary files in Android's cache. Saves continue to use their existing private paths.
- The mobile first-run profile uses `renderScale=0.75`, Linear and a factor of 1. The scaled render target already uses linear sampling. Stored settings retain precedence. Cache budgets and performance on a 4 GB device require measurements.
- The native game keeps its existing `StoragePaths` and `FileResolver` paths. The setup preference contains only the selected tree URI in app-private storage. Saves remain outside SAF.
- The APK asset allowlist includes the engine shaders and their notices. It excludes the optional engine splash, the launcher PNG, the user route, translations and game packs. The A1 dependency graph contains fontconfig and libunibreak. The APK has no `fonts.conf` or production TTF. `SubtitleScene` registers fonts from `FileResolver` with libass. When the bundled default font is missing, Android resolves a system font for Japanese through its font APIs. The launcher uses Android UI fonts. Subtitle glyph coverage and collection face selection still need visual gameplay QA.

## Reproduce with synthetic data

1. From the repository root, run `python tools/android/make_school_days_setup_fixture.py build/SchoolDaysSetupQA`. Supply a new directory each time. It contains a 2:1 test PNG, a route filename and synthetic JSONs. It is suitable for metadata tests only.
2. From `buildsystems/android`, run `gradle :app:assembleSchoolDaysDebug -Pabis=arm64-v8a -PbundleGameAssets=false --no-daemon --console=plain`. Use the pinned Android NDK and the host pkgconf executable used by the vcpkg environment.
3. Run `python tools/android/check_school_days_apk.py <apk>` from the root and `zipalign -c -P 16 4 <apk>`. Install using `adb install -r <apk>`.
4. Put only the new fixture in a new test folder on the phone. Open setup, cancel the picker once, then select the fixture. Expect a missing Packs message, an undistorted image and only `pt-BR` in Language. The English fixture intentionally lacks its L×H pair.
   In the synthetic fixture only, add a one-byte file named `Packs/System.gpk` and tap **Check again**. The missing list should contain `Script.GPK` and omit `System.GPK`. With a zero-byte `System.gpk`, both should appear.
5. Force-stop and reopen the app. Check the same selection, then use Disconnect folder and open the picker again. Check Android Back and Quit. Keep real game data outside this procedure.

For native failure QA, add `-I ../../tools/android/school-days-setup-qa.init.gradle` to the same Gradle command in `buildsystems/android`. This produces `me.hirameki.kotonoha.schooldays.setupqa` with separate app storage. In this QA profile, use a dummy route and one-byte stubs for the expected filenames under the private `game_assets/Packs`. Press Play twice, returning from Options each time. Each native validation failure must return to the portrait setup screen. On the POCO both attempts passed. An in-app probe also opened the system-selected Japanese font and verified its sfnt header (32,355,424 bytes). This test did not render subtitles.

For native mount QA, add `--pack-writer <SchoolDaysGpkStreamingTest>` to the fixture generator. Select the new fixture in the QA app, choose `pt-BR`, return and press Play. On the POCO, the shared engine mounted all 29 HQ packs and L×H. It then rejected the deliberately short KTRF and returned to setup. A temporary device probe read the 28 synthetic L×H entries and a BGM entry at four offsets each, and decoded the compressed Movie00 entry. The same device probe resolved the synthetic Display Options PNG through the native overlay, checked both selected JSON files and found no GPK in the launch cache. A truncated synthetic pack produced the pack-specific error in setup. The final APKs exclude the probe. The Windows streaming test also reads from a mount whose underlying archives lie outside the physical game root.

## POCO X7 Pro — synthetic S0

Three cycles passed on API 36 with the local storage provider. After the phone rebooted, the S0 app reopened the same synthetic 1 GiB document with its existing read grant. All 1024 blocks matched the private reference in each pass. The post-reboot history contained the original 12 measurements on opening and replayed all 18 after the retest. The table gives the 95th percentile per 1 MiB read in milliseconds. [All measurements by cycle](s0-poco-api36.csv)

| Read order | SAF first / app reopened / phone rebooted | Private first / app reopened / phone rebooted |
|---|---:|---:|
| Forward | 1.125 / 0.469 / 2.479 | 1.337 / 0.473 / 2.062 |
| Reverse | 0.795 / 0.500 / 2.063 | 0.806 / 0.539 / 1.987 |
| Strided | 0.540 / 0.508 / 0.630 | 0.589 / 0.506 / 0.673 |

The earlier attempt through Downloads returned `different_document_refused`. The successful cycles used the device storage root in both pickers. The provider supported direct reads on its seekable descriptor across all three cycles. Reopening through `/proc/self/fd` returned errno 13. The S0 app retains its two synthetic files (about 2 GiB total). These results support testing in-place reads from this provider through the engine mount. Other providers and native gameplay still require testing.

## Remaining acceptance work

1. Obtain separate authorization before testing real GPKs.
2. Validate an authorized full route, translation coverage, overlay precedence, renderer and audiovisual playback on the device.
3. Bind remaining Windows options to the shared settings. Verify contextual touch with `MobileTouchAdapter`, `TouchPointer` and the original hotspots.
4. Exercise Route Map, BackLog, Save/Load, MenuBar, choices, media, pause/resume and GPU resets with isolated profiles. Measure FPS, memory and subtitle readability on representative hardware.
