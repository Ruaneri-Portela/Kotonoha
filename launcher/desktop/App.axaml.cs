using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Kotonoha.Manager.ViewModels;
using Kotonoha.Manager.Views;

namespace Kotonoha.Manager;

public partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        var viewModel = new MainWindowViewModel();
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow { DataContext = viewModel };
        }
        base.OnFrameworkInitializationCompleted();
    }
}
