using System.Collections.ObjectModel;
using System.Globalization;
using System.Text.Json;
using System.Windows.Input;
using Kotonoha.Launcher;
using Kotonoha.Manager.Models;
using Kotonoha.Manager.Services;

namespace Kotonoha.Manager.ViewModels;

/// <summary>
/// Global defaults and library roots. Same structure as the Android graphics
/// sheet and the macOS settings sheet, with WinUI-style selectors.
/// </summary>
public sealed class SettingsViewModel : ViewModelBase
{
    private readonly ManagerSettings _settings;

    public SettingsViewModel(ManagerSettings settings)
    {
        _settings = settings;
        foreach (var root in settings.LibraryRoots) Roots.Add(root);
        SplitSavesByGame = settings.SplitSavesByGame;
        CustomSaveRoot = settings.CustomSaveRoot ?? "";
        RendererBackend = settings.RendererBackend ?? GlobalLabel;
        UpscalerBackend = settings.UpscalerBackend ?? GlobalLabel;
        UpscalerPreset = settings.UpscalerPreset ?? GlobalLabel;
        PresetOptions.Add(GlobalLabel);
        UpscalerFactor = ParseFactor(settings.UpscalerFactor);

        try
        {
            var presets = JsonSerializer.Deserialize<List<UpscalePreset>>(
                KotonohaLauncher.UpscalePresets(),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            foreach (var preset in presets ?? new List<UpscalePreset>())
                if (!string.IsNullOrWhiteSpace(preset.Name)) PresetOptions.Add(preset.Name);
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or JsonException)
        {
            // O gerenciador continua utilizavel sem a biblioteca nativa.
        }
        if (PresetOptions.Count == 1)
            foreach (var preset in FallbackPresets)
                PresetOptions.Add(preset);
        if (!PresetOptions.Contains(UpscalerPreset))
            UpscalerPreset = GlobalLabel;

        AddRootCommand = new RelayCommand(path => AddRoot(path as string));
        RemoveRootCommand = new RelayCommand(path => RemoveRoot(path as string));
        SaveCommand = new RelayCommand(_ => Save());
    }

    public ObservableCollection<string> Roots { get; } = new();
    public ObservableCollection<string> PresetOptions { get; } = new();
    internal const string GlobalLabel = "Padrão global";
    private static readonly string[] FallbackPresets =
    {
        "Anime4K: Mode A (HQ)", "Anime4K: Mode B", "ArtCNN: RCA FSR4"
    };
    public IReadOnlyList<string> RendererOptions { get; } =
        new[] { GlobalLabel, "auto", "opengl", "opengles2", "software" };
    public IReadOnlyList<string> UpscalerOptions { get; } =
        new[] { GlobalLabel, "auto", "none", "linear", "mpv", "metalfx" };
    public ICommand AddRootCommand { get; }
    public ICommand RemoveRootCommand { get; }
    public ICommand SaveCommand { get; }

    private bool _splitSavesByGame;
    public bool SplitSavesByGame { get => _splitSavesByGame; set => SetProperty(ref _splitSavesByGame, value); }
    private string _customSaveRoot = "";
    public string CustomSaveRoot { get => _customSaveRoot; set => SetProperty(ref _customSaveRoot, value); }
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

    private string _status = "";
    public string Status { get => _status; set => SetProperty(ref _status, value); }

    public void AddRoot(string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || Roots.Contains(path)) return;
        Roots.Add(path);
        Status = $"Raiz adicionada: {path}";
    }

    public void RemoveRoot(string? path)
    {
        if (path is not null) Roots.Remove(path);
    }

    public void Save()
    {
        _settings.LibraryRoots = Roots.ToList();
        _settings.SplitSavesByGame = SplitSavesByGame;
        _settings.CustomSaveRoot = string.IsNullOrWhiteSpace(CustomSaveRoot) ? null : CustomSaveRoot;
        _settings.RendererBackend = Normalize(RendererBackend);
        _settings.UpscalerBackend = Normalize(UpscalerBackend);
        _settings.UpscalerPreset = Normalize(UpscalerPreset);
        _settings.UpscalerFactor = UpscalerFactor > 1.0
            ? UpscalerFactor.ToString("0.00", CultureInfo.InvariantCulture)
            : null;
        _settings.Save();
        Status = "Ajustes salvos.";
    }

    private static double ParseFactor(string? value) =>
        double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var factor)
            ? Math.Clamp(factor, 1.0, 4.0)
            : 1.0;

    private static string? Normalize(string value) =>
        string.IsNullOrWhiteSpace(value) || value == GlobalLabel
            ? null
            : value.Trim();
}
