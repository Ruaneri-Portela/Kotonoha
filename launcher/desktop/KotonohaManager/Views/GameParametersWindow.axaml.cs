using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Kotonoha.Manager.ViewModels;

namespace Kotonoha.Manager.Views;

public partial class GameParametersWindow : Window
{
    public GameParametersWindow() => InitializeComponent();

    private GameParametersViewModel Model => DataContext as GameParametersViewModel
        ?? throw new InvalidOperationException("Missing view model");

    private void Cancel_Click(object? sender, RoutedEventArgs e) => Close();

    private void Save_Click(object? sender, RoutedEventArgs e)
    {
        Model.SaveCommand.Execute(null);
        if (Model.Saved == true) Close();
    }

    private async void BrowseFolder_Click(object? sender, RoutedEventArgs e)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "Selecionar pasta",
            AllowMultiple = false,
        });
        if (folders.Count == 0) return;
        var path = folders[0].TryGetLocalPath();
        if (path is null) return;

        switch ((sender as Control)?.Name)
        {
            case "BrowseOverlay": Model.AssetOverlayPath = path; break;
            case "BrowseScripts": Model.ScriptsDir = path; break;
            case "BrowseDocuments": Model.CustomDocumentsDir = path; break;
        }
    }

    private async void BrowseFile_Click(object? sender, RoutedEventArgs e)
    {
        var name = (sender as Control)?.Name;
        var filter = name switch
        {
            "BrowseRoute" => new FilePickerFileType("Rota") { Patterns = new[] { "*.ktrf", "*.ktnroute" } },
            "BrowseSubtitleStyle" => new FilePickerFileType("ASS") { Patterns = new[] { "*.ass" } },
            _ => new FilePickerFileType("Tradução") { Patterns = new[] { "*.ktr", "*.json", "*.txt" } },
        };
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Selecionar arquivo",
            AllowMultiple = false,
            FileTypeFilter = new[] { filter },
        });
        if (files.Count == 0) return;
        var path = files[0].TryGetLocalPath();
        if (path is null) return;

        switch (name)
        {
            case "BrowseRoute": Model.KtrfPath = path; break;
            case "BrowseSubtitleStyle": Model.SubtitleStylePath = path; break;
            case "BrowseTranslation": Model.TranslationPath = path; break;
        }
    }
}
