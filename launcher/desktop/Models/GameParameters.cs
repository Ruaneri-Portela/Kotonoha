using System.Text.Json.Serialization;
using Kotonoha.Launcher;

namespace Kotonoha.Manager.Models;

/// <summary>
/// Per-game launch overrides. Mirrors the "params" object of the native
/// catalog and the KotonohaGameLaunchRequest managed struct.
/// </summary>
public sealed class GameParameters
{
    [JsonPropertyName("assetOverlayPath")] public string? AssetOverlayPath { get; set; }
    [JsonPropertyName("translationPath")] public string? TranslationPath { get; set; }
    [JsonPropertyName("subtitleStylePath")] public string? SubtitleStylePath { get; set; }
    [JsonPropertyName("customDocumentsDir")] public string? CustomDocumentsDir { get; set; }
    [JsonPropertyName("ktrfPath")] public string? KtrfPath { get; set; }
    [JsonPropertyName("scriptsDir")] public string? ScriptsDir { get; set; }
    [JsonPropertyName("startScenario")] public string? StartScenario { get; set; }
    [JsonPropertyName("rendererBackend")] public string? RendererBackend { get; set; }
    [JsonPropertyName("upscalerBackend")] public string? UpscalerBackend { get; set; }
    [JsonPropertyName("upscalerPreset")] public string? UpscalerPreset { get; set; }
    [JsonPropertyName("upscalerFactor")] public string? UpscalerFactor { get; set; }

    public bool HasAnyOverride =>
        new[] { AssetOverlayPath, TranslationPath, SubtitleStylePath, CustomDocumentsDir,
                KtrfPath, ScriptsDir, StartScenario, RendererBackend, UpscalerBackend,
                UpscalerPreset, UpscalerFactor }
            .Any(value => !string.IsNullOrWhiteSpace(value));

    public KotonohaGameLaunchRequest ApplyTo(
        KotonohaGameLaunchRequest request, CatalogGame game, string? customSaveRoot,
        bool splitSavesByGame)
    {
        request.GamePath = game.Path;
        request.EngineSlug = game.EngineSlug;
        request.SaveProfileId = game.SaveProfileId;
        request.SplitSavesByGame = splitSavesByGame;
        request.CustomSaveRoot = customSaveRoot;
        request.AssetOverlayPath = AssetOverlayPath;
        request.TranslationPath = TranslationPath;
        request.SubtitleStylePath = SubtitleStylePath;
        request.CustomDocumentsDir = CustomDocumentsDir;
        request.KtrfPath = KtrfPath;
        request.ScriptsDir = ScriptsDir;
        request.StartScenario = StartScenario;
        request.RendererBackend = RendererBackend;
        request.UpscalerBackend = UpscalerBackend;
        request.UpscalerPreset = UpscalerPreset;
        request.UpscalerFactor = UpscalerFactor;
        return request;
    }

    public GameParameters Clone() => new()
    {
        AssetOverlayPath = AssetOverlayPath,
        TranslationPath = TranslationPath,
        SubtitleStylePath = SubtitleStylePath,
        CustomDocumentsDir = CustomDocumentsDir,
        KtrfPath = KtrfPath,
        ScriptsDir = ScriptsDir,
        StartScenario = StartScenario,
        RendererBackend = RendererBackend,
        UpscalerBackend = UpscalerBackend,
        UpscalerPreset = UpscalerPreset,
        UpscalerFactor = UpscalerFactor,
    };
}
