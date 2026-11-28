using Avalonia;
using Kotonoha.Manager.Services;

namespace Kotonoha.Manager;

internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        NativeLibraryResolver.Install();
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        return 0;
    }

    // Necessario para o designer do Avalonia.
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UsePlatformDetect()
        .WithInterFont()
        .LogToTrace();
}
