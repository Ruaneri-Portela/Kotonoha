namespace KotonohaBridgeTest;

/// <summary>
/// One row from KotonohaLauncherScanRootsJson. Every field is passed into the
/// KotonohaGameLaunchRequest built by MainWindow before LaunchGame is called.
/// </summary>
public sealed class GameEntry
{
    public string Title { get; init; } = "";
    public string SaveProfileId { get; init; } = "";
    public string Path { get; init; } = "";
    public string OverlayPath { get; init; } = "";
    public string EngineSlug { get; init; } = "";
    public string AssetOverlayPath { get; init; } = "";
    public string TranslationPath { get; init; } = "";
    public string SubtitleStylePath { get; init; } = "";
    public string CustomDocumentsDir { get; init; } = "";
    public string KtrfPath { get; init; } = "";
    public string ScriptsDir { get; init; } = "";
    public string StartScenario { get; init; } = "";
    public string RendererBackend { get; init; } = "";
    public string UpscalerBackend { get; init; } = "";
    public string UpscalerPreset { get; init; } = "";
    public string UpscalerFactor { get; init; } = "";
}
