using System.Text.Json;
using System.Text.Json.Serialization;

namespace Kotonoha.Manager.Models;

public sealed class LaunchResult
{
    [JsonPropertyName("ok")] public bool Ok { get; set; }
    [JsonPropertyName("playable")] public bool Playable { get; set; }
    [JsonPropertyName("phase")] public string? Phase { get; set; }
    [JsonPropertyName("message")] public string? Message { get; set; }
    [JsonPropertyName("frames")] public int Frames { get; set; }
    [JsonPropertyName("dryRun")] public bool DryRun { get; set; }
    [JsonPropertyName("engineSlug")] public string? EngineSlug { get; set; }

    public static LaunchResult Parse(string json)
    {
        if (string.IsNullOrWhiteSpace(json))
            return new LaunchResult { Message = "No result from native launcher." };
        try
        {
            return JsonSerializer.Deserialize<LaunchResult>(json) ??
                   new LaunchResult { Message = "Empty launch result." };
        }
        catch (JsonException ex)
        {
            return new LaunchResult { Message = $"Invalid launch result: {ex.Message}" };
        }
    }

    public override string ToString() =>
        $"ok={Ok} playable={Playable} phase={Phase} frames={Frames} {Message}";
}

public sealed class UpscalePreset
{
    [JsonPropertyName("index")] public int Index { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = "";
}
