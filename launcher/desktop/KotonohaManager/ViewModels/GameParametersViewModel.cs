using System.Collections.ObjectModel;
using System.Globalization;
using System.Text.Json;
using System.Windows.Input;
using Kotonoha.Launcher;
using Kotonoha.Manager.Models;
using Kotonoha.Manager.Services;

namespace Kotonoha.Manager.ViewModels;

/// <summary>
/// Per-game parameters dialog: overlays/localization, School Days HQ route,
/// saves/startup and WinUI-style graphics selectors.
/// </summary>
public sealed class GameParametersViewModel : ViewModelBase
{
    private readonly CatalogGame _game;
    private readonly ManagerSettings _settings;

    public GameParametersViewModel(CatalogGame game, ManagerSettings settings)
    {
        _game = game;
        _settings = settings;
        var current = game.Parameters ?? new GameParameters();
        AssetOverlayPath = current.AssetOverlayPath ?? "";
        TranslationPath = current.TranslationPath ?? "";
        SubtitleStylePath = current.SubtitleStylePath ?? "";
        CustomDocumentsDir = current.CustomDocumentsDir ?? "";
        KtrfPath = current.KtrfPath ?? "";
        ScriptsDir = current.ScriptsDir ?? "";
        StartScenario = current.StartScenario ?? "";
        RendererBackend = current.RendererBackend ?? settings.RendererBackend ?? GlobalLabel;
        UpscalerBackend = current.UpscalerBackend ?? settings.UpscalerBackend ?? GlobalLabel;
        UpscalerPreset = current.UpscalerPreset ?? settings.UpscalerPreset ?? GlobalLabel;
        UpscalerFactor = ParseFactor(current.UpscalerFactor ?? settings.UpscalerFactor);
        IsSchoolDays = game.IsSchoolDays;

        PresetOptions.Add(GlobalLabel);
        foreach (var preset in LoadPresets())
            PresetOptions.Add(preset);
        if (PresetOptions.Count == 1)
            foreach (var preset in FallbackPresets)
                PresetOptions.Add(preset);
        if (!PresetOptions.Contains(UpscalerPreset))
            UpscalerPreset = GlobalLabel;

        SaveCommand = new RelayCommand(_ => Save());
        ResetCommand = new RelayCommand(_ => Reset());
        ValidateRouteCommand = new AsyncRelayCommand(_ => ValidateRouteAsync());
        UseGlobalGraphicsCommand = new RelayCommand(_ => UseGlobalGraphics());
    }

    public string Title => $"Parâmetros — {_game.Title}";
    public bool IsSchoolDays { get; }
    public ICommand SaveCommand { get; }
    public ICommand ResetCommand { get; }
    public ICommand ValidateRouteCommand { get; }
    public ICommand UseGlobalGraphicsCommand { get; }

    internal const string GlobalLabel = "Padrão global";
    private static readonly string[] FallbackPresets =
    {
        "Anime4K: Mode A (HQ)", "Anime4K: Mode B", "ArtCNN: RCA FSR4"
    };
    public IReadOnlyList<string> RendererOptions { get; } =
        new[] { GlobalLabel, "auto", "opengl", "opengles2", "software" };
    public IReadOnlyList<string> UpscalerOptions { get; } =
        new[] { GlobalLabel, "auto", "none", "linear", "mpv", "metalfx" };
    public ObservableCollection<string> PresetOptions { get; } = new();

    private string _assetOverlayPath = "";
    public string AssetOverlayPath { get => _assetOverlayPath; set => SetProperty(ref _assetOverlayPath, value); }
    private string _translationPath = "";
    public string TranslationPath { get => _translationPath; set => SetProperty(ref _translationPath, value); }
    private string _subtitleStylePath = "";
    public string SubtitleStylePath { get => _subtitleStylePath; set => SetProperty(ref _subtitleStylePath, value); }
    private string _customDocumentsDir = "";
    public string CustomDocumentsDir { get => _customDocumentsDir; set => SetProperty(ref _customDocumentsDir, value); }
    private string _ktrfPath = "";
    public string KtrfPath { get => _ktrfPath; set => SetProperty(ref _ktrfPath, value); }
    private string _scriptsDir = "";
    public string ScriptsDir { get => _scriptsDir; set => SetProperty(ref _scriptsDir, value); }
    private string _startScenario = "";
    public string StartScenario { get => _startScenario; set => SetProperty(ref _startScenario, value); }
    private string _rendererBackend = "";
    public string RendererBackend { get => _rendererBackend; set => SetProperty(ref _rendererBackend, value); }
    private string _upscalerBackend = "";
    public string UpscalerBackend { get => _upscalerBackend; set => SetProperty(ref _upscalerBackend, value); }
    private string _upscalerPreset = "";
    public string UpscalerPreset { get => _upscalerPreset; set => SetProperty(ref _upscalerPreset, value); }
    private double _upscalerFactor = 1.0;
    public double UpscalerFactor
    {
        get => _upscalerFactor;
        set
        {
            if (SetProperty(ref _upscalerFactor, value))
                OnPropertyChanged(nameof(FactorText));
        }
    }
    public string FactorText => $"{UpscalerFactor:0.00}×";

