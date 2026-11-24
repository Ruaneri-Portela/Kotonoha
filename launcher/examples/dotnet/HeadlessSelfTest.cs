using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using Kotonoha.Launcher;

namespace KotonohaBridgeTest;

/// <summary>
/// Execucao sem janela que exercita TODOS os pontos da C ABI, incluindo scan
/// recursivo de uma biblioteca e um lancamento in-process por jogo detectado.
/// Uso: dotnet run --project ... -- --selftest [--root /caminho]
/// </summary>
internal static class HeadlessSelfTest
{
    public const string LogDirectoryName = "TMPLOG";
    public static string LogDirectory => Path.Combine(Path.GetTempPath(), LogDirectoryName);
    public static string LogPath => Path.Combine(LogDirectory, "kotonoha-bridge-selftest.log");

    public static int Run(string[] args)
    {
        Directory.CreateDirectory(LogDirectory);
        var log = new List<string>();

        void Print(string line)
        {
            Console.WriteLine(line);
            log.Add(line);
        }

        try
        {
            var roots = new List<string>();
            // --single starts only the roots passed on the command line; useful
            // to verify one visible game window without launching the library.
            bool single = Array.Exists(args, a => a == "--single");
            if (!single)
                foreach (string candidate in Program.DefaultRoots)
                    if (Directory.Exists(candidate)) roots.Add(candidate);
            for (int i = 0; i < args.Length; ++i)
            {
                if (args[i] == "--root" && i + 1 < args.Length)
                    roots.Add(args[++i]);
                else if (!args[i].StartsWith("--", StringComparison.Ordinal) &&
                         !string.IsNullOrWhiteSpace(args[i]))
                    roots.Add(args[i]);
            }
            if (roots.Count == 0) roots.Add(Program.DefaultRoots[0]);

            Print($"[INFO] Repo: {Program.RepoRoot}");
            Print($"[INFO] DllImport: KotonohaLauncherCore");
            Print($"[INFO] Nativa resolvida: {Program.NativeLibraryPath}");
            Print($"[INFO] Arquivo existe: {File.Exists(Program.NativeLibraryPath)}");
            Print($"[INFO] Roots da biblioteca: {string.Join(" | ", roots)}");

            // 1. ScanRootsJson -- varredura recursiva da pasta.
            string scanJson = KotonohaLauncher.ScanRoots(roots.ToArray(), true, null);
            Print($"[FEATURE][ScanRootsJson] {scanJson}");
            bool dryRun = Array.Exists(args, a => a == "--dry-run");
            if (Array.Exists(args, a => a == "--headless"))
                Environment.SetEnvironmentVariable(
                    "KOTONOHA_LAUNCHER_LAUNCH_HEADLESS", "1");
            var requests = new List<KotonohaGameLaunchRequest>();
            using (var doc = JsonDocument.Parse(scanJson))
            {
                Print($"[SCAN] Kind={doc.RootElement.ValueKind}, " +
                      $"entries={doc.RootElement.GetArrayLength()}");
                foreach (var entry in doc.RootElement.EnumerateArray())
                {
                    static string Str(JsonElement obj, string name) =>
                        obj.ValueKind == JsonValueKind.Object &&
                        obj.TryGetProperty(name, out var v) &&
                        v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";

                    JsonElement p = entry.TryGetProperty("params", out var pp) ? pp : default;
                    string Param(string name) => Str(p, name);
                    string path = Str(entry, "path");
                    string slug = Str(entry, "engineSlug");
                    if (string.IsNullOrEmpty(path)) continue;
                    requests.Add(new KotonohaGameLaunchRequest
                    {
                        GamePath = path,
                        EngineSlug = slug,
                        SaveProfileId = Str(entry, "saveProfileId"),
                        SplitSavesByGame = true,
                        CustomSaveRoot = null,
                        AssetOverlayPath = string.IsNullOrEmpty(Param("assetOverlayPath"))
                            ? Str(entry, "overlayPath") : Param("assetOverlayPath"),
                        TranslationPath = Param("translationPath"),
                        SubtitleStylePath = Param("subtitleStylePath"),
                        CustomDocumentsDir = Param("customDocumentsDir"),
                        KtrfPath = Param("ktrfPath"),
                        ScriptsDir = Param("scriptsDir"),
                        StartScenario = Param("startScenario"),
                        RendererBackend = Param("rendererBackend"),
                        UpscalerBackend = Param("upscalerBackend"),
                        UpscalerPreset = Param("upscalerPreset"),
                        UpscalerFactor = Param("upscalerFactor"),
                        DryRun = dryRun,
                    });
                }
            }

            // 2. DetectPathJson -- raiz e primeiro jogo.
            Print("[FEATURE][DetectPathJson(root)] " +
                  KotonohaLauncher.DetectPath(roots[0], true, null));
            if (requests.Count > 0)
                Print("[FEATURE][DetectPathJson(game)] " +
                      KotonohaLauncher.DetectPath(requests[0].GamePath!, true, null));

            // 3. SaveGameParams + LoadGameParamsJson.
            const string profile = "dotnet-bridge-profile";
            bool saved = KotonohaLauncher.SaveGameParams(profile);
            Print($"[FEATURE][SaveGameParams] {saved}");
            Print("[FEATURE][LoadGameParamsJson] " +
                  KotonohaLauncher.LoadGameParams(profile));

            // 4. UpscalePresetsJson.
            string presets = KotonohaLauncher.UpscalePresets();
            Print($"[FEATURE][UpscalePresetsJson] tamanho={presets.Length}; " +
                  $"inicio={(presets.Length > 160 ? presets.Substring(0, 160) + "..." : presets)}");

            // 5. DefaultSchoolDaysRoute + ValidateRoute.
            string route = KotonohaLauncher.DefaultSchoolDaysRoute();
            Print($"[FEATURE][DefaultSchoolDaysRoute] '{route}'");
            if (string.IsNullOrEmpty(route) || !File.Exists(route))
            {
                string bundled = Path.Combine(Program.RepoRoot, "games",
                    "school-days", "game-assets", "school-days-hq.ktnroute");
                if (File.Exists(bundled)) route = bundled;
            }
            if (!string.IsNullOrEmpty(route) && File.Exists(route))
            {
                var validation = KotonohaLauncher.ValidateRoute(route);
                Print($"[FEATURE][ValidateRoute] valid={validation.Valid}; " +
                      $"info='{validation.Info}'");
            }
            else
            {
                Print("[FEATURE][ValidateRoute] SKIPPED (rota nao encontrada)");
            }

            // 6. ResolveSaveDirectory.
            Print("[FEATURE][ResolveSaveDirectory] " +
                  KotonohaLauncher.ResolveSaveDirectory(
                      roots[0], "school-days", profile, true, null));

            // 7. LaunchGame -- cada jogo detectado e' lancado in-process.
            int ok = 0, failed = 0;
            foreach (var request in requests)
            {
                string result = KotonohaLauncher.LaunchGame(request);
                bool good = result.Contains("\"ok\":true", StringComparison.Ordinal);
                bool playable = result.Contains("\"playable\":true",
                    StringComparison.Ordinal);
                if (good) ++ok; else ++failed;
                Print($"[LAUNCH] {(good ? "OK  " : "FAIL")} " +
                      $"playable={playable} {request.GamePath} => {result}");
            }
            Print($"[LAUNCH] ok={ok} fail={failed} total={requests.Count}");

            // TakeString() chama KotonohaLauncherFreeString em toda string nativa.
            Print("[FEATURE][FreeString] exercised by TakeString() on every native string");
            Print("[FEATURES] ScanRootsJson, DetectPathJson, LoadGameParamsJson, "
                + "SaveGameParams, UpscalePresetsJson, DefaultSchoolDaysRoute, "
                + "ValidateRoute, ResolveSaveDirectory, FreeString, LaunchGame");
            Print("[OK] P/Invoke concluido sem excecoes.");
            return 0;
        }
        catch (Exception ex)
        {
            Print("[ERRO] " + ex);
            return 1;
        }
        finally
        {
            File.WriteAllText(LogPath, string.Join(Environment.NewLine, log)
                + Environment.NewLine);
            Console.WriteLine($"[LOG] {LogPath}");
        }
    }
}
