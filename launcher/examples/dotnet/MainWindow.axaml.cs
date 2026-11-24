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

    public MainWindow()
    {
        InitializeComponent();

        var available = Program.DefaultRoots.Where(Directory.Exists).ToArray();
        _roots = new ObservableCollection<string>(
            available.Length > 0 ? available : new[] { Program.DefaultRoots[1] });
        RootsList.ItemsSource = _roots;
        if (_roots.Count > 0)
            RootsList.SelectedIndex = 0;

        Log($"Ponte carregada: DllImport(\"KotonohaLauncherCore\") -> {Program.NativeLibraryPath}");
        Log($"Nativa existe: {File.Exists(Program.NativeLibraryPath)}");
        Log($"Roots iniciais: {string.Join(" | ", _roots)}");
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

    private async void Scan_Click(object? sender, RoutedEventArgs e)
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

    private async void LaunchAll_Click(object? sender, RoutedEventArgs e)
    {
        var roots = _roots.ToArray();
        if (roots.Length == 0)
        {
            Log("[AVISO] Nenhum root na lista.");
            return;
        }
        var split = SplitCheck.IsChecked == true;
        var custom = Opt(CustomSaveRootTextBox);
        var profile = Opt(ProfileTextBox) ?? "";
        Log("LaunchGame: escaneando roots e lancando cada jogo detectado...");
        try
        {
            var results = await Task.Run(() =>
            {
                var lines = new List<string>();
                string scan = KotonohaLauncher.ScanRoots(roots, split, custom);
                using var doc = JsonDocument.Parse(scan);
                foreach (var entry in doc.RootElement.EnumerateArray())
                {
                    string path = entry.TryGetProperty("path", out var p)
                        ? p.GetString() ?? "" : "";
                    string slug = entry.TryGetProperty("engineSlug", out var s)
                        ? s.GetString() ?? "" : "";
                    if (string.IsNullOrEmpty(path)) continue;
                    string result = KotonohaLauncher.LaunchGame(
                        path, slug, profile, split, custom);
                    lines.Add($"[LAUNCH] {(result.Contains("\"ok\":true") ? "OK" : "FAIL")} {path} => {result}");
                }
                return lines;
            });
            int ok = 0;
            foreach (string line in results)
            {
                if (line.Contains("[LAUNCH] OK")) ++ok;
                Log(line);
            }
            Log($"Launch all: ok={ok} fail={results.Count - ok} total={results.Count}");
        }
        catch (Exception ex)
        {
            Log($"[ERRO] Launch all: {ex}");
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
