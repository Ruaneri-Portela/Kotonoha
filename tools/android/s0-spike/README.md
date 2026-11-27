# Android S0 — isolated synthetic FD/SAF spike

This is a **separate test APK**, not the School Days game APK. It does not
depend on Kotonoha's engine, CMake, NDK, native libraries, retail GPKs, saved
games, or the native Android A1 build. Its application ID is
`org.kotonoha.schooldays.safspike`.

## Source / requirements

- `app/src/main/java/org/kotonoha/schooldays/safspike/S0StorageReadProbeActivity.java`
- `app/src/main/AndroidManifest.xml`
- AGP **8.13.2**, Gradle **8.14.6** (or compatible), JDK **17**, Android
  compile SDK **36**, min API **30**, target API **34**.
- No Android storage permissions or dependencies are required. All storage
  outside the app-private synthetic baseline is accessed through SAF or through
  a MediaStore record created and owned by this test app.

## Build and manual run — for the prime agent (not executed in this task)

From `tools/android/s0-spike/` on a machine with Gradle and the Android SDK:

```powershell
gradle :app:assembleDebug --no-daemon
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.kotonoha.schooldays.safspike/.S0StorageReadProbeActivity
adb logcat -s KTN_S0:I '*:S'
```

The standalone APK has its own launcher icon; unlike the initial variant-only
idea, it does not add any launcher or Activity to the School Days app. The
source set under the game app was discarded before this standalone handoff.

## Manual workflow

1. **SAF:** Tap **Start synthetic SAF probe**, choose a destination through
   `ACTION_CREATE_DOCUMENT`, and let the app write its own 1 GiB synthetic
   document plus an identical 1 GiB app-private baseline. The app then opens
   `ACTION_OPEN_DOCUMENT`. Select **only the document just created**. It
   rejects a different `content://` document ID *before opening it*. This
   system picker selects a **file, not a directory/tree**; never select a game
   folder, a GPK or an unrelated personal file. No manual folder access is
   required.
2. The SAF path requests and checks a persisted **read** grant via
   `takePersistableUriPermission`, uses `ContentResolver.openFileDescriptor`,
   measures `Os.lseek` / `Os.pread` access, attempts to reopen the provider FD
   via `/proc/self/fd/<number>` (without resolving or logging that symlink's
   target). **The read grant is not released after a run.** Its URI and the
   generated app-private baseline are saved for reopening after app restart or
   phone reboot.
3. **MediaStore:** Alternatively, tap **Start MediaStore synthetic probe**.
   This creates a new app-owned item in `Downloads/KotonohaS0` using
   `MediaStore.Downloads`, writes the same synthetic baseline and provider
   bytes, publishes the item (`IS_PENDING=0`), and reads its FD directly.
   This mode **does not** test persisted SAF grants.
4. In each mode, the test performs **three whole-file byte comparisons**:
   forward sequential, reverse 1 MiB blocks, and strided blocks (all 1024
   offsets exactly once). The **single `KTN_S0` log tag** emits one measurement
   for the provider (`mode=saf` or `mode=mediastore`) and one for
   `mode=private` per repetition. The fields are `test`, `mode`,
   `access=sequential|random`, `pattern=forward|reverse|strided`,
   `repetition=1|2|3`, `block_bytes`, `blocks`, `bytes`, `equal`, `read_ms`,
   `throughput_mib_s`, `lat_p50_ms`, and `lat_p95_ms`. The percentiles use the
   nearest-rank method across 1024 individual 1 MiB read operations per mode
   and repetition. The same tag records persisted-grant availability (SAF),
   `/proc/self/fd` reopening status, and result codes.
   Each measurement also has a random `metric_id` and `historic=false` when
   first recorded. The ID contains no device/user information.
5. Once all three rounds finish, **do not press Delete**. Close/reopen the app,
   then press **Retest saved synthetic document (after app/reboot)**. It reads
   the *same* two files in three full rounds, without regenerating or copying
   them. Reboot the phone, reopen the app, and press **Retest** again. For SAF,
   the button verifies that the persisted read grant survived reboot; if not,
   `persisted_read_grant_on_retest=false` appears under `KTN_S0` and the
   system picker requests **only the same synthetic file** again. Run SAF and
   MediaStore separately; the project keeps one synthetic session at a time.
6. **The phone reboot clears logcat, not S0 history.** On the next app launch,
   previously saved measurements are re-emitted under `KTN_S0` with
   `historic=true replay=on_open` and the **same `metric_id`** as their original
   `historic=false replay=none` line. If logcat was not connected at launch,
   press **Replay saved metrics to KTN_S0 logcat** to emit
   `historic=true replay=manual`. **When consolidating across cycles, keep
   only one record per `metric_id`**; never add original and replayed rows
   as separate measurements. Replaying does not run a benchmark or access GPKs.

All output is fixed metrics or predefined failure codes. No selected URIs,
pathnames, payload bytes, document names, or user data are logged. No GPKs or
game saves are opened, copied, or changed. **Do not select a real file.**

## Storage and cleanup (including 32 GB phones)

- Writes are explicit; nothing allocates 1 GiB at app startup. The app checks
  for **at least 2.5 GiB free on internal storage** before writing. The private
  baseline and provider document each use 1 GiB, possibly on the same volume.
  A provider can require additional temporary/cache space; a 32 GB handset
  must have ample *free* space, not just a 32 GB nominal capacity.
- The test uses bounded 1 MiB generation blocks and two 1 MiB comparison
  buffers, not an in-memory 1 GiB archive. It refuses insufficient internal
  space instead of downscaling the requested test silently.
- Measurement-only history is saved separately in private SharedPreferences,
  synchronously before each live log line. **Maximum 96 entries, 32,768
  characters overall, and 384 characters per entry**; oldest entries are
  evicted first. A fixed-format validation accepts only synthetic test metrics
  and rejects URI, paths, document names, payload, or arbitrary log messages.
  Three complete read cycles for one mode normally produce 18 entries
  (2 origins × 3 patterns × 3 cycles), well below this bound.
- A successful run **does not delete either file or release the persisted SAF
  grant**. The user explicitly presses **Delete MY synthetic test files** when
  finished, which attempts to remove **only the test-created document** via
  `DocumentsContract.deleteDocument` (SAF) or `ContentResolver.delete`
  (MediaStore), then the private baseline and saved grant. **Delete also clears
  metric history**, even if a provider refuses to remove the document. Collect
  the three cycles and their replay before pressing Delete. Some SAF providers
  may deny deletion after reboot because the persisted grant is read-only; in
  that case delete the synthetic document in the system Files app and retry or
  clear the test app data. No independently selected document is deleted.

## Interpretation / limits

- A passing direct `openFileDescriptor` + `pread/lseek` test means **that
  provider on that device** supports random-access FD reads. Other providers
  may return pipes/proxy FDs (`ESPIPE`), virtual/cloud streams, or cached
  temporary files. This does not prove *physical* zero-copy or native C++
  GPK-provider integration. No native FD injection was implemented here.
- `/proc/self/fd` reopening is a separate, explicitly reported compatibility
  probe; it can fail even when direct `pread` works. Never assume a Java FD
  can be converted to a persistent native pathname without testing.
- SAF persisted-grant visibility is checked during first run and retest after
  app restart/reboot; actual persistence must be verified on the handset.
  Cloud providers, 32-bit offsets in upstream native GPK codecs, real-device
  thermal behavior, and gameplay still require separate QA.
- No build, install, emulator, or device test was executed by this worker.
