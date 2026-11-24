using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Avalonia;
using Kotonoha.Launcher;

namespace KotonohaBridgeTest;

internal static class Program
{
    // Repo local do exemplo (launcher/examples/dotnet). KOTONOHA_REPO pode
    // sobrescrever; o fallback sobe seis nivelos a partir de bin/Debug/net10.0.
    public static readonly string RepoRoot = ResolveRepoRoot();

    private static readonly string BuildRoot =
        Path.Combine(RepoRoot, "out", "build", "macos-arm64-vcpkg");

    public static readonly string NativeLibraryPath =
        Path.Combine(BuildRoot, "lib", "libKotonohaLauncherCore.dylib");

    public static readonly string BuildBin = Path.Combine(BuildRoot, "bin");

    // Uma pasta com varios jogos: o scan e' recursivo no core.
    public static readonly string[] DefaultRoots =
    {
        "/Volumes/Server/data2/Games",
        "/Volumes/Nero 2TB/KotonohaAssets",
    };

    public static readonly string[] DdlcTestRoots =
    {
        "/Volumes/Nero 2TB/KotonohaAssets/DDLC",
        "/Volumes/Server/data2/Games/DDLC",
    };

    [STAThread]
    public static int Main(string[] args)
    {
        // O motor procura assets/fonts no diretorio de trabalho.
        if (Directory.Exists(BuildBin))
            Directory.SetCurrentDirectory(BuildBin);
        EnsureNativeDependencies();
        NativeLibrary.SetDllImportResolver(
            typeof(KotonohaLauncher).Assembly, ResolveKotonohaCore);

        if (Array.Exists(args, a => a is "--selftest" or "--headless"))
            return HeadlessSelfTest.Run(args);

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        return 0;
    }

    private static string ResolveRepoRoot()
    {
        string? fromEnv = Environment.GetEnvironmentVariable("KOTONOHA_REPO");
        if (!string.IsNullOrWhiteSpace(fromEnv))
            return Path.GetFullPath(fromEnv);
        return Path.GetFullPath(Path.Combine(
            AppContext.BaseDirectory, "../../../../../.."));
    }

    // O dylib do Kotonoha referencia dependencias via @rpath e nao tem
    // LC_RPATH. Pre-carrega-las por caminho absoluto para que o dyld as
    // reutilize quando resolver o core (alternativa ao DYLD_LIBRARY_PATH).
    private static void EnsureNativeDependencies()
    {
        string lib = Path.Combine(BuildRoot, "lib");
        string vcpkg = Path.Combine(
            BuildRoot, "vcpkg_installed", "macos-arm64-vcpkg", "lib");

        string[] dependencies =
        {
            Path.Combine(vcpkg, "libSDL3.0.dylib"),
            Path.Combine(vcpkg, "libz.1.3.2.dylib"),
            Path.Combine(lib, "libKotonohaEngine.dylib"),
            Path.Combine(vcpkg, "libSDL3_ttf.0.2.2.dylib"),
            Path.Combine(vcpkg, "libavformat.63.1.102.dylib"),
            Path.Combine(vcpkg, "libavcodec.63.1.102.dylib"),
            Path.Combine(vcpkg, "libswscale.10.1.102.dylib"),
            Path.Combine(vcpkg, "libswresample.7.1.102.dylib"),
            Path.Combine(vcpkg, "libavutil.61.1.102.dylib"),
            Path.Combine(lib, "libImgui.dylib"),
        };

        foreach (string path in dependencies)
        {
            if (File.Exists(path))
                NativeLibrary.Load(path);
        }
    }

    private static IntPtr ResolveKotonohaCore(
        string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName == "KotonohaLauncherCore")
            return NativeLibrary.Load(NativeLibraryPath);
        return IntPtr.Zero;
    }

    // Configuracao do Avalonia, nao remover (usada tambem pelo designer).
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UsePlatformDetect()
        .WithInterFont()
        .LogToTrace();
}
