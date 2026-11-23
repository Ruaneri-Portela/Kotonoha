#include <Kotonoha/core/LauncherLocale.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace Kotonoha::LauncherLocale {
namespace {
using Translations = std::array<const char *, 4>;

const std::unordered_map<std::string_view, Translations> &catalog() {
  // Order: pt-BR, es, ru, zh-CN. English is the source string.
  static const std::unordered_map<std::string_view, Translations> entries{
      {"Interface language",
       {"Idioma da interface", "Idioma de la interfaz", "Язык интерфейса",
        "界面语言"}},
      {"Game", {"Jogo", "Juego", "Игра", "游戏"}},
      {"Video", {"Vídeo", "Vídeo", "Видео", "视频"}},
      {"Upscaling", {"Ampliação", "Reescalado", "Масштабирование", "画面放大"}},
      {"Translation", {"Tradução", "Traducción", "Перевод", "翻译"}},
      {"System info",
       {"Informações do sistema", "Información del sistema",
        "Сведения о системе", "系统信息"}},
      {"Advanced", {"Avançado", "Avanzado", "Дополнительно", "高级"}},
      {"Play", {"Jogar", "Jugar", "Играть", "开始游戏"}},
      {"Quit", {"Sair", "Salir", "Выход", "退出"}},
      {"Restore defaults",
       {"Restaurar padrões", "Restaurar valores", "Сбросить настройки",
        "恢复默认"}},
      {"Settings will not be saved",
       {"As configurações não serão salvas", "Los ajustes no se guardarán",
        "Настройки не будут сохранены", "设置不会保存"}},
      {"Asset overlay",
       {"Sobreposição de arquivos", "Archivos superpuestos", "Замена ресурсов",
        "资源覆盖"}},
      {"Overlay folder",
       {"Pasta de sobreposição", "Carpeta de reemplazos",
        "Папка замены ресурсов", "覆盖资源文件夹"}},
      {"Browse overlay...",
       {"Procurar sobreposição...", "Buscar reemplazos...",
        "Выбрать папку ресурсов...", "浏览覆盖资源..."}},
      {"Automatic overlay",
       {"Sobreposição automática", "Reemplazos automáticos",
        "Автоматическая замена", "自动覆盖"}},
      {"Display", {"Tela", "Pantalla", "Экран", "显示"}},
      {"Mode", {"Modo", "Modo", "Режим", "模式"}},
      {"Windowed", {"Janela", "Ventana", "Оконный", "窗口"}},
      {"Borderless fullscreen",
       {"Tela cheia sem bordas", "Pantalla completa sin bordes",
        "Безрамочный полный экран", "无边框全屏"}},
      {"Exclusive fullscreen",
       {"Tela cheia exclusiva", "Pantalla completa exclusiva", "Полный экран",
        "独占全屏"}},
      {"Monitor", {"Monitor", "Monitor", "Монитор", "显示器"}},
      {"Window", {"Janela", "Ventana", "Окно", "窗口"}},
      {"Custom size",
       {"Tamanho personalizado", "Tamaño personalizado", "Свой размер",
        "自定义尺寸"}},
      {"Desktop resolution",
       {"Resolução da área de trabalho", "Resolución del escritorio",
        "Разрешение рабочего стола", "桌面分辨率"}},
      {"Exclusive resolution",
       {"Resolução em tela cheia", "Resolución en pantalla completa",
        "Полноэкранное разрешение", "全屏分辨率"}},
      {"Resizable window",
       {"Janela redimensionável", "Ventana ajustable", "Изменяемый размер окна",
        "可调整窗口大小"}},
      {"Renderer", {"Renderizador", "Renderizador", "Рендерер", "渲染器"}},
      {"Automatic", {"Automático", "Automático", "Автоматически", "自动"}},
      {"Scale", {"Escala", "Escala", "Масштаб", "缩放"}},
      {"Off (1.00x)",
       {"Desligado (1,00x)", "Desactivado (1,00x)", "Выключено (1,00x)",
        "关闭 (1.00x)"}},
      {"Turn off", {"Desligar", "Desactivar", "Выключить", "关闭"}},
      {"Backend",
       {"Motor de ampliação", "Motor de escalado", "Бэкенд", "处理后端"}},
      {"Shader (OpenGL)",
       {"Shader (OpenGL)", "Shader (OpenGL)", "Шейдер (OpenGL)",
        "着色器 (OpenGL)"}},
      {"Master volume",
       {"Volume geral", "Volumen general", "Общая громкость", "主音量"}},
      {"Mute all audio",
       {"Desativar todo o áudio", "Silenciar todo el audio",
        "Отключить весь звук", "全部静音"}},
      {"VN text",
       {"Texto da VN", "Texto de la VN", "Текст новеллы", "视觉小说文本"}},
      {"VN font",
       {"Fonte da VN", "Fuente de la VN", "Шрифт новеллы", "游戏文字字体"}},
      {"Original game font",
       {"Fonte original do jogo", "Fuente original del juego",
        "Оригинальный шрифт игры", "游戏原字体"}},
      {"Override VN text colors",
       {"Substituir cores do texto", "Cambiar colores del texto",
        "Переопределить цвета текста", "覆盖文本颜色"}},
      {"Dialogue color",
       {"Cor do diálogo", "Color de diálogos", "Цвет диалогов", "对话颜色"}},
      {"Speaker color",
       {"Cor do falante", "Color del hablante", "Цвет говорящего",
        "发言者颜色"}},
      {"Debug UI scale",
       {"Escala da interface de depuração", "Escala de interfaz de depuración",
        "Масштаб интерфейса отладки", "调试界面缩放"}},
      {"UI font",
       {"Fonte da interface", "Fuente de la interfaz", "Шрифт интерфейса",
        "界面字体"}},
      {"Subtitles", {"Legendas", "Subtítulos", "Субтитры", "字幕"}},
      {"Enable forced subtitles",
       {"Ativar legendas forçadas", "Activar subtítulos forzados",
        "Включить обязательные субтитры", "启用强制字幕"}},
      {"Log level",
       {"Nível de log", "Nivel de registro", "Уровень журнала", "日志级别"}},
      {"Trace", {"Rastreamento", "Traza", "Трассировка", "追踪"}},
      {"Verbose", {"Detalhado", "Detallado", "Подробно", "详细"}},
      {"Debug", {"Depuração", "Depuración", "Отладка", "调试"}},
      {"Info", {"Informações", "Información", "Информация", "信息"}},
      {"Warning", {"Aviso", "Aviso", "Предупреждение", "警告"}},
      {"Error", {"Erro", "Error", "Ошибка", "错误"}},
      {"Critical", {"Crítico", "Crítico", "Критическая", "严重"}},
      {"None", {"Nenhum", "Ninguno", "Нет", "无"}},
      {"Linear (SDL)",
       {"Linear (SDL)", "Lineal (SDL)", "Линейный (SDL)", "线性 (SDL)"}},
      {"Translation dictionary (JSON)",
       {"Dicionário de tradução (JSON)", "Diccionario de traducción (JSON)",
        "Словарь переводов (JSON)", "翻译词典 (JSON)"}},
      {"Translation file found",
       {"Arquivo de tradução encontrado", "Archivo de traducción encontrado",
        "Файл перевода найден", "已找到翻译文件"}},
      {"Translation file not found",
       {"Arquivo de tradução não encontrado",
        "Archivo de traducción no encontrado", "Файл перевода не найден",
        "未找到翻译文件"}},
      {"No translation file selected",
       {"Nenhum arquivo de tradução selecionado",
        "No se seleccionó archivo de traducción", "Файл перевода не выбран",
        "未选择翻译文件"}},
      {"UI font file found",
       {"Arquivo de fonte encontrado", "Archivo de fuente encontrado",
        "Файл шрифта найден", "已找到字体文件"}},
      {"UI font file not found",
       {"Arquivo de fonte não encontrado", "Archivo de fuente no encontrado",
        "Файл шрифта не найден", "未找到字体文件"}},
      {"Clear", {"Limpar", "Limpiar", "Очистить", "清除"}},
      {"Browse...", {"Procurar...", "Buscar...", "Обзор...", "浏览..."}},
      {"Folder...", {"Pasta...", "Carpeta...", "Папка...", "文件夹..."}},
      {"ISO...", {"ISO...", "ISO...", "ISO...", "ISO..."}},
      {"Game folder or Packs folder",
       {"Pasta do jogo ou Packs", "Carpeta del juego o Packs",
        "Папка игры или Packs", "游戏文件夹或 Packs"}},
      {"Route file (.ktnroute)",
       {"Arquivo de rota (.ktnroute)", "Archivo de ruta (.ktnroute)",
        "Файл маршрута (.ktnroute)", "路线文件 (.ktnroute)"}},
      {"Game data",
       {"Dados do jogo", "Datos del juego", "Данные игры", "游戏数据"}},
      {"ISO disc image",
       {"Imagem de disco ISO", "Imagen de disco ISO", "Образ диска ISO",
        "ISO 光盘镜像"}},
      {"All files",
       {"Todos os arquivos", "Todos los archivos", "Все файлы", "所有文件"}},
      {"KTRF routes",
       {"Rotas KTRF", "Rutas KTRF", "Маршруты KTRF", "KTRF 路线"}},
      {"Translation JSON (*.json)",
       {"Tradução JSON (*.json)", "Traducción JSON (*.json)",
        "Перевод JSON (*.json)", "JSON 翻译 (*.json)"}},
      {"All files (*)",
       {"Todos os arquivos (*)", "Todos los archivos (*)", "Все файлы (*)",
        "所有文件 (*)"}},
      {"Automatic overlay: no overlay folder found",
       {"Sobreposição automática: pasta não encontrada",
        "Reemplazos automáticos: carpeta no encontrada",
        "Автоматическая замена: папка не найдена", "自动覆盖：未找到文件夹"}},
      {"Defaults restored (saved on Play)",
       {"Padrões restaurados (salvos ao jogar)",
        "Valores restaurados (se guardan al jugar)",
        "Настройки сброшены (сохранение при запуске)",
        "已恢复默认（开始游戏时保存）"}},
      {"Invalid settings",
       {"Configurações inválidas", "Ajustes no válidos", "Неверные настройки",
        "设置无效"}},
      {"School Days route is valid",
       {"A rota do School Days é válida", "La ruta de School Days es válida",
        "Маршрут School Days действителен", "School Days 路线有效"}},
      {"Desktop", {"Área de trabalho", "Escritorio", "Рабочий стол", "桌面"}},
  };
  return entries;
}

std::string normalize(std::string_view code) {
  std::string value;
  value.reserve(code.size());
  for (char ch : code) {
    if (ch == '_')
      ch = '-';
    value.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return value;
}

std::filesystem::path preferencePath(const std::string &settingsPath) {
  if (settingsPath.empty())
    return {};
  return std::filesystem::path(settingsPath).parent_path() /
         "launcher-language.txt";
}
} // namespace

Language parse(std::string_view code) {
  const auto value = normalize(code);
  if (value == "pt-br" || value == "pt")
    return Language::PortugueseBrazil;
  if (value == "es" || value.rfind("es-", 0) == 0)
    return Language::Spanish;
  if (value == "ru" || value.rfind("ru-", 0) == 0)
    return Language::Russian;
  if (value == "zh" || value == "zh-cn" || value == "zh-sg" ||
      value == "zh-hans" || value.rfind("zh-hans-", 0) == 0)
    return Language::Chinese;
  return Language::English;
}

Language systemLanguage(std::string_view language, std::string_view country) {
  if (language.empty())
    return Language::English;
  std::string combined(language);
  if (!country.empty())
    combined += "-" + std::string(country);
  return parse(combined);
}

const char *tag(Language language) {
  switch (language) {
  case Language::PortugueseBrazil:
    return "pt-BR";
  case Language::Spanish:
    return "es";
  case Language::Russian:
    return "ru";
  case Language::Chinese:
    return "zh-CN";
  default:
    return "en";
  }
}

const char *nativeName(Language language) {
  switch (language) {
  case Language::PortugueseBrazil:
    return "Português (Brasil)";
  case Language::Spanish:
    return "Español";
  case Language::Russian:
    return "Русский";
  case Language::Chinese:
    return "简体中文";
  default:
    return "English";
  }
}

Language load(const std::string &settingsPath, Language newInstallDefault) {
  const auto path = preferencePath(settingsPath);
  if (path.empty())
    return Language::English;
  std::error_code ec;
  if (std::filesystem::is_regular_file(path, ec)) {
    std::ifstream in(path, std::ios::binary);
    std::string value;
    if (std::getline(in, value)) {
      if (!value.empty() && value.back() == '\r')
        value.pop_back();
      // Reject invalid user-edited tags instead of guessing a different UI.
      for (const auto language :
           {Language::English, Language::PortugueseBrazil, Language::Spanish,
            Language::Russian, Language::Chinese}) {
        if (value == tag(language))
          return language;
      }
    }
    return Language::English;
  }
  // No new preference in existing installations: preserve old English UI.
  if (std::filesystem::exists(settingsPath, ec))
    return Language::English;
  return newInstallDefault;
}

void save(const std::string &settingsPath, Language language) {
  const auto path = preferencePath(settingsPath);
  if (!path.empty()) {
    // The first language selection may precede the first engine settings save.
    if (!path.parent_path().empty())
      std::filesystem::create_directories(path.parent_path());
    writeTextFileAtomically(path.string(), std::string(tag(language)) + "\n");
  }
}

const char *text(Language language, const char *english) {
  if (!english || language == Language::English)
    return english;
  const auto found = catalog().find(english);
  if (found == catalog().end())
    return english;
  return found->second.at(static_cast<std::size_t>(language) - 1);
}

} // namespace Kotonoha::LauncherLocale
