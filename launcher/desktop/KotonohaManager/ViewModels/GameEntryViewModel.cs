using System.Diagnostics;
using Avalonia.Media;
using Kotonoha.Manager.Models;
using Kotonoha.Manager.Services;

namespace Kotonoha.Manager.ViewModels;

/// <summary>
/// One card in the library list. Mirrors the Android item_game_card.xml and
/// the iOS/macOS KotonohaGameItem detail pane.
/// </summary>
public sealed class GameEntryViewModel : ViewModelBase
{
    public CatalogGame Game { get; }

    public GameEntryViewModel(CatalogGame game)
    {
        Game = game;
        Localization.Instance.PropertyChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(StatusText));
            OnPropertyChanged(nameof(ActionLabel));
        };
    }

    public string Title => string.IsNullOrWhiteSpace(Game.Title)
        ? Path.GetFileName(Game.Path.TrimEnd(Path.DirectorySeparatorChar))
        : Game.Title;
    public string Engine => string.IsNullOrWhiteSpace(Game.EngineDisplayName)
        ? Game.EngineSlug
        : Game.EngineDisplayName;
    public string PathText => Game.Path;
    public string SaveDirectory => Game.SaveDirectory ?? "";
    public bool IsDiscImage => Game.IsDiscImage;
    public bool HasOverlay => !string.IsNullOrWhiteSpace(Game.OverlayPath);
    public bool HasOverrides => Game.Parameters?.HasAnyOverride ?? false;
    public string KindBadge => Game.IsDiscImage ? "DISC IMAGE" : "FOLDER";

    private bool _isRunning;
    public bool IsRunning
    {
        get => _isRunning;
        set
        {
            if (SetProperty(ref _isRunning, value))
            {
                OnPropertyChanged(nameof(StatusText));
                OnPropertyChanged(nameof(ActionLabel));
                OnPropertyChanged(nameof(RowBackground));
                OnPropertyChanged(nameof(RowForeground));
            }
        }
    }

    public string StatusText
    {
        get
        {
            var strings = Localization.Instance;
            return IsRunning
                ? strings.Library + ": " + strings.Running
                : HasOverrides ? strings.Parameters.Replace("...", "") + " (Custom)" : strings.Ready;
        }
    }

    public string ActionLabel =>
        IsRunning ? Localization.Instance.Stop : T("▶", "▶");

    private static string T(string pt, string en) =>
        Localization.Instance.Language == "en" ? en : pt;

    public Process? RunningProcess { get; set; }

    public void Stop()
    {
        if (RunningProcess is { HasExited: false } process)
        {
            try { process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
        }
        RunningProcess = null;
        IsRunning = false;
    }
    public IBrush RowBackground => IsRunning
        ? new SolidColorBrush(Color.Parse("#525D84"))
        : Brushes.Transparent;
    public IBrush RowForeground => IsRunning
        ? new SolidColorBrush(Color.Parse("#FFFFFF"))
        : new SolidColorBrush(Color.Parse("#DCE3F0"));

    public void Refresh() => OnPropertyChanged(string.Empty);
}
