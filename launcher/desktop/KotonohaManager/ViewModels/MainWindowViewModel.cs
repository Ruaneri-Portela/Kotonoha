using System.IO.Compression;
using System.Text.Json;
using System.Windows.Input;
using Avalonia.Threading;
using Kotonoha.Launcher;
using Kotonoha.Manager.Models;
using Kotonoha.Manager.Services;

namespace Kotonoha.Manager.ViewModels;

public sealed class MainWindowViewModel : ViewModelBase
{
    private readonly ManagerSettings _settings;
    private readonly SemaphoreSlim _nativeLock = new(1, 1);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    public MainWindowViewModel()
    {
        _settings = ManagerSettings.Load();
        if (_settings.LibraryRoots.Count == 0)
        {
            _settings.LibraryRoots.Add(DefaultGamesRoot());
            _settings.Save();
        }
        ScanCommand = new AsyncRelayCommand(_ => ScanAsync());
        LaunchCommand = new AsyncRelayCommand(parameter => LaunchAsync(parameter as GameEntryViewModel));
        ParametersCommand = new RelayCommand(parameter =>
            RequestParameters?.Invoke(parameter as GameEntryViewModel));
        SettingsCommand = new RelayCommand(_ => RequestSettings?.Invoke());
        AddRootCommand = new RelayCommand(parameter => AddRoot(parameter as string));
        RemoveRootCommand = new RelayCommand(parameter => RemoveRoot(parameter as string));
        ImportZipCommand = new AsyncRelayCommand(parameter => ImportZipAsync(parameter as string));
        RefreshCommand = new RelayCommand(_ => RefreshFilter());

        NativeStatus = NativeLibraryResolver.NativeDirectory is null
            ? "Nativo não encontrado — defina KOTONOHA_NATIVE_DIR."
            : $"Nativo: {NativeLibraryResolver.NativeDirectory}";

        // Carrega a biblioteca automaticamente na abertura (padrao Android/iOS).
        _ = ScanAsync();
    }

    public ObservableRangeCollection<GameEntryViewModel> Games { get; } = new();
    public ObservableRangeCollection<GameEntryViewModel> VisibleGames { get; } = new();

    public event Action<GameEntryViewModel?>? RequestParameters;
    public event Action? RequestSettings;

    public ICommand ScanCommand { get; }
    public ICommand LaunchCommand { get; }
    public ICommand ParametersCommand { get; }
    public ICommand SettingsCommand { get; }
    public ICommand AddRootCommand { get; }
    public ICommand RemoveRootCommand { get; }
    public ICommand ImportZipCommand { get; }
    public ICommand RefreshCommand { get; }

    private GameEntryViewModel? _selectedGame;
    public GameEntryViewModel? SelectedGame
    {
        get => _selectedGame;
        set { if (SetProperty(ref _selectedGame, value)) OnPropertyChanged(nameof(HasSelection)); }
    }
    public bool HasSelection => SelectedGame is not null;

    private string _searchQuery = "";
    public string SearchQuery
    {
        get => _searchQuery;
        set { if (SetProperty(ref _searchQuery, value)) RefreshFilter(); }
    }

    private string _status = "Pronto.";
    public string Status { get => _status; set => SetProperty(ref _status, value); }

    private string _notificationText = "";
    public string NotificationText { get => _notificationText; set => SetProperty(ref _notificationText, value); }
    private bool _notificationVisible;
    public bool NotificationVisible { get => _notificationVisible; set => SetProperty(ref _notificationVisible, value); }