    private string _routeInfo = "Informação da rota aparece aqui.";
    public string RouteInfo { get => _routeInfo; set => SetProperty(ref _routeInfo, value); }

    private bool? _saved;
    public bool? Saved { get => _saved; set => SetProperty(ref _saved, value); }

    public GameParameters ToParameters() => new()
    {
        AssetOverlayPath = Null(AssetOverlayPath),
        TranslationPath = Null(TranslationPath),
        SubtitleStylePath = Null(SubtitleStylePath),
        CustomDocumentsDir = Null(CustomDocumentsDir),
        KtrfPath = Null(KtrfPath),
        ScriptsDir = Null(ScriptsDir),
        StartScenario = Null(StartScenario),
        RendererBackend = Normalize(RendererBackend),
        UpscalerBackend = Normalize(UpscalerBackend),
        UpscalerPreset = Normalize(UpscalerPreset),
        UpscalerFactor = UpscalerFactor > 1.0
            ? UpscalerFactor.ToString("0.00", CultureInfo.InvariantCulture)
            : null,
    };

    private void Save()
    {
        var p = ToParameters();
        Saved = KotonohaLauncher.SaveGameParams(
            _game.SaveProfileId,
            p.AssetOverlayPath, p.TranslationPath, p.SubtitleStylePath,
            p.CustomDocumentsDir, p.KtrfPath, p.ScriptsDir, p.StartScenario,
            p.RendererBackend, p.UpscalerBackend, p.UpscalerPreset, p.UpscalerFactor);
        RouteInfo = Saved == true ? "Parâmetros salvos." : "Falha ao salvar parâmetros.";
    }

    private void Reset()
    {
        AssetOverlayPath = TranslationPath = SubtitleStylePath = "";
        CustomDocumentsDir = KtrfPath = ScriptsDir = StartScenario = "";
        UseGlobalGraphics();
        Saved = null;
        RouteInfo = "Parâmetros restaurados para os padrões globais.";
    }

    private void UseGlobalGraphics()
    {
        RendererBackend = _settings.RendererBackend ?? GlobalLabel;
        UpscalerBackend = _settings.UpscalerBackend ?? GlobalLabel;
        UpscalerPreset = _settings.UpscalerPreset ?? GlobalLabel;
        UpscalerFactor = ParseFactor(_settings.UpscalerFactor);
    }

    private async Task ValidateRouteAsync()
    {
        var path = string.IsNullOrWhiteSpace(KtrfPath)
            ? KotonohaLauncher.DefaultSchoolDaysRoute()
            : KtrfPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            RouteInfo = "Nenhuma rota .ktrf informada.";
            return;
        }
        var (valid, info) = await Task.Run(() => KotonohaLauncher.ValidateRoute(path));
        RouteInfo = $"{(valid ? "Válida" : "Inválida")}: {info}";
    }

    private static IEnumerable<string> LoadPresets()
    {
        try
        {
            var presets = JsonSerializer.Deserialize<List<UpscalePreset>>(
                KotonohaLauncher.UpscalePresets(),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            return presets?.Select(p => p.Name).Where(n => !string.IsNullOrWhiteSpace(n))
                          ?? Array.Empty<string>();
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or JsonException)
        {
            return Array.Empty<string>();
        }
    }

    private static double ParseFactor(string? value) =>
        double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var factor)
            ? Math.Clamp(factor, 1.0, 4.0)
            : 1.0;

    private static string? Null(string value) =>
        string.IsNullOrWhiteSpace(value) ? null : value.Trim();

    private static string? Normalize(string value) =>
        string.IsNullOrWhiteSpace(value) || value == GlobalLabel
            ? null
            : value.Trim();
}
