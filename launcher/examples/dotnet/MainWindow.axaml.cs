using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Kotonoha.Launcher;

namespace KotonohaBridgeTest;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<string> _roots;
    private readonly ObservableCollection<GameEntry> _games = new();
    // Log de cada jogo, indexado pelo caminho do scan (sobrevive a re-scans).
    private readonly Dictionary<string, StringBuilder> _gameLogs = new();
    private sealed class HostState
    {
        public required int Pid { get; init; }
        public required GameEntry Game { get; init; }
        public required string LogPath { get; init; }
        public long Offset { get; set; }
    }
    private readonly Dictionary<int, HostState> _hosts = new();
    private DispatcherTimer? _hostTimer;

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
        ShowGameLog(null);

        Opened += MainWindow_Opened;
    }

    private static string? Opt(TextBox box) =>
        string.IsNullOrWhiteSpace(box.Text) ? null : box.Text;

    private void Log(string message) => AppendLog(message, null);

    /// <summary>Escreve no log geral e no log especifico do jogo.</summary>
    private void LogFor(GameEntry game, string message) => AppendLog(message, game);

    private void AppendLog(string message, GameEntry? game)
    {
        var line = $"[{DateTime.Now:HH:mm:ss}] {message}";
        LogTextBox.Text += line + Environment.NewLine;
        if (game is not null)
            GameLogFor(game).AppendLine(line);
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

    private StringBuilder GameLogFor(GameEntry game)
    {
        if (!_gameLogs.TryGetValue(game.Path, out var log))
            _gameLogs[game.Path] = log = new StringBuilder();
        return log;
    }

    /// <summary>Chamado pelo SelectionChanged: mostra so o log daquele jogo.</summary>
    private void ShowGameLog(GameEntry? game)
    {
        if (game is null)
        {
            GameLogTextBox.Text = "Selecione um jogo na lista para ver o log.";
            return;
        }
        GameLogTextBox.Text =
            _gameLogs.TryGetValue(game.Path, out var log) && log.Length > 0
                ? log.ToString()
                : $"Sem log de launch para {game.Title}.\n" +
                  "Selecione o jogo e clique em Launch game para gerar o log.";
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

            string path = Str(entry, "path");
            _games.Add(new GameEntry
            {
                Title = Str(entry, "title"),
                SaveProfileId = Str(entry, "saveProfileId"),
                Path = path,
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
        {
            ShowGameLog(null);
            return;
        }
        Log($"Jogo selecionado: {game.Title} | {game.EngineSlug} | " +
            $"{game.Path} (profile={game.SaveProfileId})");
        LaunchStatusTextBlock.Text = "Selecionado: " + game.Title;
        // Clicar em um jogo mostra o log do launch/engine daquele jogo.
        ShowGameLog(game);

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
        // Segundo clique no mesmo jogo encerra o host isolado.
        var running = _hosts.Values.FirstOrDefault(host => host.Game == game);
        if (running is not null)
        {
            try
            {
                using var process = System.Diagnostics.Process.GetProcessById(running.Pid);
                process.Kill(entireProcessTree: true);
                LogFor(game, $"Encerrando jogo (PID {running.Pid})");
            }
            catch (ArgumentException) { }
            game.IsRunning = false;
            _hosts.Remove(running.Pid);
            LaunchStatusTextBlock.Text = "Game encerrado: " + game.Title;
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
            LogFor(game, "Validate request => launch request validated; " +
                "graphical launch requires SDL app");
            var description = JsonSerializer.Serialize(request,
                new JsonSerializerOptions { WriteIndented = true, IncludeFields = true });
            LogFor(game, "REQUEST " + description.Replace('\n', ' '));
            ShowJson("LaunchRequest (validated)", description);
            ShowGameLog(game);
            LaunchStatusTextBlock.Text = "Request validada: " + game.Title;
            return;
        }
        if (Program.AttachedMode)
        {
            var window = (Application.Current?.ApplicationLifetime as
                IClassicDesktopStyleApplicationLifetime)?.MainWindow;
            window?.Hide();
            try
            {
                var attached = await Task.Run(
                    () => KotonohaLauncher.LaunchGame(request));
                LogFor(game, "ATTACHED " + attached);
                ShowJson("LaunchGame (attached)", attached);
            }
            finally { window?.Show(); }
            return;
        }
        if (_hosts.Values.Any(host => host.Game == game))
        {
            Log($"[AVISO] {game.Title} ja esta em execucao.");
            return;
        }
        // Cada jogo tem seu proprio host; varios podem rodar ao mesmo tempo.
        game.IsRunning = true;
        LogFor(game, $"Launch game: {game.Path} engine={request.EngineSlug} " +
            $"profile={request.SaveProfileId} dryRun={request.DryRun}");
        LaunchStatusTextBlock.Text = request.DryRun
            ? "Validando request..."
            : "Aguardando jogo... feche a janela para voltar...";
        System.Diagnostics.Process? process = null;
        try
        {
            // O nucleo inicia o jogo em KotonohaGameHost; a UI nao bloqueia.
            var logPath = Path.Combine(Path.GetTempPath(),
                $"kotonoha-game-{Guid.NewGuid():N}.log");
            File.WriteAllText(logPath, string.Empty);
            Environment.SetEnvironmentVariable("KOTONOHA_DETACHED_LOG", logPath);
            process = await Task.Run(
                () => KotonohaLauncher.LaunchGameDetached(request));
            bool ok = process is not null;
            var result = JsonSerializer.Serialize(new
            {
                ok,
                pid = process?.Id ?? 0,
                host = "core",
                message = ok ? "Game started in a detached host"
                             : "Detached host did not report a PID"
            });
            bool playable = ok;
            if (ok && process is not null)
            {
                _hosts[process.Id] = new HostState
                {
                    Pid = process.Id, Game = game, LogPath = logPath
                };
                if (_hostTimer is null)
                {
                    _hostTimer = new DispatcherTimer
                    {
                        Interval = TimeSpan.FromSeconds(1)
                    };
                    _hostTimer.Tick += (_, _) => PollHosts();
                    _hostTimer.Start();
                }
            }
            LogFor(game, $"Launch {(ok ? "OK  " : "ERRO")} {game.Title} " +
                $"playable={playable} => {result}");
            // JSON final (inclui "Game exited cleanly"/frames) entra no log do jogo.
            LogFor(game, "RESULT " + result);
            ShowJson("LaunchGameDetached", result);
            GamesList.SelectedItem = game;
            GamesList.ScrollIntoView(game);
            LaunchStatusTextBlock.Text = (ok ? "Launched OK: " : "Launch ERRO: ")
                + game.Title + $" (playable={playable})";
            if (!ok || result.Contains("\"dryRun\":true", StringComparison.Ordinal))
            {
                LogFor(game, "launch request validated; graphical launch requires SDL app" +
                    " (veja o JSON/request acima)");
            }
        }
        catch (Exception ex)
        {
            Log($"[ERRO] Launch game: {ex}");
            LogFor(game, $"[ERRO] Launch game: {ex}");
            LaunchStatusTextBlock.Text = "Launch ERRO: " + game.Title;
        }
        finally
        {
            // Em caso de sucesso o evento Exited limpa o estado; aqui so'
            // tratamos a falha de spawn.
            if (process is null)
                game.IsRunning = false;
            ShowGameLog(game);
        }
    }

    private void PollHosts()
    {
        if (_hosts.Count == 0)
        {
            _hostTimer?.Stop();
            _hostTimer = null;
            return;
        }
        foreach (var state in _hosts.Values.ToList())
        {
            bool alive;
            try
            {
                alive = !System.Diagnostics.Process.GetProcessById(state.Pid).HasExited;
            }
            catch (ArgumentException) { alive = false; }
            var exitedInLog = false;
            try
            {
                exitedInLog = File.Exists(state.LogPath) &&
                    File.ReadAllText(state.LogPath)
                        .Contains("[Kotonoha] game result:", StringComparison.Ordinal);
            }
            catch (IOException) { }
            if (!alive || exitedInLog)
            {
                state.Game.IsRunning = false;
                LaunchStatusTextBlock.Text = "Game exited: " + state.Game.Title;
                LogFor(state.Game, $"Game host {state.Pid} exited");
                _hosts.Remove(state.Pid);
            }
            try
            {
                using var stream = new FileStream(state.LogPath, FileMode.Open,
                    FileAccess.Read, FileShare.ReadWrite);
                stream.Seek(state.Offset, SeekOrigin.Begin);
                using var reader = new StreamReader(stream);
                var text = reader.ReadToEnd();
                state.Offset = stream.Position;
                if (!string.IsNullOrEmpty(text))
                    LogFor(state.Game, text.TrimEnd());
            }
            catch (IOException) { }
        }
    }

    private void ClearRunning()
    {
        foreach (var entry in _games) entry.IsRunning = false;
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
