#include <Kotonoha/routing/SchoolDaysLauncherLocale.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <algorithm>
#include <cctype>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>

namespace Kotonoha::SchoolDaysLauncherLocale {
namespace {

std::string normalizeToken(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (unsigned char c : raw) {
    if (std::isspace(c))
      continue;
    if (c == '_')
      out.push_back('-');
    else
      out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

using Table = std::array<const char *, kKeyCount>;

constexpr Table kEnglish{{
    "School Days HQ - Launcher",
    "PLAY",
    "OPTIONS",
    "QUIT",
    "Launcher music: ON",
    "Launcher music: OFF",
    "Launcher language",
    "Changes only the launcher interface language (independent of game translation).",
    "Missing assets/SchoolDaysHQ-Launcher.png",
    "SETUP / OPTIONS",
    "Display",
    "Graphics",
    "Audio",
    "General",
    "Language",
    "Game Files",
    "Advanced",
    "Reset",
    "OK",
    "Cancel",
    "Default options are staged. Select OK to save them.",

    // Display
    "Display Options (same settings as in-game)",
    "Android uses a fullscreen Surface controlled by the device. Windowed, exclusive fullscreen and monitor switching do not apply on phones.",
    "Device default",
    "Native resolution / refresh",
    "The device did not publish display modes.",
    "Render resolution is adjusted separately in Graphics.",
    "The selected monitor, mode, refresh, upscaler and renderer are saved in settings.json and used by in-game Display Options.",
    "Window mode",
    "Windowed",
    "Borderless fullscreen",
    "Exclusive fullscreen",
    "Monitor",
    "Display",
    "Unavailable",
    "Monitor",
    "Borderless uses the current desktop mode; choosing a different resolution below requests Exclusive fullscreen.",
    " (desktop)",
    " (window)",
    "Resolution / refresh",
    "No display modes were returned for this monitor.",
    "Custom window size",
    "VSync",
    "Resizable window",

    // Graphics
    "Renderer and upscaling",
    "Render scale",
    "Renderer: OpenGL ES (device-native)",
    "Renderer",
    "Automatic",
    "Upscaler",
    "Auto",
    "None",
    "Linear",
    "MPV",
    "Upscale factor",
    "Upscaling is OFF at 1.0x, regardless of preset.",
    "Custom GLSL (external file)",
    "Shader preset",
    "Choosing a shader preset selects OpenGL + MPV and >=2.0x.",
    "%s uses Linear with Auto: Anime4K/ArtCNN GLSL presets are NOT active. Select a shader preset to switch to OpenGL + MPV, or keep this renderer for Linear scaling.",
    "Automatic renderer: the active upscaler depends on the driver SDL chooses. GLSL requires OpenGL + MPV.",
    "Custom GLSL paths",
    "Enter a .glsl / .hook / .frag FILE, not its folder.",
    "FSRCNNX x2 needs >1.4x; x3 >2.3x; x4 >3.3x.",
    "A folder name is not a shader. Select the .glsl FILE.",
    "Custom GLSL paths are saved but inactive with this renderer/backend or 1.0x factor.",
    "Additional engine upscaling settings",

    // Audio
    "Launcher-only music",
    "Play music while launcher is open",
    "Does not change the Title music in the game.",
    "Audio (retail Config.DAT)",
    "The game host has not provided a Config.DAT profile; retail options are unavailable to avoid modifying another player's saves.",
    "BGM volume",
    "SE volume",
    "Voice volume",
    "Male voices",
    "Mute game audio",
    "Master volume",
    "Movie volume",
    "Retail MasterVolume is intentionally left untouched: its original load-back semantics are not authenticated.",

    // General
    "General options",
    "The retail Config.DAT profile is unavailable.",
    "Show text",
    "Skip unread scenes",
    "SuperSkip / next choice",
    "Auto draw",
    "Auto mode",
    "Swap controller A/B",
    "Forced subtitles",
    "Optional expansion",
    "Enable L×H endings",
    "Enables the seven additional epilogues when the required files are available.",
    "Status: %s",
    "Automatic",
    "4:3",
    "16:9",
    "Narrative aspect",
    "Narrative aspect changes the authored story layout only; monitor resolution remains in settings.json.",
    "Retail General/Sound defaults",

    // LxH dynamic status
    "L×H active (7 epilogues ready)",
    "Base HQ (L×H disabled)",
    "Base HQ (Packs/LxH.GPK not found)",
    "Base HQ (L×H translation unavailable for selected language)",
    "Game folder is not selected",

    // Language tab
    "School Days HQ game language",
    "Select a language",
    "Game language",
    "Format: %s",
    "Spanish GPK requires a certified Script - Spanish.GPK archive.",
    "Chinese GPK requires Script - chinese-compatible.GPK.",
    "Russian GPK requires its certified Script - Russian.GPK archive.",
    "HQ: original Packs/Script.GPK (never duplicated).",
    "Base HQ GPK: %s",
    "Seven L×H endings GPK: %s",
    "Missing original HQ language script in translations/scripts.",
    "Optional L×H script %s not found in translations/scripts. Base School Days HQ will play in %s without L×H endings.",
    "Retail-only edition: original 29 GPKs; no L×H epilogues or translations.",
    "Original 29 retail GPKs and optional L×H media stay untouched.",
    "L×H status: %s",
    "Language texts are read from scripts, not converted to JSON.",
    "English uses the original School Days HQ dialogue.",
    "Base HQ JSON: %s",
    "Play checks dialogue, choices and reviewed source-identical phrases. Missing entries do not fall back to English.",
    "Base language JSON missing. Choose the matching HQ JSON before Play.",
    "The JSON list contains installed language profiles only.",
    "L×H epilogue translation (7 endings)",
    "L×H JSON (%s): %s",
    "auto-matched to base",
    "manual override",
    "Not found",
    "Choose L×H JSON...",
    "Use matching L×H automatically",
    "The L×H videos remain in Packs/LxH.GPK. Only subtitle JSON changes; the base and L×H JSON are validated together before Play.",

    // Game Files & Advanced
    "Imported School Days HQ data",
    "Original 29 HQ GPKs and LxH.GPK are imported using the Android file picker, before this launcher opens. They remain outside the APK and are not modified.",
    "Active Packs folder: %s",
    "Routing profile: %s",
    "To import a new language, close the app and use the Android setup screen.",
    "A content:// URI is not a disk path and cannot be entered into the desktop Folder/ISO field.",
    "School Days HQ original game files",
    "Select the folder of your legitimate School Days HQ installation (or its Packs folder). Original game assets are not included.",
    "IMPORTANT: Do NOT select this distribution folder or add \\Packs to its path if it contains no retail packs. Click Folder... and select your ORIGINAL School Days HQ installation containing the 29 original retail GPKs (optional LxH.GPK is supported when present). No save files are required.",
    "Game folder or Packs folder",
    "Use your own School Days HQ retail data (29 GPKs), or an extracted directory with Script/English, or an ISO containing these files.",
    "Folder...",
    "ISO...",
    "Browse...",
    "Route file (.ktnroute)",
    "Select a compatible School Days HQ KTRF route.",
    "Data files and routing",
    "Asset overlay",
    "Overlay folder",
    "Choose overlay folder...",
    "Automatic overlay",
    "Advanced engine settings",

    // Validation errors
    "Custom GLSL must be the full path to a .glsl/.hook/.frag file, not a folder (e.g. FSRCNNX_x2_16-0-4-1.glsl).",
    "Custom GLSL file not found: ",
    "Custom GLSL path is invalid; select an existing shader file.",
    "MPV/Anime4K/ArtCNN requires the OpenGL renderer. Select OpenGL or change Upscaler to Auto/Linear.",
    "Native GPK languages need a persistent School Days settings path.",
    "Native GPK mode cannot be combined with a base JSON dictionary.",
    "Selected translated Script.GPK is not compatible with the School Days HQ retail ORS names. A converted pack is needed.",
    "Selected non-English locale has no valid base JSON. Choose a translation file in Game language before Play.",
    "Manually selected L×H JSON is missing. Choose a valid file or restore automatic matching.",
    "Selected language JSON contains no translated entries.",
    "Discord Rich Presence",
}};

constexpr Table kPortugueseBr{{
    "School Days HQ - Launcher",
    "JOGAR",
    "OPÇÕES",
    "SAIR",
    "Música da launcher: LIGADA",
    "Música da launcher: DESLIGADA",
    "Idioma da launcher",
    "Altera apenas o idioma da interface da launcher (independente da tradução do jogo).",
    "Arquivo ausente: assets/SchoolDaysHQ-Launcher.png",
    "CONFIGURAÇÃO / OPÇÕES",
    "Tela",
    "Gráficos",
    "Áudio",
    "Geral",
    "Idioma",
    "Arquivos do jogo",
    "Avançado",
    "Restaurar",
    "OK",
    "Cancelar",
    "Opções padrão preparadas. Clique em OK para salvar.",

    // Display
    "Opções de tela (mesmas configurações do jogo)",
    "O Android usa uma Surface em tela cheia controlada pelo aparelho. Modo janela, tela cheia exclusiva e troca de monitor não se aplicam a celulares.",
    "Padrão do dispositivo",
    "Resolução nativa / taxa de atualização",
    "O dispositivo não publicou modos de exibição.",
    "A escala de renderização é ajustada separadamente em Gráficos.",
    "O monitor, modo, taxa de atualização, upscaler e renderizador selecionados são salvos em settings.json e usados nas Opções de Tela do jogo.",
    "Modo de janela",
    "Janela",
    "Tela cheia sem bordas",
    "Tela cheia exclusiva",
    "Monitor",
    "Monitor",
    "Indisponível",
    "Monitor",
    "Tela cheia sem bordas usa o modo atual da área de trabalho; escolher outra resolução abaixo solicita Tela cheia exclusiva.",
    " (área de trabalho)",
    " (janela)",
    "Resolução / taxa de atualização",
    "Nenhum modo de exibição foi retornado para este monitor.",
    "Tamanho personalizado da janela",
    "VSync",
    "Janela redimensionável",

    // Graphics
    "Renderizador e upscaling",
    "Escala de renderização",
    "Renderizador: OpenGL ES (nativo do dispositivo)",
    "Renderizador",
    "Automático",
    "Upscaler",
    "Automático",
    "Nenhum",
    "Linear",
    "MPV",
    "Fator de upscale",
    "Em 1.0x o upscale fica DESLIGADO, independentemente do preset.",
    "GLSL personalizado (arquivo externo)",
    "Preset de shader",
    "Escolher um preset de shader seleciona OpenGL + MPV e fator >=2.0x.",
    "%s usa Linear com Automático: presets GLSL Anime4K/ArtCNN NÃO estão ativos. Escolha um preset de shader para mudar para OpenGL + MPV ou mantenha este renderizador para escala Linear.",
    "Renderizador automático: o upscaler ativo depende do driver escolhido pelo SDL. GLSL requer OpenGL + MPV.",
    "Caminhos GLSL personalizados",
    "Informe um ARQUIVO .glsl / .hook / .frag, não a pasta.",
    "FSRCNNX x2 exige >1.4x; x3 >2.3x; x4 >3.3x.",
    "Um nome de pasta não é um shader. Selecione o ARQUIVO .glsl.",
    "Os caminhos GLSL personalizados foram salvos, mas estão inativos com este renderizador/backend ou fator 1.0x.",
    "Configurações adicionais de upscaling do motor",

    // Audio
    "Música exclusiva da launcher",
    "Tocar música enquanto a launcher estiver aberta",
    "Não altera a música da tela inicial do jogo.",
    "Áudio (Config.DAT original)",
    "O host do jogo não forneceu um perfil Config.DAT; opções originais ficam indisponíveis para não alterar saves de outro jogador.",
    "Volume de BGM",
    "Volume de SE",
    "Volume de vozes",
    "Vozes masculinas",
    "Silenciar áudio do jogo",
    "Volume principal",
    "Volume de vídeos",
    "O MasterVolume original é preservado intacto intencionalmente.",

    // General
    "Opções gerais",
    "O perfil Config.DAT original não está disponível.",
    "Exibir texto / legendas",
    "Pular cenas não lidas",
    "SuperSkip / próxima escolha",
    "Desenho automático (Auto draw)",
    "Modo automático (Auto mode)",
    "Inverter botões A/B do controle",
    "Legendas forçadas (.ass)",
    "Expansão opcional",
    "Ativar finais L×H",
    "Ativa os sete epílogos adicionais quando os arquivos necessários estiverem disponíveis.",
    "Status: %s",
    "Automático",
    "4:3",
    "16:9",
    "Proporção narrativa",
    "A proporção narrativa altera apenas o enquadramento da história; a resolução do monitor permanece em settings.json.",
    "Padrões de Geral/Áudio originais",

    // LxH dynamic status
    "L×H ativo (7 epílogos prontos)",
    "HQ base (L×H desativado)",
    "HQ base (Packs/LxH.GPK não encontrado)",
    "HQ base (tradução L×H indisponível para o idioma selecionado)",
    "Pasta do jogo não selecionada",

    // Language tab
    "Idioma do jogo (School Days HQ)",
    "Selecione um idioma",
    "Idioma do jogo",
    "Formato: %s",
    "O GPK em espanhol exige o arquivo certificado Script - Spanish.GPK.",
    "O GPK em chinês exige Script - chinese-compatible.GPK.",
    "O GPK em russo exige o arquivo certificado Script - Russian.GPK.",
    "HQ: Packs/Script.GPK original (nunca duplicado).",
    "GPK do HQ base: %s",
    "GPK dos sete finais L×H: %s",
    "Script original de idioma do HQ ausente em translations/scripts.",
    "Script opcional L×H %s não encontrado em translations/scripts. O School Days HQ base será executado em %s sem os finais L×H.",
    "Edição somente retail: 29 GPKs originais; sem epílogos ou traduções L×H.",
    "Os 29 GPKs originais e a mídia opcional L×H permanecem intactos.",
    "Status L×H: %s",
    "Os textos do idioma são lidos dos scripts, sem conversão para JSON.",
    "O inglês utiliza os diálogos originais do School Days HQ.",
    "JSON do HQ base: %s",
    "Ao jogar, o motor verifica diálogos, escolhas e frases revisadas. Entradas ausentes não caem silenciosamente para inglês.",
    "JSON do idioma base ausente. Escolha o JSON correspondente antes de jogar.",
    "A lista JSON exibe apenas perfis de idioma instalados.",
    "Tradução dos epílogos L×H (7 finais)",
    "JSON L×H (%s): %s",
    "associado automaticamente à base",
    "seleção manual",
    "Não encontrado",
    "Escolher JSON L×H...",
    "Usar L×H correspondente automaticamente",
    "Os vídeos L×H permanecem em Packs/LxH.GPK. Apenas o JSON de legendas muda; o JSON base e o L×H são validados juntos antes de jogar.",

    // Game Files & Advanced
    "Dados importados do School Days HQ",
    "Os 29 GPKs originais e LxH.GPK são acessados via seletor de pastas SAF do Android. Eles permanecem fora do APK e não são modificados.",
    "Pasta Packs ativa: %s",
    "Perfil de rota: %s",
    "Para alterar o idioma, feche o jogo e use a tela de configuração do Android.",
    "Um URI content:// não é um caminho de disco local.",
    "Arquivos originais do School Days HQ",
    "Selecione a pasta da sua instalação legítima do School Days HQ (ou sua pasta Packs). Os arquivos originais do jogo não estão incluídos.",
    "IMPORTANTE: NÃO selecione esta pasta de distribuição se ela ainda não tiver os pacotes retail. Clique em Pasta... e selecione a instalação ORIGINAL do School Days HQ com os 29 GPKs originais (LxH.GPK opcional é suportado quando presente). Nenhum save é exigido.",
    "Pasta do jogo ou pasta Packs",
    "Use seus próprios dados originais do School Days HQ (29 GPKs), um diretório extraído com Script/English ou uma ISO compatível.",
    "Pasta...",
    "ISO...",
    "Procurar...",
    "Arquivo de rota (.ktnroute)",
    "Selecione uma rota KTRF compatível do School Days HQ.",
    "Arquivos de dados e roteamento",
    "Overlay de assets",
    "Pasta de overlay",
    "Escolher pasta de overlay...",
    "Overlay automático",
    "Configurações avançadas do motor",

    // Validation errors
    "GLSL personalizado deve apontar para um arquivo .glsl/.hook/.frag, não uma pasta (ex.: FSRCNNX_x2_16-0-4-1.glsl).",
    "Arquivo GLSL personalizado não encontrado: ",
    "Caminho GLSL personalizado inválido; selecione um arquivo de shader existente.",
    "MPV/Anime4K/ArtCNN requer o renderizador OpenGL. Selecione OpenGL ou altere o Upscaler para Automático/Linear.",
    "Idiomas GPK nativos exigem um caminho persistente de configurações.",
    "O modo GPK nativo não pode ser combinado com um dicionário JSON base.",
    "O Script.GPK traduzido selecionado não é compatível com os nomes ORS originais do School Days HQ.",
    "O idioma selecionado não possui um JSON base válido. Escolha um arquivo de tradução em Idioma antes de jogar.",
    "O JSON L×H selecionado manualmente está ausente. Escolha um arquivo válido ou restaure a associação automática.",
    "O JSON de idioma selecionado não contém entradas traduzidas.",
    "Discord Rich Presence (atividade no perfil)",
}};

constexpr Table kSpanish{{
    "School Days HQ - Launcher",
    "JUGAR",
    "OPCIONES",
    "SALIR",
    "Música del launcher: SÍ",
    "Música del launcher: NO",
    "Idioma del launcher",
    "Cambia solo el idioma de la interfaz del launcher (independiente de la traducción del juego).",
    "Falta assets/SchoolDaysHQ-Launcher.png",
    "CONFIGURACIÓN / OPCIONES",
    "Pantalla",
    "Gráficos",
    "Audio",
    "General",
    "Idioma",
    "Archivos del juego",
    "Avanzado",
    "Restablecer",
    "Aceptar",
    "Cancelar",
    "Valores predeterminados listos. Pulsa Aceptar para guardar.",

    // Display
    "Opciones de pantalla (mismos ajustes que en el juego)",
    "Android usa una Surface en pantalla completa controlada por el dispositivo. El modo ventana, pantalla completa exclusiva y cambio de monitor no aplican en teléfonos.",
    "Predeterminado del dispositivo",
    "Resolución nativa / frecuencia",
    "El dispositivo no informó modos de pantalla.",
    "La escala de renderizado se ajusta por separado en Gráficos.",
    "El monitor, modo, frecuencia, upscaler y renderizador elegidos se guardan en settings.json y se usan en las Opciones de pantalla del juego.",
    "Modo de ventana",
    "Ventana",
    "Pantalla completa sin bordes",
    "Pantalla completa exclusiva",
    "Monitor",
    "Pantalla",
    "No disponible",
    "Monitor",
    "Sin bordes usa el modo actual del escritorio; elegir otra resolución abajo solicita Pantalla completa exclusiva.",
    " (escritorio)",
    " (ventana)",
    "Resolución / frecuencia",
    "No se devolvieron modos de pantalla para este monitor.",
    "Tamaño personalizado de ventana",
    "VSync",
    "Ventana redimensionable",

    // Graphics
    "Renderizador y escalado",
    "Escala de renderizado",
    "Renderizador: OpenGL ES (nativo del dispositivo)",
    "Renderizador",
    "Automático",
    "Upscaler",
    "Automático",
    "Ninguno",
    "Linear",
    "MPV",
    "Factor de escalado",
    "En 1.0x el escalado está DESACTIVADO sin importar el preset.",
    "GLSL personalizado (archivo externo)",
    "Preset de shader",
    "Elegir un preset de shader selecciona OpenGL + MPV y factor >=2.0x.",
    "%s usa Linear con Automático: los presets GLSL Anime4K/ArtCNN NO están activos. Elige un preset de shader para cambiar a OpenGL + MPV o mantén este renderizador para escala Linear.",
    "Renderizador automático: el upscaler activo depende del controlador elegido por SDL. GLSL requiere OpenGL + MPV.",
    "Rutas GLSL personalizadas",
    "Introduce un ARCHIVO .glsl / .hook / .frag, no su carpeta.",
    "FSRCNNX x2 requiere >1.4x; x3 >2.3x; x4 >3.3x.",
    "Un nombre de carpeta no es un shader. Selecciona el ARCHIVO .glsl.",
    "Las rutas GLSL personalizadas están guardadas pero inactivas con este renderizador/backend o factor 1.0x.",
    "Ajustes adicionales de escalado del motor",

    // Audio
    "Música solo del launcher",
    "Reproducir música mientras el launcher está abierto",
    "No cambia la música de la pantalla de título del juego.",
    "Audio (Config.DAT original)",
    "El host del juego no proporcionó un perfil Config.DAT; las opciones originales no están disponibles para evitar alterar partidas de otro jugador.",
    "Volumen de BGM",
    "Volumen de SE",
    "Volumen de voces",
    "Voces masculinas",
    "Silenciar audio del juego",
    "Volumen maestro",
    "Volumen de vídeos",
    "MasterVolume original se mantiene intacto intencionalmente.",

    // General
    "Opciones generales",
    "El perfil Config.DAT original no está disponible.",
    "Mostrar texto / subtítulos",
    "Saltar escenas no leídas",
    "SuperSkip / siguiente decisión",
    "Dibujo automático (Auto draw)",
    "Modo automático (Auto mode)",
    "Intercambiar botones A/B del mando",
    "Subtítulos forzados (.ass)",
    "Expansión opcional",
    "Activar finales L×H",
    "Activa los siete epílogos adicionales cuando los archivos necesarios estén disponibles.",
    "Estado: %s",
    "Automático",
    "4:3",
    "16:9",
    "Relación narrativa",
    "La relación narrativa solo cambia el encuadre de la historia; la resolución del monitor permanece en settings.json.",
    "Predeterminados de General/Audio originales",

    // LxH dynamic status
    "L×H activo (7 epílogos listos)",
    "HQ base (L×H desactivado)",
    "HQ base (no se encontró Packs/LxH.GPK)",
    "HQ base (traducción L×H no disponible para este idioma)",
    "Carpeta del juego no seleccionada",

    // Language tab
    "Idioma del juego (School Days HQ)",
    "Selecciona un idioma",
    "Idioma del juego",
    "Formato: %s",
    "El GPK en español requiere un archivo certificado Script - Spanish.GPK.",
    "El GPK en chino requiere Script - chinese-compatible.GPK.",
    "El GPK en ruso requiere su archivo certificado Script - Russian.GPK.",
    "HQ: Packs/Script.GPK original (nunca duplicado).",
    "GPK base HQ: %s",
    "GPK de los siete finales L×H: %s",
    "Falta el script de idioma base HQ en translations/scripts.",
    "No se encontró el script opcional L×H %s en translations/scripts. School Days HQ base se ejecutará en %s sin los finales L×H.",
    "Edición solo retail: 29 GPKs originales; sin epílogos ni traducciones L×H.",
    "Los 29 GPKs originales y la multimedia opcional L×H permanecen intactos.",
    "Estado L×H: %s",
    "Los textos del idioma se leen de scripts, no convertidos a JSON.",
    "El inglés utiliza los diálogos originales de School Days HQ.",
    "JSON base HQ: %s",
    "Al jugar se verifican diálogos, decisiones y frases revisadas. Las entradas faltantes no retroceden al inglés.",
    "Falta el JSON del idioma base. Selecciona el JSON correspondiente antes de jugar.",
    "La lista JSON muestra únicamente los perfiles de idioma instalados.",
    "Traducción de epílogos L×H (7 finales)",
    "JSON L×H (%s): %s",
    "emparejado automáticamente",
    "selección manual",
    "No encontrado",
    "Elegir JSON L×H...",
    "Usar L×H correspondiente automáticamente",
    "Los vídeos L×H permanecen en Packs/LxH.GPK. Solo cambia el JSON de subtítulos; el JSON base y el L×H se validan juntos antes de jugar.",

    // Game Files & Advanced
    "Datos importados de School Days HQ",
    "Los 29 GPKs originales y LxH.GPK se leen mediante el selector SAF de Android. Permanecen fuera del APK y no se modifican.",
    "Carpeta Packs activa: %s",
    "Perfil de ruta: %s",
    "Para cambiar el idioma, cierra el juego y usa la pantalla de configuración de Android.",
    "Un URI content:// no es una ruta de disco local.",
    "Archivos originales de School Days HQ",
    "Selecciona la carpeta de tu instalación legítima de School Days HQ (o su carpeta Packs). Los archivos originales del juego no están incluidos.",
    "IMPORTANTE: NO selecciones esta carpeta de distribución si no contiene los paquetes retail. Haz clic en Carpeta... y elige tu instalación ORIGINAL de School Days HQ con los 29 GPKs originales (LxH.GPK opcional es compatible si está presente). No se requieren partidas guardadas.",
    "Carpeta del juego o carpeta Packs",
    "Usa tus propios datos originales de School Days HQ (29 GPKs), un directorio extraído con Script/English o una imagen ISO compatible.",
    "Carpeta...",
    "ISO...",
    "Examinar...",
    "Archivo de ruta (.ktnroute)",
    "Selecciona una ruta KTRF compatible de School Days HQ.",
    "Archivos de datos y rutas",
    "Overlay de recursos",
    "Carpeta de overlay",
    "Elegir carpeta de overlay...",
    "Overlay automático",
    "Ajustes avanzados del motor",

    // Validation errors
    "GLSL personalizado debe ser la ruta completa a un archivo .glsl/.hook/.frag, no una carpeta (ej. FSRCNNX_x2_16-0-4-1.glsl).",
    "Archivo GLSL personalizado no encontrado: ",
    "Ruta GLSL personalizada no válida; selecciona un archivo de shader existente.",
    "MPV/Anime4K/ArtCNN requiere el renderizador OpenGL. Selecciona OpenGL o cambia Upscaler a Automático/Linear.",
    "Los idiomas GPK nativos requieren una ruta de configuración persistente.",
    "El modo GPK nativo no puede combinarse con un diccionario JSON base.",
    "El Script.GPK traducido seleccionado no es compatible con los nombres ORS originales de School Days HQ.",
    "El idioma seleccionado no tiene un JSON base válido. Elige un archivo de traducción en Idioma antes de jugar.",
    "Falta el JSON L×H seleccionado manualmente. Elige un archivo válido o restaura el emparejamiento automático.",
    "El JSON de idioma seleccionado no contiene entradas traducidas.",
    "Discord Rich Presence (actividad en el perfil)",
}};

constexpr Table kRussian{{
    "School Days HQ - Лаунчер",
    "ИГРАТЬ",
    "НАСТРОЙКИ",
    "ВЫХОД",
    "Музыка лаунчера: ВКЛ",
    "Музыка лаунчера: ВЫКЛ",
    "Язык лаунчера",
    "Меняет только язык интерфейса лаунчера (независимо от перевода игры).",
    "Отсутствует assets/SchoolDaysHQ-Launcher.png",
    "НАСТРОЙКА / ОПЦИИ",
    "Экран",
    "Графика",
    "Аудио",
    "Общие",
    "Язык",
    "Файлы игры",
    "Дополнительно",
    "Сброс",
    "ОК",
    "Отмена",
    "Настройки по умолчанию подготовлены. Нажмите ОК для сохранения.",

    // Display
    "Настройки экрана (те же, что в игре)",
    "Android использует полноэкранную Surface устройства. Оконный режим, эксклюзивный полный экран и выбор монитора на телефонах не применяются.",
    "По умолчанию устройства",
    "Разрешение / частота обновления",
    "Устройство не сообщило режимы отображения.",
    "Масштаб рендера настраивается отдельно во вкладке Графика.",
    "Выбранные монитор, режим, частота, апскейлер и рендерер сохраняются в settings.json и используются в настройках экрана игры.",
    "Режим окна",
    "Оконный",
    "Полный экран без рамок",
    "Эксклюзивный полный экран",
    "Монитор",
    "Экран",
    "Недоступно",
    "Монитор",
    "Режим без рамок использует текущий режим рабочего стола; выбор другого разрешения ниже запросит Эксклюзивный полный экран.",
    " (рабочий стол)",
    " (окно)",
    "Разрешение / частота",
    "Для этого монитора не найдено режимов отображения.",
    "Пользовательский размер окна",
    "VSync",
    "Изменяемый размер окна",

    // Graphics
    "Рендерер и масштабирование",
    "Масштаб рендера",
    "Рендерер: OpenGL ES (нативный)",
    "Рендерер",
    "Автоматически",
    "Апскейлер",
    "Авто",
    "Нет",
    "Linear",
    "MPV",
    "Коэффициент апскейла",
    "При 1.0x апскейлинг ВЫКЛЮЧЕН независимо от пресета.",
    "Пользовательский GLSL (внешний файл)",
    "Пресет шейдера",
    "Выбор пресета шейдера включает OpenGL + MPV и коэффициент >=2.0x.",
    "%s использует Linear при Авто: пресеты GLSL Anime4K/ArtCNN НЕ активны. Выберите пресет шейдера для OpenGL + MPV или оставьте Linear.",
    "Автоматический рендерер: активный апскейлер зависит от драйвера SDL. Для GLSL требуется OpenGL + MPV.",
    "Пути к пользовательским GLSL",
    "Укажите ФАЙЛ .glsl / .hook / .frag, а не папку.",
    "FSRCNNX x2 требует >1.4x; x3 >2.3x; x4 >3.3x.",
    "Имя папки не является шейдером. Выберите ФАЙЛ .glsl.",
    "Пользовательские пути GLSL сохранены, но неактивны с текущим рендерером или коэффициентом 1.0x.",
    "Дополнительные настройки апскейлинга движка",

    // Audio
    "Музыка только в лаунчере",
    "Воспроизводить музыку, пока открыт лаунчер",
    "Не влияет на музыку главного меню игры.",
    "Аудио (оригинальный Config.DAT)",
    "Профиль Config.DAT не предоставлен; оригинальные параметры недоступны, чтобы не изменять чужие сохранения.",
    "Громкость BGM",
    "Громкость SE",
    "Громкость голосов",
    "Мужские голоса",
    "Отключить звук игры",
    "Общая громкость",
    "Громкость видео",
    "Оригинальный MasterVolume намеренно не изменяется.",

    // General
    "Общие настройки",
    "Профиль Config.DAT недоступен.",
    "Показывать текст / субтитры",
    "Пропускать непрочитанные сцены",
    "SuperSkip / к следующему выбору",
    "Авто-отрисовка (Auto draw)",
    "Авто-режим (Auto mode)",
    "Поменять кнопки A/B геймпада",
    "Принудительные субтитры (.ass)",
    "Необязательное дополнение",
    "Включить концовки L×H",
    "Включает семь дополнительных эпилогов при наличии необходимых файлов.",
    "Статус: %s",
    "Автоматически",
    "4:3",
    "16:9",
    "Соотношение сторон сюжета",
    "Меняет только пропорции сюжетного кадра; разрешение монитора хранится в settings.json.",
    "Сбросить общие/аудио Config.DAT",

    // LxH dynamic status
    "L×H активен (7 эпилогов готовы)",
    "Базовая HQ (L×H выключен)",
    "Базовая HQ (Packs/LxH.GPK не найден)",
    "Базовая HQ (перевод L×H недоступен для выбранного языка)",
    "Папка игры не выбрана",

    // Language tab
    "Язык игры (School Days HQ)",
    "Выберите язык",
    "Язык игры",
    "Формат: %s",
    "Для испанского GPK требуется сертифицированный Script - Spanish.GPK.",
    "Для китайского GPK требуется Script - chinese-compatible.GPK.",
    "Для русского GPK требуется сертифицированный Script - Russian.GPK.",
    "HQ: оригинальный Packs/Script.GPK (без дублирования).",
    "Базовый HQ GPK: %s",
    "GPK семи концовок L×H: %s",
    "Отсутствует базовый языковой скрипт HQ в translations/scripts.",
    "Необязательный скрипт L×H %s не найден в translations/scripts. Базовая School Days HQ запустится на языке %s без концовок L×H.",
    "Только базовая версия: 29 оригинальных GPK без эпилогов L×H.",
    "29 оригинальных GPK и медиа L×H остаются нетронутыми.",
    "Статус L×H: %s",
    "Тексты читаются напрямую из скриптов, без конвертации в JSON.",
    "Английский использует оригинальные диалоги School Days HQ.",
    "Базовый HQ JSON: %s",
    "Перед запуском проверяются диалоги, выборы и проверенные строки. Пропущенные записи не откатываются к английскому.",
    "Отсутствует базовый JSON выбранного языка. Укажите файл перед запуском.",
    "В списке JSON отображаются только установленные языковые профили.",
    "Перевод эпилогов L×H (7 концовок)",
    "L×H JSON (%s): %s",
    "автоматически к базе",
    "выбран вручную",
    "Не найден",
    "Выбрать L×H JSON...",
    "Использовать подходящий L×H автоматически",
    "Видео L×H остаются в Packs/LxH.GPK. Меняется только JSON субтитров; базовый и L×H JSON проверяются вместе перед запуском.",

    // Game Files & Advanced
    "Импортированные данные School Days HQ",
    "29 оригинальных GPK и LxH.GPK подключаются через SAF Android. Они остаются вне APK и не изменяются.",
    "Активная папка Packs: %s",
    "Файл маршрута: %s",
    "Для смены языка закройте игру и используйте экран настройки Android.",
    "URI content:// не является локальным путем на диске.",
    "Оригинальные файлы School Days HQ",
    "Выберите папку вашей оригинальной установки School Days HQ (или её папку Packs). Оригинальные ресурсы игры не включены.",
    "ВАЖНО: НЕ выбирайте папку этой сборки, если в ней нет оригинальных GPK. Нажмите Папка... и укажите ОРИГИНАЛЬНУЮ установку School Days HQ с 29 файлами GPK (необязательный LxH.GPK поддерживается при наличии). Сохранения не требуются.",
    "Папка игры или папка Packs",
    "Укажите оригинальные файлы School Days HQ (29 GPK), распакованную папку с Script/English или совместимый ISO-образ.",
    "Папка...",
    "ISO...",
    "Обзор...",
    "Файл маршрута (.ktnroute)",
    "Выберите совместимый файл маршрута KTRF для School Days HQ.",
    "Файлы данных и маршрутизация",
    "Оверлей ресурсов",
    "Папка оверлея",
    "Выбрать папку оверлея...",
    "Автоматический оверлей",
    "Расширенные настройки движка",

    // Validation errors
    "Пользовательский GLSL должен указывать на файл .glsl/.hook/.frag, а не на папку (например, FSRCNNX_x2_16-0-4-1.glsl).",
    "Файл GLSL не найден: ",
    "Некорректный путь GLSL; выберите существующий файл шейдера.",
    "MPV/Anime4K/ArtCNN требует рендерер OpenGL. Выберите OpenGL или переключите апскейлер на Авто/Linear.",
    "Для нативных GPK-языков требуется постоянный путь к настройкам.",
    "Режим нативного GPK нельзя совмещать с базовым JSON-словарем.",
    "Выбранный переведенный Script.GPK несовместим с оригинальными именами ORS School Days HQ.",
    "Для выбранного языка отсутствует корректный базовый JSON. Выберите перевод во вкладке Язык перед запуском.",
    "Выбранный вручную L×H JSON отсутствует. Укажите корректный файл или верните автоподбор.",
    "Выбранный JSON-файл не содержит переведенных строк.",
    "Discord Rich Presence (активность в профиле)",
}};

constexpr Table kChineseSimplified{{
    "School Days HQ - 启动器",
    "开始游戏",
    "选项",
    "退出",
    "启动器音乐：开",
    "启动器音乐：关",
    "启动器语言",
    "仅更改启动器界面语言（与游戏翻译语言相互独立）。",
    "缺少 assets/SchoolDaysHQ-Launcher.png",
    "设置 / 选项",
    "显示",
    "图形",
    "音频",
    "常规",
    "语言",
    "游戏文件",
    "高级",
    "重置",
    "确定",
    "取消",
    "默认选项已准备就绪。点击“确定”以保存。",

    // Display
    "显示选项（与游戏内设置同步）",
    "Android 使用设备控制的全屏 Surface。窗口模式、独占全屏和显示器切换不适用于手机。",
    "设备默认",
    "原生分辨率 / 刷新率",
    "设备未提供可用的显示模式。",
    "渲染比例请在“图形”选项卡中单独调整。",
    "所选显示器、模式、刷新率、超分器和渲染器将保存至 settings.json，并与游戏内显示选项同步。",
    "窗口模式",
    "窗口",
    "无边框全屏",
    "独占全屏",
    "显示器",
    "显示器",
    "不可用",
    "显示器",
    "无边框模式使用当前桌面分辨率；在下方选择其他分辨率将请求独占全屏。",
    "（桌面）",
    "（窗口）",
    "分辨率 / 刷新率",
    "此显示器未返回任何显示模式。",
    "自定义窗口大小",
    "VSync",
    "可调整窗口大小",

    // Graphics
    "渲染器与超分辨率",
    "渲染比例",
    "渲染器：OpenGL ES（设备原生）",
    "渲染器",
    "自动",
    "超分后端 (Upscaler)",
    "自动",
    "无",
    "Linear",
    "MPV",
    "超分倍率",
    "倍率为 1.0x 时，无论选择何种预设都会关闭超分。",
    "自定义 GLSL（外部文件）",
    "着色器预设",
    "选择着色器预设将自动启用 OpenGL + MPV 且倍率 >=2.0x。",
    "%s 在自动模式下使用 Linear：Anime4K/ArtCNN GLSL 预设未启用。请选择着色器预设以切换到 OpenGL + MPV，或保留当前渲染器使用 Linear。",
    "自动渲染器：实际超分方式取决于 SDL 选择的驱动。GLSL 需要 OpenGL + MPV。",
    "自定义 GLSL 路径",
    "请输入 .glsl / .hook / .frag 文件路径，而非文件夹。",
    "FSRCNNX x2 需要 >1.4x；x3 需要 >2.3x；x4 需要 >3.3x。",
    "文件夹不是着色器。请选择具体的 .glsl 文件。",
    "自定义 GLSL 路径已保存，但在当前渲染器/后端或 1.0x 倍率下不会生效。",
    "额外引擎超分设置",

    // Audio
    "仅限启动器音乐",
    "打开启动器时播放背景音乐",
    "不会更改游戏标题界面的音乐。",
    "音频（原版 Config.DAT）",
    "未提供 Config.DAT 配置目录；为避免修改其他玩家的存档，原版选项不可用。",
    "BGM 音量",
    "SE 音量",
    "语音音量",
    "男性语音",
    "静音游戏音频",
    "主音量",
    "影片音量",
    "原版 MasterVolume 保持原样不予改动。",

    // General
    "常规选项",
    "原版 Config.DAT 配置不可用。",
    "显示文本 / 字幕",
    "跳过未读场景",
    "SuperSkip / 跳至下一选项",
    "自动绘制 (Auto draw)",
    "自动模式 (Auto mode)",
    "交换手柄 A/B 键",
    "强制字幕 (.ass)",
    "可选扩展",
    "启用 L×H 结局",
    "当所需文件齐全时，启用七个额外的 L×H 后日谈结局。",
    "状态：%s",
    "自动",
    "4:3",
    "16:9",
    "剧情画面比例",
    "仅改变剧情排版比例；显示器分辨率仍保存在 settings.json 中。",
    "恢复原版常规/音频默认值",

    // LxH dynamic status
    "L×H 已启用（7 个结局就绪）",
    "原版 HQ（L×H 已关闭）",
    "原版 HQ（未找到 Packs/LxH.GPK）",
    "原版 HQ（所选语言缺少 L×H 翻译）",
    "未选择游戏目录",

    // Language tab
    "游戏语言（School Days HQ）",
    "请选择语言",
    "游戏语言",
    "格式：%s",
    "西班牙语 GPK 需要经认证的 Script - Spanish.GPK。",
    "简体中文 GPK 需要经认证的 Script - chinese-compatible.GPK。",
    "俄语 GPK 需要经认证的 Script - Russian.GPK。",
    "HQ：原版 Packs/Script.GPK（不会复制原文件）。",
    "基础 HQ GPK：%s",
    "七个 L×H 结局 GPK：%s",
    "translations/scripts 中缺少基础 HQ 语言脚本。",
    "translations/scripts 中未找到可选 L×H 脚本 %s。将以 %s 运行基础版 School Days HQ（不含 L×H 结局）。",
    "仅限原版：29 个原版 GPK；不含 L×H 结局或翻译。",
    "原有的 29 个零售 GPK 与可选 L×H 媒体文件均保持原样。",
    "L×H 状态：%s",
    "语言文本直接从脚本读取，无需转换为 JSON。",
    "英语使用 School Days HQ 原始对话。",
    "基础 HQ JSON：%s",
    "开始游戏时会校验对话、选项和已审核条目。缺失条目不会静默回退到英语。",
    "缺少基础语言 JSON。请在开始游戏前选择对应的 HQ JSON。",
    "JSON 列表仅显示已安装的语言配置。",
    "L×H 结局翻译（7 个结局）",
    "L×H JSON（%s）：%s",
    "自动匹配基础语言",
    "手动指定",
    "未找到",
    "选择 L×H JSON...",
    "自动匹配对应的 L×H",
    "L×H 视频保留在 Packs/LxH.GPK 中。仅字幕 JSON 发生变化；开始游戏前会同时校验基础和 L×H JSON。",

    // Game Files & Advanced
    "已导入的 School Days HQ 数据",
    "29 个原版 GPK 与 LxH.GPK 通过 Android SAF 选择器访问，保留在 APK 外部且不会被修改。",
    "当前 Packs 文件夹：%s",
    "路线文件：%s",
    "如需切换语言，请关闭游戏并使用 Android 设置界面。",
    "content:// URI 不是本地磁盘路径。",
    "School Days HQ 原版游戏文件",
    "请选择您正版 School Days HQ 的安装目录（或其 Packs 文件夹）。本发布包不包含原版游戏资源。",
    "重要提示：如果当前发布文件夹中尚未放入原版 GPK，请勿直接选择它。请点击“文件夹...”并选择包含 29 个原版 GPK 的 School Days HQ 安装目录（如存在可选 LxH.GPK 也会自动支持）。无需存档文件。",
    "游戏文件夹或 Packs 文件夹",
    "请使用您自己的 School Days HQ 原版数据（29 个 GPK）、包含 Script/English 的解包目录或兼容 ISO 镜像。",
    "文件夹...",
    "ISO...",
    "浏览...",
    "路线文件 (.ktnroute)",
    "请选择兼容的 School Days HQ KTRF 路线文件。",
    "数据文件与路线",
    "资源覆盖 (Overlay)",
    "Overlay 文件夹",
    "选择 Overlay 文件夹...",
    "自动 Overlay",
    "高级引擎设置",

    // Validation errors
    "自定义 GLSL 必须是 .glsl/.hook/.frag 文件的完整路径，不能是文件夹（例如 FSRCNNX_x2_16-0-4-1.glsl）。",
    "未找到自定义 GLSL 文件：",
    "自定义 GLSL 路径无效；请选择已存在的着色器文件。",
    "MPV/Anime4K/ArtCNN 需要 OpenGL 渲染器。请选择 OpenGL 或将超分后端改为自动/Linear。",
    "原生 GPK 语言需要持久化的配置路径。",
    "原生 GPK 模式不能与基础 JSON 词典同时使用。",
    "所选翻译 Script.GPK 与 School Days HQ 原版 ORS 名称不兼容，需要兼容转换包。",
    "所选非英语语言缺少有效的基础 JSON。请在“语言”中选择翻译文件后再开始游戏。",
    "手动指定的 L×H JSON 不存在。请选择有效文件或恢复自动匹配。",
    "所选语言 JSON 不包含任何已翻译条目。",
    "Discord Rich Presence（个人资料动态）",
}};

constexpr std::array<Table, kLanguageCount> kTables{{
    kEnglish,
    kPortugueseBr,
    kSpanish,
    kRussian,
    kChineseSimplified,
}};

} // namespace

const char *Code(Language language) {
  switch (language) {
  case Language::En: return "en";
  case Language::PtBr: return "pt-BR";
  case Language::Es: return "es";
  case Language::Ru: return "ru";
  case Language::ZhCn: return "zh-CN";
  default: return "en";
  }
}

const char *NativeName(Language language) {
  switch (language) {
  case Language::En: return "English";
  case Language::PtBr: return "Português (Brasil)";
  case Language::Es: return "Español";
  case Language::Ru: return "Русский";
  case Language::ZhCn: return "简体中文";
  default: return "English";
  }
}

std::optional<Language> ParseCode(std::string_view raw) {
  const std::string token = normalizeToken(raw);
  if (token.empty())
    return std::nullopt;
  if (token == "en" || token.rfind("en-", 0) == 0)
    return Language::En;
  if (token == "pt-br" || token == "pt" || token.rfind("pt-", 0) == 0)
    return Language::PtBr;
  if (token == "es" || token.rfind("es-", 0) == 0)
    return Language::Es;
  if (token == "ru" || token.rfind("ru-", 0) == 0)
    return Language::Ru;
  if (token == "zh-cn" || token == "zh-hans" || token == "zh" ||
      token.rfind("zh-hans-", 0) == 0 || token.rfind("zh-cn-", 0) == 0)
    return Language::ZhCn;
  return std::nullopt;
}

Language ResolveDefault(bool existingInstallation,
                        std::string_view systemLocaleTag) {
  if (existingInstallation)
    return Language::En;
  if (const auto detected = ParseCode(systemLocaleTag))
    return *detected;
  return Language::En;
}

std::filesystem::path PreferencePath(const std::string &settingsPath) {
  if (settingsPath.empty())
    return {};
  return std::filesystem::path(settingsPath).parent_path() / kPreferenceFilename;
}

std::optional<Language> ReadPreference(const std::string &settingsPath) {
  const auto file = PreferencePath(settingsPath);
  std::error_code ec;
  if (file.empty() || !std::filesystem::is_regular_file(file, ec) || ec ||
      std::filesystem::file_size(file, ec) > 64 || ec)
    return std::nullopt;
  Kotonoha::Filesystem::InputFile input(file);
  if (!input)
    return std::nullopt;
  std::string line;
  std::getline(input, line);
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return ParseCode(line);
}

bool WritePreference(const std::string &settingsPath, Language language) {
  const auto file = PreferencePath(settingsPath);
  if (file.empty())
    return false;
  try {
    Kotonoha::writeTextFileAtomically(
        file.string(), std::string(Code(language)) + "\n");
    return true;
  } catch (...) {
    return false;
  }
}

const char *Text(Language language, Key key) {
  const auto langIdx = static_cast<std::size_t>(language);
  const auto keyIdx = static_cast<std::size_t>(key);
  if (langIdx >= kLanguageCount || keyIdx >= kKeyCount)
    return "";
  const char *value = kTables[langIdx][keyIdx];
  return (value && *value) ? value : kEnglish[keyIdx];
}

bool ValidateCoverage(std::string *firstMissing) {
  for (std::size_t l = 0; l < kLanguageCount; ++l) {
    for (std::size_t k = 0; k < kKeyCount; ++k) {
      const char *value = kTables[l][k];
      if (!value || !*value) {
        if (firstMissing) {
          *firstMissing = std::string(Code(static_cast<Language>(l))) +
                          ":key#" + std::to_string(k);
        }
        return false;
      }
    }
  }
  return true;
}

} // namespace Kotonoha::SchoolDaysLauncherLocale
