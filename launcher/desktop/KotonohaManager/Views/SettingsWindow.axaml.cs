using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Kotonoha.Manager.ViewModels;

namespace Kotonoha.Manager.Views;

public partial class SettingsWindow : Window
{
    public SettingsWindow() => InitializeComponent();

    private SettingsViewModel Model =>
        DataContext as SettingsViewModel ?? throw new InvalidOperationException("Missing view model");

    private async void AddRoot_Click(object? sender, RoutedEventArgs e)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "Adicionar raiz da biblioteca",
            AllowMultiple = true,
        });
        foreach (var folder in folders) Model.AddRoot(folder.TryGetLocalPath());
    }

    private void RemoveRoot_Click(object? sender, RoutedEventArgs e)
    {
        var list = this.FindControl<ListBox>("RootsList");
        if (list?.SelectedItem is string path) Model.RemoveRoot(path);
    }

    private void Cancel_Click(object? sender, RoutedEventArgs e) => Close();

    private void Save_Click(object? sender, RoutedEventArgs e)
    {
        Model.SaveCommand.Execute(null);
        Close();
    }
}
