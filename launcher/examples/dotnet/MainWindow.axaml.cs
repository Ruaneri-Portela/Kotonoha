using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Kotonoha.Launcher;

namespace KotonohaBridgeTest;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<string> _roots;
    private readonly ObservableCollection<GameEntry> _games = new();

    public MainWindow()
    {
        InitializeComponent();

        var available = Program.DefaultRoots.Where(Directory.Exists).ToArray();
        _roots = new ObservableCollection<string>(
            available.Length > 0 ? available : new[] { Program.DefaultRoots[0] });
        RootsList.ItemsSource = _roots;
        if (_roots.Count > 0)
            RootsList.SelectedIndex = 0;
        GamesList.ItemsSource = _games;

        Log($"Ponte carregada: DllImport(\"KotonohaLauncherCore\") -> {Program.NativeLibraryPath}");
        Log($"Nativa existe: {File.Exists(Program.NativeLibraryPath)}");
        Log($"Roots iniciais: {string.Join(" | ", _roots)}");
        Log("Scan -> selecione UM jogo -> Launch game (um por vez).");

        Opened += MainWindow_Opened;
    }

    private static string? Opt(TextBox box) =>
        string.IsNullOrWhiteSpace(box.Text) ? null : box.Text;

    private void Log(string message)
    {
        var line = $"[{DateTime.Now:HH:mm:ss}] {message}";
        LogTextBox.Text += line + Environment.NewLine;
        StatusTextBlock.Text = message;
        try
        {
            Directory.CreateDirectory(HeadlessSelfTest.LogDirectory);
            File.AppendAllText(
                Path.Combine(HeadlessSelfTest.LogDirectory, "kotonoha-bridge-gui.log"),
                line + Environment.NewLine);
        }
        catch
        {
            // Log em arquivo e' best-effort; a area de log da UI e' a principal.
        }
    }

    private void ShowJson(string label, string json)
    {
        string formatted;
        try
        {
            using var doc = JsonDocument.Parse(json);
            formatted = JsonSerializer.Serialize(
                doc.RootElement, new JsonSerializerOptions { WriteIndented = true });
        }
        catch (JsonException)
        {
            formatted = json;
        }

        ResultTextBox.Text = $"// {label}\n{formatted}";
        Log($"{label}: {json.Length} chars, JSON=" +
            $"{(formatted == json ? "bruto" : "valido/formatado")}");
        if (label == "ScanRootsJson")
            ShowGames(json);
    }

    private async Task RunJsonAsync(string label, Func<string> call)
    {
        try
        {
            Log($"{label} ...");
            var json = await Task.Run(call);
            ShowJson(label, json);
        }
        catch (Exception ex)
        {
            Log($"[ERRO] {label}: {ex}");
        }
    }

    private void ShowGames(string json)
    {
        _games.Clear();
        using var doc = JsonDocument.Parse(json);
        foreach (var entry in doc.RootElement.EnumerateArray())
        {
            static string Str(JsonElement obj, string name) =>
                obj.ValueKind == JsonValueKind.Object &&
                obj.TryGetProperty(name, out var v) &&
                v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";

            JsonElement p = entry.TryGetProperty("params", out var pp) ? pp : default;
            string Param(string name) => Str(p, name);

            _games.Add(new GameEntry
            {
                Title = Str(entry, "title"),
                SaveProfileId = Str(entry, "saveProfileId"),
                Path = Str(entry, "path"),
                OverlayPath = Str(entry, "overlayPath"),
                EngineSlug = Str(entry, "engineSlug"),
                AssetOverlayPath = Param("assetOverlayPath"),
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
            });
        }
        Log($"Scan: {_games.Count} jogo(s) na lista");
    }

    private string SelectedRootOrTyped() =>
        RootsList.SelectedItem as string
        ?? (string.IsNullOrWhiteSpace(RootTextBox.Text) ? null : RootTextBox.Text)
        ?? string.Empty;

    private void AddRoot(string path)
    {
        path = path.Trim();
        if (path.Length == 0 || _roots.Contains(path))
            return;
        _roots.Add(path);
        RootsList.SelectedItem = path;
        Log($"Root adicionado: {path} (existe={Directory.Exists(path)})");
    }

    private async void AddRoot_Click(object? sender, RoutedEventArgs e)
    {
        try
        {
            var topLevel = TopLevel.GetTopLevel(this);
            if (topLevel?.StorageProvider.CanPickFolder == true)
            {
                var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(
                    new FolderPickerOpenOptions { Title = "Adicionar biblioteca / pasta de jogo" });
                if (folders.Count > 0)
                {
                    var picked = folders[0].TryGetLocalPath();
                    if (!string.IsNullOrWhiteSpace(picked))
                        AddRoot(picked);
                    return;
                }
            }
            AddRoot(RootTextBox.Text ?? string.Empty);
        }
        catch (Exception ex)
        {
            Log($"[ERRO] Adicionar pasta: {ex.Message}. Use o campo de texto e Enter.");
        }
    }

    private void RootTextBox_KeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
            AddRoot(RootTextBox.Text ?? string.Empty);
    }

    private void RemoveRoot_Click(object? sender, RoutedEventArgs e)
    {
        var toRemove = (RootsList.SelectedItems ?? Array.Empty<object>())
            .OfType<string>().ToList();
        foreach (var item in toRemove)
            _roots.Remove(item);
        Log($"Roots removidos: {toRemove.Count}");
    }

    private async void Scan_Click(object? sender, RoutedEventArgs e) => await ScanCoreAsync();

    private async Task ScanCoreAsync()
    {
        var roots = _roots.ToArray();
        if (roots.Length == 0)
        {
            Log("[AVISO] Nenhum root na lista.");
            return;
        }
        var split = SplitCheck.IsChecked == true;
        var custom = Opt(CustomSaveRootTextBox);
        Log($"ScanRootsJson roots=[{string.Join(", ", roots)}] split={split} custom={custom ?? "<null>"}");
        await RunJsonAsync("ScanRootsJson",
            () => KotonohaLauncher.ScanRoots(roots, split, custom));
    }

    private void GamesList_SelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (GamesList.SelectedItem is not GameEntry game)
            return;
        Log($"Jogo selecionado: {game.Title} | {game.EngineSlug} | " +
            $"{game.Path} (profile={game.SaveProfileId})");
        LaunchStatusTextBlock.Text = "Selecionado: " + game.Title;

        // Preenche a request com os valores vindos do JSON de scan.
        OverlayTextBox.Text = string.IsNullOrEmpty(game.AssetOverlayPath)
            ? game.OverlayPath : game.AssetOverlayPath;
        TranslationTextBox.Text = game.TranslationPath;
        SubtitleStyleTextBox.Text = game.SubtitleStylePath;
        DocumentsTextBox.Text = game.CustomDocumentsDir;
        RouteTextBox.Text = game.KtrfPath;
        ScriptsTextBox.Text = game.ScriptsDir;
        StartScenarioTextBox.Text = game.StartScenario;
        RendererTextBox.Text = game.RendererBackend;
        UpscalerTextBox.Text = game.UpscalerBackend;
        UpscalerPresetTextBox.Text = game.UpscalerPreset;
        UpscalerFactorTextBox.Text = game.UpscalerFactor;
    }

    private KotonohaGameLaunchRequest BuildRequest(GameEntry game) => new()
    {
        GamePath = game.Path,
        EngineSlug = game.EngineSlug,
        SaveProfileId = game.SaveProfileId,
        SplitSavesByGame = SplitCheck.IsChecked == true,
        CustomSaveRoot = Opt(CustomSaveRootTextBox),
        AssetOverlayPath = Opt(OverlayTextBox) ?? game.OverlayPath,
        TranslationPath = Opt(TranslationTextBox),
        SubtitleStylePath = Opt(SubtitleStyleTextBox),
        CustomDocumentsDir = Opt(DocumentsTextBox),
        KtrfPath = Opt(RouteTextBox),
        ScriptsDir = Opt(ScriptsTextBox),
        StartScenario = Opt(StartScenarioTextBox),
        RendererBackend = Opt(RendererTextBox),
        UpscalerBackend = Opt(UpscalerTextBox),
        UpscalerPreset = Opt(UpscalerPresetTextBox),
        UpscalerFactor = Opt(UpscalerFactorTextBox),
        DryRun = DryRunCheck.IsChecked == true,
    };

    private async void LaunchGame_Click(object? sender, RoutedEventArgs e)
    {
        if (GamesList.SelectedItem is not GameEntry game)
        {
            Log("[AVISO] Selecione um jogo na lista antes de Launch game.");
            LaunchStatusTextBlock.Text = "Nenhum jogo selecionado";
            return;
        }
        await LaunchOneAsync(game, false);
    }

    private async void ValidateRequest_Click(object? sender, RoutedEventArgs e)
    {
        if (GamesList.SelectedItem is not GameEntry game)
        {
            Log("[AVISO] Selecione um jogo na lista antes de Validate request.");
            return;
        }
        await LaunchOneAsync(game, true);
    }

    /** Launches only `game`, with the full request assembled from the scan JSON. */
    private async Task LaunchOneAsync(GameEntry game, bool validate)
    {
        var request = BuildRequest(game);
        if (validate)
        {
            Log("Validate request => launch request validated; " +
                "graphical launch requires SDL app");
            var description = JsonSerializer.Serialize(request,
                new JsonSerializerOptions { WriteIndented = true, IncludeFields = true });
            ShowJson("LaunchRequest (validated)", description);
            LaunchStatusTextBlock.Text = "Request validada: " + game.Title;
            return;
        }
        try
        {
            Log($"Launch game: {game.Path} engine={request.EngineSlug} " +
                $"profile={request.SaveProfileId} dryRun={request.DryRun}");
            LaunchStatusTextBlock.Text = request.DryRun
                ? "Validando request..."
                : "Jogo em execução — feche a janela para voltar ao launcher...";
            // Bloqueia ate' o jogo fechar (ou ate' o dry-run terminar); so'
            // entao' o controle volta para a UI gerenciada.
            var result = await Task.Run(() => KotonohaLauncher.LaunchGame(request));
            bool ok = result.Contains("\"ok\":true", StringComparison.Ordinal);
            bool playable = result.Contains("\"playable\":true",
                StringComparison.Ordinal);
            Log($"Launch {(ok ? "OK  " : "ERRO")} {game.Title} " +
                $"playable={playable} => {result}");
            ShowJson("LaunchGame", result);
            LaunchStatusTextBlock.Text = (ok ? "Launched OK: " : "Launch ERRO: ")
                + game.Title + $" (playable={playable})";
            if (!ok || result.Contains("\"dryRun\":true", StringComparison.Ordinal))
            {
                Log("launch request validated; graphical launch requires SDL app" +
                    " (veja o JSON/request acima)");
            }
        }
        catch (Exception ex)
        {
            Log($"[ERRO] Launch game: {ex}");
            LaunchStatusTextBlock.Text = "Launch ERRO: " + game.Title;
        }
    }

    private async void MainWindow_Opened(object? sender, EventArgs e)
    {
        var args = Environment.GetCommandLineArgs();
        bool autoScan = args.Contains("--auto-scan");
        bool validate = args.Contains("--validate");
        string? autoLaunch = args
            .FirstOrDefault(a => a.StartsWith("--auto-launch=", StringComparison.Ordinal))
            ?.Substring("--auto-launch=".Length);

        if (autoScan || autoLaunch != null)
        {
            Log("--auto-scan: disparando Scan pela mesma logica do botao");
            await ScanCoreAsync();
        }
        if (autoLaunch != null)
        {
            var game = _games.FirstOrDefault(g =>
                g.Path.Contains(autoLaunch, StringComparison.OrdinalIgnoreCase)
                || g.Title.Contains(autoLaunch, StringComparison.OrdinalIgnoreCase)
                || g.EngineSlug.Contains(autoLaunch, StringComparison.OrdinalIgnoreCase));
            if (game == null)
            {
                Log($"--auto-launch: jogo nao encontrado: {autoLaunch}");
                LaunchStatusTextBlock.Text = "Jogo nao encontrado: " + autoLaunch;
                return;
            }
            GamesList.SelectedItem = game;
            GamesList.ScrollIntoView(game);
            Log($"--auto-launch: selecionado {game.Title} ({game.Path})");
            await LaunchOneAsync(game, validate);
        }
    }

    private async void Detect_Click(object? sender, RoutedEventArgs e)
    {
        var path = SelectedRootOrTyped();
        if (string.IsNullOrWhiteSpace(path))
        {
            Log("[AVISO] Selecione/digite um caminho para Detect.");
            return;
        }
        var split = SplitCheck.IsChecked == true;
        var custom = Opt(CustomSaveRootTextBox);
        await RunJsonAsync("DetectPathJson",
            () => KotonohaLauncher.DetectPath(path, split, custom));
    }

    private async void LoadParams_Click(object? sender, RoutedEventArgs e)
    {
        var profile = Opt(ProfileTextBox) ?? "bridge-test";
        await RunJsonAsync("LoadGameParamsJson",
            () => KotonohaLauncher.LoadGameParams(profile));
    }

    private async void SaveParams_Click(object? sender, RoutedEventArgs e)
    {
        var profile = Opt(ProfileTextBox) ?? "bridge-test";
        try
        {
            Log($"SaveGameParams profile='{profile}' ...");
            var ok = await Task.Run(() => KotonohaLauncher.SaveGameParams(
                profile,
                Opt(OverlayTextBox), Opt(TranslationTextBox),
                Opt(SubtitleStyleTextBox), Opt(DocumentsTextBox),
                Opt(RouteTextBox), Opt(ScriptsTextBox),
                Opt(StartScenarioTextBox), Opt(RendererTextBox),
                Opt(UpscalerTextBox), Opt(UpscalerPresetTextBox),
                Opt(UpscalerFactorTextBox)));
            Log($"SaveGameParams retornou: {ok}");
            await RunJsonAsync("LoadGameParamsJson (apos save)",
                () => KotonohaLauncher.LoadGameParams(profile));
        }
        catch (Exception ex)
        {
            Log($"[ERRO] SaveGameParams: {ex}");
        }
    }

    private async void Upscale_Click(object? sender, RoutedEventArgs e) =>
        await RunJsonAsync("UpscalePresetsJson",
            () => KotonohaLauncher.UpscalePresets());

    private async void ValidateRoute_Click(object? sender, RoutedEventArgs e)
    {
        try
        {
            var route = Opt(RouteTextBox);
            if (route is null)
            {
                route = await Task.Run(() => KotonohaLauncher.DefaultSchoolDaysRoute());
                Log($"Rota padrao: '{route}'");
            }
            var result = await Task.Run(() => KotonohaLauncher.ValidateRoute(route));
            var json = JsonSerializer.Serialize(
                new { valid = result.Valid, info = result.Info, route },
                new JsonSerializerOptions { WriteIndented = true });
            ShowJson("ValidateRoute", json);
        }
        catch (Exception ex)
        {
            Log($"[ERRO] ValidateRoute: {ex}");
        }
    }

    private async void ResolveSaveDir_Click(object? sender, RoutedEventArgs e)
    {
        var path = SelectedRootOrTyped();
        var slug = Opt(EngineTextBox);
        var profile = Opt(ProfileTextBox);
        var split = SplitCheck.IsChecked == true;
        var custom = Opt(CustomSaveRootTextBox);
        await RunJsonAsync("ResolveSaveDirectory", () =>
        {
            var dir = KotonohaLauncher.ResolveSaveDirectory(path, slug, profile, split, custom);
            return JsonSerializer.Serialize(new { path, saveDirectory = dir },
                new JsonSerializerOptions { WriteIndented = true });
        });
    }

    private async void DefaultRoute_Click(object? sender, RoutedEventArgs e)
    {
        var route = await Task.Run(() => KotonohaLauncher.DefaultSchoolDaysRoute());
        ShowJson("DefaultSchoolDaysRoute",
            JsonSerializer.Serialize(new { route },
                new JsonSerializerOptions { WriteIndented = true }));
    }

    private void ClearLog_Click(object? sender, RoutedEventArgs e)
    {
        LogTextBox.Text = string.Empty;
        Log("Log da UI limpo (arquivo em TMPLOG mantido).");
    }
}
