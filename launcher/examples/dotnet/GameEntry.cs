using System.ComponentModel;
using Avalonia.Media;

namespace KotonohaBridgeTest;

/// <summary>
/// One row from KotonohaLauncherScanRootsJson. Every field is passed into the
/// KotonohaGameLaunchRequest built by MainWindow before LaunchGame is called.
/// IsRunning drives the green "em execucao" indicator while LaunchGame blocks.
/// </summary>
public sealed class GameEntry : INotifyPropertyChanged
{
    private bool _isRunning;

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsRunning
    {
        get => _isRunning;
        set
        {
            if (_isRunning == value) return;
            _isRunning = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsRunning)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StatusText)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(RowBackground)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(RowForeground)));
        }
    }

    /// <summary>Texto indicador exibido enquanto o jogo esta aberto.</summary>
    public string StatusText => IsRunning ? "● RUNNING / EM EXECUÇÃO" : "";

    /// <summary>Verde no item da lista enquanto LaunchGame bloqueia (binding no XAML).</summary>
    public IBrush RowBackground => IsRunning
        ? new SolidColorBrush(Color.Parse("#B9F6C6"))
        : Brushes.Transparent;

    public IBrush RowForeground => IsRunning
        ? new SolidColorBrush(Color.Parse("#0B5A20"))
        : Brushes.Gray;

    public string Title { get; init; } = "";
    public string SaveProfileId { get; init; } = "";
    public string Path { get; init; } = "";
    public string OverlayPath { get; init; } = "";
    public string EngineSlug { get; init; } = "";
    public string AssetOverlayPath { get; init; } = "";
    public string TranslationPath { get; init; } = "";
    public string SubtitleStylePath { get; init; } = "";
    public string CustomDocumentsDir { get; init; } = "";
    public string KtrfPath { get; init; } = "";
    public string ScriptsDir { get; init; } = "";
    public string StartScenario { get; init; } = "";
    public string RendererBackend { get; init; } = "";
    public string UpscalerBackend { get; init; } = "";
    public string UpscalerPreset { get; init; } = "";
    public string UpscalerFactor { get; init; } = "";
}
