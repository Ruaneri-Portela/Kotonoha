using System.Text.Json;

namespace Kotonoha.Manager.Services;

/// <summary>
/// User-writable settings. Never stored inside the read-only game VFS.
/// </summary>
public sealed class ManagerSettings
{
    public List<string> LibraryRoots { get; set; } = new();
    public bool SplitSavesByGame { get; set; } = true;
    public string? CustomSaveRoot { get; set; }
    public string? RendererBackend { get; set; }
    public string? UpscalerBackend { get; set; }
    public string? UpscalerPreset { get; set; }
    public string? UpscalerFactor { get; set; }

    private static string SettingsPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                     "Kotonoha", "KotonohaManager", "settings.json");

    public static ManagerSettings Load()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return new ManagerSettings();
            var json = File.ReadAllText(SettingsPath);
            return JsonSerializer.Deserialize<ManagerSettings>(json,
                       new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
                   ?? new ManagerSettings();
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            return new ManagerSettings();
        }
    }

    public void Save()
    {
        try
        {
            var path = SettingsPath;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(this,
                new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Settings are best-effort; the manager must stay usable.
        }
    }
}
