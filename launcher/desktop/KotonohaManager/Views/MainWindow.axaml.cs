using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Kotonoha.Manager.ViewModels;

namespace Kotonoha.Manager.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    private MainWindowViewModel Model =>
        DataContext as MainWindowViewModel ?? throw new InvalidOperationException("Missing view model");

    private void PlayCard_Click(object? sender, RoutedEventArgs e)
    {
        if ((sender as Button)?.DataContext is GameEntryViewModel game)
            Model.ToggleGame(game);
    }

    private async void AddFolder_Click(object? sender, RoutedEventArgs e)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "Adicionar pasta de jogos",
            AllowMultiple = true,
        });
        foreach (var folder in folders) Model.AddRoot(folder.TryGetLocalPath());
    }

    private async void AddDisc_Click(object? sender, RoutedEventArgs e)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Adicionar imagem de disco",
            AllowMultiple = true,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Disc images")
                {
                    Patterns = new[] { "*.iso", "*.bin", "*.img" },
                },
            },
        });
        foreach (var file in files) Model.AddRoot(file.TryGetLocalPath());
    }

    private async void ImportZip_Click(object? sender, RoutedEventArgs e)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Importar pacote ZIP",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("ZIP") { Patterns = new[] { "*.zip" } },
            },
        });
        if (files.Count > 0)
            Model.ImportZipCommand.Execute(files[0].TryGetLocalPath());
    }

    private async void Parameters_Click(object? sender, RoutedEventArgs e)
    {
        if (Model.SelectedGame is null) return;
        var vm = new GameParametersViewModel(Model.SelectedGame.Game, Model.CurrentSettings);
        var dialog = new GameParametersWindow { DataContext = vm };
        await dialog.ShowDialog(this);
        if (vm.Saved == true) Model.SelectedGame.Refresh();
    }

    private async void Settings_Click(object? sender, RoutedEventArgs e)
    {
        var vm = new SettingsViewModel(Model.CurrentSettings);
        var dialog = new SettingsWindow { DataContext = vm };
        await dialog.ShowDialog(this);
        Model.SyncSettings();
    }

    private void Quit_Click(object? sender, RoutedEventArgs e) =>
        (Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes
            .IClassicDesktopStyleApplicationLifetime)?.Shutdown();
}
