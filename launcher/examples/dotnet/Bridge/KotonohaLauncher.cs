// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// P/Invoke bridge for KotonohaLauncherCore. The core is built as a shared
// library by CMake on every native platform, so it is loadable as
// KotonohaLauncherCore.dll / .dylib / .so. All strings crossing the ABI are
// UTF-8; pointers returned by the native side must be released with FreeString.
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.ExceptionServices;
using System.Text.Json;

namespace Kotonoha.Launcher;

/// <summary>
/// Mirrors the C struct KotonohaGameLaunchRequest from LauncherBridge.h.
/// Optional fields may be null; the native side treats null as an empty value.
/// </summary>
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct KotonohaGameLaunchRequest
{
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? GamePath;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? EngineSlug;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? SaveProfileId;
    [MarshalAs(UnmanagedType.Bool)] public bool SplitSavesByGame;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? CustomSaveRoot;

    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? AssetOverlayPath;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? TranslationPath;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? SubtitleStylePath;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? CustomDocumentsDir;

    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? KtrfPath;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? ScriptsDir;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? StartScenario;

    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? RendererBackend;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? UpscalerBackend;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? UpscalerPreset;
    [MarshalAs(UnmanagedType.LPUTF8Str)] public string? UpscalerFactor;

    /// <summary>
    /// "Test launcher only": validate the request without starting SDL or a
    /// game runtime. Appended last so existing field offsets are preserved.
    /// </summary>
    [MarshalAs(UnmanagedType.Bool)] public bool DryRun;
}

/// <summary>Managed facade over the KotonohaLauncherCore C ABI.</summary>
public static class KotonohaLauncher
{
    private const string Library = "KotonohaLauncherCore";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherScanRootsJson(
        IntPtr[] roots, int rootCount,
        [MarshalAs(UnmanagedType.Bool)] bool splitSavesByGame,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? customSaveRoot);

