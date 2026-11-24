package me.hirameki.kotonoha.bridgetest;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import me.hirameki.kotonoha.bridgetest.JsonUtil.GameInfo;
import me.hirameki.kotonoha.launcher.KotonohaLauncherBridge;
import me.hirameki.kotonoha.launcher.NativeLibraries;

/**
 * Headless integration harness: exercises every native entry point exported by
 * KotonohaLauncherCore, including a recursive library scan and one in-process
 * launch per discovered game. No JavaFX window is required.
 *
 * <p>Uso: ./run-cli.sh [/Volumes/Server/data2/Games ...] [--dry-run]</p>
 */
public final class BridgeTestCli {
    private static final List<String> LOG = new ArrayList<>();

    public static void main(String[] args) {
        print("core=" + NativeLibraries.corePath());
        boolean loaded = KotonohaLauncherBridge.ensureLoaded();
        print("ensureLoaded=" + loaded);
        if (!loaded) {
            print("loadError=" + KotonohaLauncherBridge.getLoadError());
            flushLog();
            return;
        }

        boolean dryRun = "1".equals(
                System.getenv("KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN"));
        List<String> rootList = new ArrayList<>();
        for (String arg : args) {
            if ("--dry-run".equals(arg))
                dryRun = true;
            else if (!arg.startsWith("--"))
                rootList.add(arg);
        }
        String[] roots = rootList.isEmpty()
                ? new String[] {"/Volumes/Server/data2/Games"}
                : rootList.toArray(new String[0]);
        print("roots=" + String.join(" | ", roots) + " dryRun=" + dryRun);

        // 1. ScanRootsJson -- uma pasta raiz com multiplos jogos.
        String scan = KotonohaLauncherBridge.nativeScanRootsJson(
                roots, true, null);
        print("FEATURE ScanRootsJson => " + scan);
        List<GameInfo> games = JsonUtil.gameInfos(scan);
        print("SCAN count=" + games.size());

        // 2. DetectPathJson -- raiz e primeiro jogo detectado.
        print("FEATURE DetectPathJson(root) => "
                + KotonohaLauncherBridge.nativeDetectPathJson(roots[0], true, null));
        if (!games.isEmpty() && !games.get(0).path.isEmpty())
            print("FEATURE DetectPathJson(game) => "
                    + KotonohaLauncherBridge.nativeDetectPathJson(
                            games.get(0).path, true, null));

        // 3. SaveGameParams + LoadGameParamsJson.
        String profile = "bridge-test-profile";
        boolean saved = KotonohaLauncherBridge.nativeSaveGameParams(
                profile, "", "", "", "", "", "", "", "", "", "", "");
        print("FEATURE SaveGameParams => " + saved);
        print("FEATURE LoadGameParamsJson => "
                + KotonohaLauncherBridge.nativeLoadGameParamsJson(profile));

        // 4. UpscalePresetsJson.
        print("FEATURE UpscalePresetsJson => "
                + KotonohaLauncherBridge.nativeUpscalePresetsJson());

        // 5. DefaultSchoolDaysRoute + ValidateRoute.
        String route = KotonohaLauncherBridge.nativeDefaultSchoolDaysRoute();
        print("FEATURE DefaultSchoolDaysRoute => " + route);
        if ((route == null || route.isEmpty() || !new File(route).isFile())) {
            String bundled = NativeLibraries.repo()
                    + "/games/school-days/game-assets/school-days-hq.ktnroute";
            if (new File(bundled).isFile()) route = bundled;
        }
        if (route != null && !route.isEmpty() && new File(route).isFile())
            print("FEATURE ValidateRoute => "
                    + KotonohaLauncherBridge.nativeValidateRoute(route));
        else
            print("FEATURE ValidateRoute => SKIPPED (no bundled route)");

        // 6. ResolveSaveDirectory.
        print("FEATURE ResolveSaveDirectory => "
                + KotonohaLauncherBridge.nativeResolveSaveDirectory(
                        roots[0], "school-days", profile, true, null));

        // 7. LaunchGame -- uma launch por jogo, com a request completa do scan.
        int ok = 0, failed = 0;
        for (GameInfo game : games) {
            if (game.path.isEmpty()) continue;
            LaunchRequest request = LaunchRequest.from(game).split(true)
                    .dryRun(dryRun).build();
            String result = request.launch();
            boolean good = result != null && result.contains("\"ok\":true");
            if (good) ++ok; else ++failed;
            print("LAUNCH " + (good ? "OK   " : "FAIL ") + game.path
                    + " playable=" + JsonUtil.boolField(result, "playable")
                    + " => " + result);
        }
        print("LAUNCH ok=" + ok + " fail=" + failed + " total=" + (ok + failed));

        // Cada string retornada pela nativa e liberada por newOwnedString().
        print("FEATURE FreeString => exercised by native newOwnedString() on every return");
        print("FEATURES: ScanRootsJson, DetectPathJson, LoadGameParamsJson, "
                + "SaveGameParams, UpscalePresetsJson, DefaultSchoolDaysRoute, "
                + "ValidateRoute, ResolveSaveDirectory, FreeString, LaunchGame");
        flushLog();
    }

    private static void print(String line) {
        System.out.println(line);
        LOG.add(line);
    }

    private static void flushLog() {
        try {
            Path dir = Path.of(System.getProperty("java.io.tmpdir"),
                    "kotonoha-java-bridge");
            Files.createDirectories(dir);
            Files.write(dir.resolve("selftest.log"), LOG,
                    StandardCharsets.UTF_8);
            System.out.println("[LOG] " + dir.resolve("selftest.log"));
        } catch (Exception ignored) {
        }
    }
}
