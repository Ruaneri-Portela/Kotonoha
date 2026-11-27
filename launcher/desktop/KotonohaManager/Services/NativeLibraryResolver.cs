using System.Reflection;
using System.Runtime.InteropServices;
using Kotonoha.Launcher;

namespace Kotonoha.Manager.Services;

/// <summary>
/// Resolves KotonohaLauncherCore.dll / .dylib / .so from a build output
/// directory. KOTONOHA_NATIVE_DIR overrides auto-detection, mirroring the
/// managed example launcher but with sensible defaults for the VS solution
/// and the CMake desktop presets.
/// </summary>
public static class NativeLibraryResolver
{
    public static string RepoRoot { get; } = ResolveRepoRoot();

    public static string? NativeDirectory { get; private set; }

    public static void Install()
    {
        var overrideDir = Environment.GetEnvironmentVariable("KOTONOHA_NATIVE_DIR");
        var candidates = new List<string>();
        if (!string.IsNullOrWhiteSpace(overrideDir)) candidates.Add(overrideDir);

        candidates.Add(Path.Combine(RepoRoot, "buildsystems", "visualstudio",
                                    "out", "x64", "Release"));
        candidates.Add(Path.Combine(RepoRoot, "buildsystems", "visualstudio",
                                    "out", "x64", "Debug"));
        candidates.Add(Path.Combine(RepoRoot, "out", "build",
                                    "windows-x64-msvc-vcpkg-release", "bin"));
        candidates.Add(Path.Combine(RepoRoot, "out", "build",
                                    "macos-arm64-vcpkg", "lib"));
        candidates.Add(Path.Combine(RepoRoot, "out", "build",
                                    "linux-x64-vcpkg", "lib"));

        NativeDirectory = candidates.FirstOrDefault(dir =>
            Directory.Exists(dir) && Directory.GetFiles(dir).Any(file =>
                Path.GetFileName(file).Contains("KotonohaLauncherCore",
                    StringComparison.OrdinalIgnoreCase)));

        if (NativeDirectory is not null)
        {
            // Os assets (fontes/shaders) ficam no output do dotnet, nao no
            // output nativo: o SDL/engine os abre relativos ao diretorio atual.
            Directory.SetCurrentDirectory(AppContext.BaseDirectory);
            PreloadDependencies(NativeDirectory);
        }

        NativeLibrary.SetDllImportResolver(typeof(KotonohaLauncher).Assembly, Resolve);
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly,
                                  DllImportSearchPath? searchPath)
    {
        if (libraryName != "KotonohaLauncherCore" || NativeDirectory is null)
            return IntPtr.Zero;
        var file = Directory.GetFiles(NativeDirectory).FirstOrDefault(path =>
            Path.GetFileName(path).Contains("KotonohaLauncherCore",
                StringComparison.OrdinalIgnoreCase));
        return file is null ? IntPtr.Zero : NativeLibrary.Load(file);
    }

    private static void PreloadDependencies(string directory)
    {
        // Windows resolves sibling DLLs via the application directory; macOS
        // needs absolute paths because the core dylib has no LC_RPATH. Loading
        // the files in name order lets dyld reuse them for @rpath references.
        var shared = Directory.GetFiles(directory)
            .Where(path => path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ||
                           path.EndsWith(".so", StringComparison.OrdinalIgnoreCase) ||
                           path.EndsWith(".dylib", StringComparison.OrdinalIgnoreCase))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase);
        foreach (var path in shared)
        {
            if (Path.GetFileName(path).Contains("KotonohaLauncherCore",
                    StringComparison.OrdinalIgnoreCase))
                continue;
            try { NativeLibrary.Load(path); }
            catch (Exception ex) when (ex is DllNotFoundException or BadImageFormatException)
            {
                // Non-loadable files (e.g. import libraries) are skipped.
            }
        }
    }

    private static string ResolveRepoRoot()
    {
        var fromEnv = Environment.GetEnvironmentVariable("KOTONOHA_REPO");
        if (!string.IsNullOrWhiteSpace(fromEnv)) return Path.GetFullPath(fromEnv);
        return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,
                                             "../../../../../../"));
    }
}