    /// <summary>Native callback: game JSON must be released with FreeString.</summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void GameFoundCallback(IntPtr gameJson, IntPtr userData);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern void KotonohaLauncherScanRootsJsonWithCallback(
        IntPtr[] roots, int rootCount,
        [MarshalAs(UnmanagedType.Bool)] bool splitSavesByGame,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? customSaveRoot,
        int maxDepth, GameFoundCallback callback, IntPtr userData);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherDetectPathJson(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
        [MarshalAs(UnmanagedType.Bool)] bool splitSavesByGame,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? customSaveRoot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherLoadGameParamsJson(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string saveProfileId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool KotonohaLauncherSaveGameParams(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string saveProfileId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? assetOverlayPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? translationPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? subtitleStylePath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? customDocumentsDir,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? ktrfPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? scriptsDir,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? startScenario,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? rendererBackend,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? upscalerBackend,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? upscalerPreset,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? upscalerFactor);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherLaunchGame(
        ref KotonohaGameLaunchRequest request);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherUpscalePresetsJson();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherDefaultSchoolDaysRoute();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr KotonohaLauncherResolveSaveDirectory(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gamePath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? engineSlug,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? saveProfileId,
        [MarshalAs(UnmanagedType.Bool)] bool splitSavesByGame,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? customSaveRoot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi)]
    private static extern int KotonohaLauncherValidateRoute(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string ktrfPath,
        IntPtr infoBuf, int infoBufSize);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern void KotonohaLauncherFreeString(IntPtr ptr);

    /// <summary>Convert a native UTF-8 result and always release it.</summary>
    private static string TakeString(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero) return string.Empty;
        try { return Marshal.PtrToStringUTF8(ptr) ?? string.Empty; }
        finally { KotonohaLauncherFreeString(ptr); }
    }

    /// <summary>Scan library roots and return the discovered games as JSON.</summary>
    public static string ScanRoots(string[] roots, bool splitSavesByGame = false,
                                   string? customSaveRoot = null)
    {
        var native = new IntPtr[roots.Length];
        try
        {
            for (int i = 0; i < roots.Length; ++i)
                native[i] = Marshal.StringToCoTaskMemUTF8(roots[i] ?? string.Empty);
            return TakeString(KotonohaLauncherScanRootsJson(
                native, native.Length, splitSavesByGame, customSaveRoot));
        }
        finally
        {
            foreach (var ptr in native) Marshal.FreeCoTaskMem(ptr);
        }
    }

    /// <summary>
    /// Scan roots and invoke `onGame` for each final resolved catalog entry
    /// on the calling thread. maxDepth=0 visits the root and direct children.
    /// </summary>
    public static void ScanRoots(string[] roots, Action<string> onGame,
                                 bool splitSavesByGame = false,
                                 string? customSaveRoot = null, int maxDepth = 0)
    {
        ArgumentNullException.ThrowIfNull(onGame);
        var native = new IntPtr[roots.Length];
        ExceptionDispatchInfo? callbackError = null;
        GameFoundCallback callback = (json, _) =>
        {
            if (json == IntPtr.Zero) return;
            try
            {
                if (callbackError is null)
                    onGame(Marshal.PtrToStringUTF8(json) ?? string.Empty);
            }
            catch (Exception ex)
            {
                // Managed exceptions must never unwind through the native ABI.
                callbackError ??= ExceptionDispatchInfo.Capture(ex);
            }
            finally { KotonohaLauncherFreeString(json); }
        };
        try
        {
            for (int i = 0; i < roots.Length; ++i)
                native[i] = Marshal.StringToCoTaskMemUTF8(roots[i] ?? string.Empty);
            KotonohaLauncherScanRootsJsonWithCallback(
                native, native.Length, splitSavesByGame, customSaveRoot,
                maxDepth, callback, IntPtr.Zero);
            GC.KeepAlive(callback);
            callbackError?.Throw();
        }
        finally
        {
            foreach (var ptr in native) Marshal.FreeCoTaskMem(ptr);
        }
    }

    /// <summary>Detect one game folder or disc image as a JSON object.</summary>
    public static string DetectPath(string path, bool splitSavesByGame = false,
                                    string? customSaveRoot = null) =>
        TakeString(KotonohaLauncherDetectPathJson(
            path, splitSavesByGame, customSaveRoot));

    /// <summary>Load persisted parameters for a save profile.</summary>
    public static string LoadGameParams(string saveProfileId) =>
        TakeString(KotonohaLauncherLoadGameParamsJson(saveProfileId));

    /// <summary>Persist per-game launch parameters.</summary>
    public static bool SaveGameParams(
        string saveProfileId, string? assetOverlayPath = null,
        string? translationPath = null, string? subtitleStylePath = null,
        string? customDocumentsDir = null, string? ktrfPath = null,
        string? scriptsDir = null, string? startScenario = null,
        string? rendererBackend = null, string? upscalerBackend = null,
        string? upscalerPreset = null, string? upscalerFactor = null) =>
        KotonohaLauncherSaveGameParams(
            saveProfileId, assetOverlayPath, translationPath,
            subtitleStylePath, customDocumentsDir, ktrfPath, scriptsDir,
            startScenario, rendererBackend, upscalerBackend, upscalerPreset,
            upscalerFactor);

    /// <summary>
    /// Launches one game in-process. Returns JSON with ok, playable, phase,
    /// message, frames and dryRun fields. Set request.DryRun=true ("Test
    /// launcher only"), or KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN=1, to validate
    /// without starting SDL. By default the real SDL drivers are used and the
    /// window stays open until the user quits; CI can opt into
    /// KOTONOHA_LAUNCHER_LAUNCH_HEADLESS=1 for a capped offscreen run.
    /// </summary>
    public static string LaunchGame(KotonohaGameLaunchRequest request) =>
        TakeString(KotonohaLauncherLaunchGame(ref request));

    /// <summary>Convenience overload for the most common launch fields.</summary>
    public static string LaunchGame(
        string gamePath, string engineSlug = "", string saveProfileId = "",
        bool splitSavesByGame = true, string? customSaveRoot = null) =>
        LaunchGame(new KotonohaGameLaunchRequest
        {
            GamePath = gamePath,
            EngineSlug = engineSlug,
            SaveProfileId = saveProfileId,
            SplitSavesByGame = splitSavesByGame,
            CustomSaveRoot = customSaveRoot,
        });

    /// <summary>
    /// Windows "fork": starts the game in a RUNDLL32 host process so SDL runs
    /// on that process main thread. stdin/stdout/stderr are inherited and
    /// captured by the caller; the manager does not exit.
    /// </summary>
    public static Process LaunchGameDetached(KotonohaGameLaunchRequest request,
                                             string launcherDllPath)
    {
        var json = JsonSerializer.Serialize(new
        {
            request.GamePath, request.EngineSlug, request.SaveProfileId,
            request.SplitSavesByGame, request.CustomSaveRoot,
            request.AssetOverlayPath, request.TranslationPath,
            request.SubtitleStylePath, request.CustomDocumentsDir,
            request.KtrfPath, request.ScriptsDir, request.StartScenario,
            request.RendererBackend, request.UpscalerBackend,
            request.UpscalerPreset, request.UpscalerFactor, request.DryRun
        });
        var psi = new ProcessStartInfo("rundll32.exe")
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        psi.Arguments = $"\"{launcherDllPath}\",KotonohaLauncherRunDll";
        psi.Environment["KOTONOHA_LAUNCH_REQUEST_JSON"] = json;
        return Process.Start(psi)
               ?? throw new InvalidOperationException("rundll32 nao iniciou");
    }

    /// <summary>List upscaler presets as JSON.</summary>
    public static string UpscalePresets() =>
        TakeString(KotonohaLauncherUpscalePresetsJson());

    /// <summary>Return the bundled default School Days route path.</summary>
    public static string DefaultSchoolDaysRoute() =>
        TakeString(KotonohaLauncherDefaultSchoolDaysRoute());

    /// <summary>Resolve the isolated save directory for a profile.</summary>
    public static string ResolveSaveDirectory(
        string gamePath, string? engineSlug = null, string? saveProfileId = null,
        bool splitSavesByGame = false, string? customSaveRoot = null) =>
        TakeString(KotonohaLauncherResolveSaveDirectory(
            gamePath, engineSlug, saveProfileId, splitSavesByGame,
            customSaveRoot));

    /// <summary>Validate a School Days route; returns (valid, diagnostics).</summary>
    public static (bool Valid, string Info) ValidateRoute(string ktrfPath)
    {
        var buffer = Marshal.AllocHGlobal(1024);
        try
        {
            int ok = KotonohaLauncherValidateRoute(ktrfPath, buffer, 1024);
            return (ok != 0, Marshal.PtrToStringUTF8(buffer) ?? string.Empty);
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }
}
