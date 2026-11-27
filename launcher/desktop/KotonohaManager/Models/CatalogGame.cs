using System.Text.Json.Serialization;

namespace Kotonoha.Manager.Models;

/// <summary>
/// One entry of the JSON catalog produced by KotonohaLauncherScanRootsJson.
/// Field names match launcher/core/LauncherBridge.cpp exactly.
/// </summary>
public sealed class CatalogGame
{
    [JsonPropertyName("title")] public string Title { get; set; } = "";
    [JsonPropertyName("saveProfileId")] public string SaveProfileId { get; set; } = "";
    [JsonPropertyName("path")] public string Path { get; set; } = "";
    [JsonPropertyName("overlayPath")] public string? OverlayPath { get; set; }
    [JsonPropertyName("engineSlug")] public string EngineSlug { get; set; } = "";
    [JsonPropertyName("engineDisplayName")] public string EngineDisplayName { get; set; } = "";
    [JsonPropertyName("evidence")] public string? Evidence { get; set; }
    [JsonPropertyName("libraryTarget")] public string? LibraryTarget { get; set; }
    [JsonPropertyName("targetBinary")] public string? TargetBinary { get; set; }
    [JsonPropertyName("packageId")] public string? PackageId { get; set; }
    [JsonPropertyName("saveDirectory")] public string? SaveDirectory { get; set; }
    [JsonPropertyName("isDiscImage")] public bool IsDiscImage { get; set; }
    [JsonPropertyName("linkedInProcess")] public bool LinkedInProcess { get; set; }
    [JsonPropertyName("isSchoolDays")] public bool IsSchoolDays { get; set; }
    [JsonPropertyName("params")] public GameParameters? Parameters { get; set; }
}
