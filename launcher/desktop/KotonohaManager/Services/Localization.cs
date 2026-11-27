using System.ComponentModel;

namespace Kotonoha.Manager.Services;

/// <summary>
/// Idiomas do gerenciador. Uma mudanca notifica todas as propriedades para
/// que os bindings XAML sejam atualizados sem recriar a janela.
/// </summary>
public sealed class Localization : INotifyPropertyChanged
{
    public static Localization Instance { get; } = new();

    private string _language = "pt";
    public string Language
    {
        get => _language;
        set
        {
            if (_language == value) return;
            _language = value;
            OnPropertyChanged(null);
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged(string? name) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name ?? string.Empty));

    private string T(string pt, string en) => _language == "en" ? en : pt;

    public string AppTitle => T("Kotonoha — Gerenciador de jogos", "Kotonoha — Game Manager");
    public string SearchPlaceholder => T("Buscar jogos ou engines...", "Search games or engines...");
    public string Rescan => T("Rescan", "Rescan");
    public string AddFolder => T("+ Pasta", "+ Folder");
    public string AddDisc => T("+ ISO/BIN", "+ ISO/BIN");
    public string AddZip => T("+ ZIP", "+ ZIP");
    public string Settings => T("Ajustes", "Settings");
    public string Quit => T("Sair", "Quit");
    public string Library => T("Biblioteca", "Library");
    public string Play => T("Jogar", "Play");
    public string Stop => T("Encerrar", "Stop");
    public string Parameters => T("Parâmetros...", "Parameters...");
    public string Path => T("Caminho", "Path");
    public string SaveDirectory => T("Diretório de saves", "Save directory");
    public string ExecutionLog => T("Log de execução", "Execution log");
    public string Found => T("jogo(s) encontrado(s).", "game(s) found.");
    public string Searching => T("Procurando jogos...", "Searching for games...");
    public string Ready => T("Pronto.", "Ready.");
    public string Running => T("em execução", "running");
    public string Exited => T("encerrado", "exited");
    public string LaunchFailed => T("Falha ao iniciar", "Failed to start");
    public string ScanFailed => T("Falha no scan", "Scan failed");
    public string FoundOne => T("Encontrado", "Found");

    public string SettingsTitle => T("Ajustes do gerenciador", "Manager settings");
    public string Saves => T("Saves", "Saves");
    public string IsolateSaves => T("Isolar saves por jogo", "Isolate saves per game");
    public string CustomSaveRoot => T("Raiz personalizada", "Custom save root");
    public string GraphicsDefaults => T("Padrões gráficos", "Graphics defaults");
    public string Renderer => T("Renderer", "Renderer");
    public string Upscaler => T("Upscaler", "Upscaler");
    public string Preset => T("Preset", "Preset");
    public string Factor => T("Fator de escala", "Scale factor");
    public string Global => T("Padrão global", "Global default");
    public string Browse => T("Procurar...", "Browse...");
    public string Cancel => T("Cancelar", "Cancel");
    public string SaveBtn => T("Salvar", "Save");
    public string Reset => T("Restaurar padrões", "Reset to defaults");
    public string Saved => T("Parâmetros salvos.", "Parameters saved.");
    public string SaveFailed => T("Falha ao salvar parâmetros.", "Failed to save parameters.");
    public string SettingsSaved => T("Ajustes salvos.", "Settings saved.");
    public string RootAdded => T("Raiz adicionada", "Root added");
    public string SelectFolder => T("Selecionar pasta", "Select folder");
    public string SelectFile => T("Selecionar arquivo", "Select file");
    public string RouteFile => T("Rota", "Route");

    public string ParametersTitle => T("Parâmetros", "Parameters");
    public string OverlaysLocalization => T("Overlays e localização", "Overlays and localization");
    public string AssetOverlay => T("Asset overlay", "Asset overlay");
    public string Translation => T("Tradução", "Translation");
    public string SubtitleStyle => T("Estilo de legendas", "Subtitle style");
    public string SchoolDaysHQ => T("School Days HQ", "School Days HQ");
    public string RouteKtrf => T("Rota (.ktrf)", "Route (.ktrf)");
    public string Validate => T("Validar", "Validate");
    public string ScriptsDir => T("Diretório de scripts", "Scripts directory");
    public string SavesStartup => T("Saves e inicialização", "Saves and startup");
    public string DocumentsDir => T("Diretório de documentos", "Documents directory");
    public string StartScenario => T("Cenário inicial", "Start scenario");
    public string GraphicsOverrides => T("Gráficos (sobrescrita por jogo)", "Graphics (per-game override)");
    public string UseGlobal => T("Usar ajuste global", "Use global setting");
    public string RouteInfo => T("Informação da rota aparece aqui.", "Route information appears here.");
    public string NoRoute => T("Nenhuma rota .ktrf informada.", "No .ktrf route provided.");
    public string RouteValid => T("Válida", "Valid");
    public string RouteInvalid => T("Inválida", "Invalid");
    public string Restored => T("Parâmetros restaurados para os padrões globais.", "Parameters restored to global defaults.");
}
