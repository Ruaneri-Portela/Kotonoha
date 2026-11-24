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
            foreach (string candidate in Program.DefaultRoots)
                if (Directory.Exists(candidate)) roots.Add(candidate);
            if (roots.Count == 0) roots.Add(Program.DefaultRoots[0]);
            for (int i = 0; i < args.Length; ++i)
            {
                if (args[i] == "--root" && i + 1 < args.Length)
                    roots.Add(args[++i]);
                else if (!args[i].StartsWith("--", StringComparison.Ordinal) &&
                         !string.IsNullOrWhiteSpace(args[i]))
                    roots.Add(args[i]);
            }

            Print($"[INFO] Repo: {Program.RepoRoot}");
            Print($"[INFO] DllImport: KotonohaLauncherCore");
            Print($"[INFO] Nativa resolvida: {Program.NativeLibraryPath}");
            Print($"[INFO] Arquivo existe: {File.Exists(Program.NativeLibraryPath)}");
            Print($"[INFO] Roots da biblioteca: {string.Join(" | ", roots)}");

            // 1. ScanRootsJson -- varredura recursiva da pasta.
            string scanJson = KotonohaLauncher.ScanRoots(roots.ToArray(), true, null);
            Print($"[FEATURE][ScanRootsJson] {scanJson}");
            var paths = new List<(string Path, string Slug)>();
            using (var doc = JsonDocument.Parse(scanJson))
            {
                Print($"[SCAN] Kind={doc.RootElement.ValueKind}, " +
                      $"entries={doc.RootElement.GetArrayLength()}");
                foreach (var entry in doc.RootElement.EnumerateArray())
                {
                    string path = entry.TryGetProperty("path", out var p)
                        ? p.GetString() ?? "" : "";
                    string slug = entry.TryGetProperty("engineSlug", out var s)
                        ? s.GetString() ?? "" : "";
                    if (!string.IsNullOrEmpty(path)) paths.Add((path, slug));
                }
            }

            // 2. DetectPathJson -- raiz e primeiro jogo.
            Print("[FEATURE][DetectPathJson(root)] " +
                  KotonohaLauncher.DetectPath(roots[0], true, null));
            if (paths.Count > 0)
                Print("[FEATURE][DetectPathJson(game)] " +
                      KotonohaLauncher.DetectPath(paths[0].Path, true, null));

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
            foreach (var game in paths)
            {
                string result = KotonohaLauncher.LaunchGame(
                    game.Path, game.Slug, profile, true, null);
                bool good = result.Contains("\"ok\":true", StringComparison.Ordinal);
                if (good) ++ok; else ++failed;
                Print($"[LAUNCH] {(good ? "OK  " : "FAIL")} {game.Path} => {result}");
            }
            Print($"[LAUNCH] ok={ok} fail={failed} total={paths.Count}");

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