    private DispatcherTimer? _notifyShowTimer;
    private DispatcherTimer? _notifyHideTimer;
    private void ShowNotification(string text, double delaySeconds = 2,
                                  double durationSeconds = 4)
    {
        // DispatcherTimer roda na thread da UI; Task.Delay em lambda Postado
        // atualizava a propriedade fora do dispatcher e nao escondia o aviso.
        Dispatcher.UIThread.Post(() =>
        {
            _notifyShowTimer?.Stop();
            _notifyHideTimer?.Stop();
            NotificationVisible = false;
            NotificationText = text;

            _notifyShowTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(delaySeconds)
            };
            _notifyShowTimer.Tick += (_, _) =>
            {
                _notifyShowTimer.Stop();
                NotificationVisible = true;
                _notifyHideTimer = new DispatcherTimer
                {
                    Interval = TimeSpan.FromSeconds(durationSeconds)
                };
                _notifyHideTimer.Tick += (_, _) =>
                {
                    _notifyHideTimer.Stop();
                    NotificationVisible = false;
                };
                _notifyHideTimer.Start();
            };
            _notifyShowTimer.Start();
        });
    }

    private string _nativeStatus = "";
    public string NativeStatus { get => _nativeStatus; set => SetProperty(ref _nativeStatus, value); }

    private string _gameLog = "";
    public string GameLog { get => _gameLog; set => SetProperty(ref _gameLog, value); }

    private bool _isBusy;
    public bool IsBusy { get => _isBusy; set => SetProperty(ref _isBusy, value); }

    public IReadOnlyList<string> LibraryRoots => _settings.LibraryRoots;
    public ManagerSettings CurrentSettings => _settings;

    public void SyncSettings()
    {
        RefreshFilter();
        Status = $"{Games.Count} jogo(s) na biblioteca; {_settings.LibraryRoots.Count} raiz(es).";
    }

    public void AddRoot(string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || _settings.LibraryRoots.Contains(path)) return;
        _settings.LibraryRoots.Add(path);
        _settings.Save();
        Status = $"Raiz adicionada: {path}";
    }

    public void RemoveRoot(string? path)
    {
        if (path is null) return;
        _settings.LibraryRoots.Remove(path);
        _settings.Save();
        Status = $"Raiz removida: {path}";
    }

    public void RefreshFilter()
    {
        VisibleGames.ReplaceWith(Games.Where(entry => MatchesSearch(entry.Game)));
        if (SelectedGame is not null && !VisibleGames.Contains(SelectedGame)) SelectedGame = null;
    }

    private bool MatchesSearch(CatalogGame game)
    {
        var query = SearchQuery.Trim();
        return string.IsNullOrWhiteSpace(query) ||
               game.Title.Contains(query, StringComparison.OrdinalIgnoreCase) ||
               game.EngineSlug.Contains(query, StringComparison.OrdinalIgnoreCase) ||
               game.EngineDisplayName.Contains(query, StringComparison.OrdinalIgnoreCase);
    }

    public async Task ScanAsync(bool marshalToUi = true)
    {
        if (NativeLibraryResolver.NativeDirectory is null)
        {
            Status = NativeStatus;
            return;
        }
        IsBusy = true;
        Status = Strings.Searching;
        try
        {
            var roots = _settings.LibraryRoots.ToArray();
            var splitSaves = _settings.SplitSavesByGame;
            var saveRoot = _settings.CustomSaveRoot;
            List<CatalogGame> catalog;
            await _nativeLock.WaitAsync();
            try
            {
                // Publish a complete native snapshot instead of mixing
                // provisional cards with the previous scan's collection.
                catalog = await Task.Run(() =>
                    JsonSerializer.Deserialize<List<CatalogGame>>(
                        KotonohaLauncher.ScanRoots(roots, splitSaves, saveRoot),
                        JsonOptions) ?? new List<CatalogGame>());
            }
            finally { _nativeLock.Release(); }
            void finish()
            {
                var existing = Games.ToDictionary(entry => entry.Game.Path,
                    StringComparer.Ordinal);
                var updated = new List<GameEntryViewModel>(catalog.Count);
                foreach (var game in catalog)
                {
                    if (existing.Remove(game.Path, out var entry))
                    {
                        entry.UpdateGame(game);
                        updated.Add(entry);
                    }
                    else
                        updated.Add(new GameEntryViewModel(game));
                }
                // Keep an active process controllable even if its library
                // root was removed during the scan.
                foreach (var entry in existing.Values)
                {
                    if (entry.IsRunning)
                        updated.Add(entry);
                    else
                        entry.Dispose();
                }
                Games.ReplaceWith(updated);
                RefreshFilter();
                Status = $"{Games.Count} {Strings.Found}";
                ShowNotification($"{Games.Count} {Strings.Found}");
            }
            if (marshalToUi) await Dispatcher.UIThread.InvokeAsync(finish);
            else finish();
        }
        catch (Exception ex)
        {
            Status = $"{Strings.ScanFailed}: {ex.Message}";
        }
        finally { IsBusy = false; }
    }

    public void ToggleGame(GameEntryViewModel game)
    {
        if (game.IsRunning) StopGame(game);
        else _ = LaunchAsync(game);
    }

    public void StopGame(GameEntryViewModel game)
    {
        game.Stop();
        IsBusy = false;
        Status = $"{game.Title}: {Strings.Stop}";
        ShowNotification(Status, delaySeconds: 0);
    }

    private Task LaunchAsync(GameEntryViewModel? game)
    {
        if (game is null || game.IsRunning) return Task.CompletedTask;
        if (NativeLibraryResolver.NativeDirectory is null)
        {
            Status = NativeStatus;
            return Task.CompletedTask;
        }
        Status = $"Iniciando {game.Title}...";
        GameLog = "";
        try
        {
            var request = (game.Game.Parameters ?? new GameParameters()).ApplyTo(
                new KotonohaGameLaunchRequest(), game.Game,
                _settings.CustomSaveRoot, _settings.SplitSavesByGame);
            var dll = Path.Combine(NativeLibraryResolver.NativeDirectory,
                                   "KotonohaLauncherCore.dll");
            var process = KotonohaLauncher.LaunchGameDetached(request, dll);
            game.RunningProcess = process;
            game.IsRunning = true;
            IsBusy = true;
            Status = $"{game.Title} {Strings.Running} (PID {process.Id})";

            void append(string? line)
            {
                if (string.IsNullOrWhiteSpace(line)) return;
                Dispatcher.UIThread.Post(() =>
                {
                    GameLog += line + Environment.NewLine;
                    if (GameLog.Length > 16000)
                        GameLog = GameLog.Substring(GameLog.Length - 16000);
                });
            }
            process.OutputDataReceived += (_, e) => append(e.Data);
            process.ErrorDataReceived += (_, e) => append(e.Data);
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.Exited += (_, _) => Dispatcher.UIThread.Post(() =>
            {
                try
                {
                    // A stopped game may already have been relaunched.
                    // Never clear the new process from an old exit event.
                    if (!ReferenceEquals(game.RunningProcess, process))
                        return;
                    game.RunningProcess = null;
                    game.IsRunning = false;
                    IsBusy = false;
                    Status = $"{game.Title} {Strings.Exited} ({process.ExitCode})";
                    ShowNotification(Status, delaySeconds: 0);
                }
                finally { process.Dispose(); }
            });
            // Subscribe before enabling events, including immediate exits.
            process.EnableRaisingEvents = true;
        }
        catch (Exception ex)
        {
            game.IsRunning = false;
            IsBusy = false;
            Status = $"{Strings.LaunchFailed} {game.Title}: {ex.Message}";
            ShowNotification(Status, delaySeconds: 0);
        }
        return Task.CompletedTask;
    }

    private async Task ImportZipAsync(string? zipPath)
    {
        if (string.IsNullOrWhiteSpace(zipPath) || !File.Exists(zipPath)) return;
        var targetRoot = _settings.LibraryRoots.FirstOrDefault() ?? DefaultGamesRoot();
        Directory.CreateDirectory(targetRoot);
        var name = Path.GetFileNameWithoutExtension(zipPath);
        var target = Path.Combine(targetRoot, name);
        IsBusy = true;
        try
        {
            await Task.Run(() => ExtractSafe(zipPath, target));
            AddRoot(target);
            Status = $"Importado: {target}";
        }
        catch (Exception ex)
        {
            Status = $"Falha na importação: {ex.Message}";
        }
        finally { IsBusy = false; }
    }

    /// <summary>Path-traversal safe ZIP extraction, same limits as Android SAF.</summary>
    private static void ExtractSafe(string zipPath, string target)
    {
        const int maxEntries = 10_000;
        const long maxBytes = 64L * 1024 * 1024 * 1024;
        var fullTarget = Path.GetFullPath(target);
        Directory.CreateDirectory(fullTarget);
        long total = 0;
        using var archive = ZipFile.OpenRead(zipPath);
        if (archive.Entries.Count > maxEntries)
            throw new InvalidDataException("ZIP com entradas demais.");
        foreach (var entry in archive.Entries)
        {
            if (string.IsNullOrWhiteSpace(entry.FullName) ||
                entry.FullName.EndsWith('/')) continue;
            var destination = Path.GetFullPath(Path.Combine(fullTarget, entry.FullName));
            if (!destination.StartsWith(fullTarget + Path.DirectorySeparatorChar,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"Entrada insegura no ZIP: {entry.FullName}");
            total += entry.Length;
            if (total > maxBytes) throw new InvalidDataException("ZIP maior que 64 GiB.");
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            entry.ExtractToFile(destination, overwrite: true);
        }
    }

    public static string DefaultGamesRoot() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
        "Documents", "Kotonoha", "games");
}
