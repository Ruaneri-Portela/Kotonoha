#include <Kotonoha/debug/DebugInternal.hpp>
#include <sstream>
#include <utility>

namespace Kotonoha::Debug {

void LoadTextEditor(const std::string &source) {
  textEditorSyncedScript = source;
  textEditorLines.clear();
  textEditorEntries.clear();
  textEditorFinalNewline = !source.empty() && source.back() == '\n';

  std::stringstream stream(source);
  std::string line;

  while (std::getline(stream, line)) {
    textEditorLines.push_back(line);
  }

  for (size_t i = 0; i < textEditorLines.size(); ++i) {
    const std::string &rawLine = textEditorLines[i];
    const size_t contentEnd = rawLine.find('\r');
    std::string content = rawLine.substr(0, contentEnd);

    const bool hasTerminator = !content.empty() && content.back() == ';';
    if (hasTerminator) {
      content.pop_back();
    } else {
      continue;
    }

    const size_t equals = content.find('=');
    if (equals == std::string::npos) {
      continue;
    }

    const bool isPrintText = content.compare(0, equals, "[PrintText]") == 0;
    const bool isSelection = content.compare(0, equals, "[SetSELECT]") == 0;

    if (!isPrintText && !isSelection) {
      continue;
    }

    const std::string payload = content.substr(equals + 1);
    const std::vector<std::string> fields = SplitTabs(payload);

    if ((isPrintText && fields.size() != 4) ||
        (isSelection && fields.size() < 3)) {
      continue;
    }

    TextEditorEntry entry;
    entry.isSelection = isSelection;
    entry.lineIndex = i;
    entry.startTime = FormatScriptTime(fields.front());
    entry.endTime = FormatScriptTime(fields.back());

    const size_t firstTab = content.find('\t', equals + 1);
    const size_t lastTab = content.rfind('\t');

    if (firstTab == std::string::npos || lastTab == std::string::npos ||
        lastTab <= firstTab) {
      continue;
    }

    entry.prefix = content.substr(0, firstTab);
    entry.suffix = content.substr(lastTab) + ";";

    if (isPrintText) {
      entry.character = fields[1];
      entry.text = fields[2];
    } else {
      for (size_t field = 1; field + 1 < fields.size(); ++field) {
        entry.options.push_back(fields[field]);
      }
    }

    textEditorEntries.push_back(std::move(entry));
  }
}

void SyncTextEditorToScript() {
  for (const TextEditorEntry &entry : textEditorEntries) {
    std::string replacement = entry.prefix;

    if (entry.isSelection) {
      for (const std::string &option : entry.options) {
        replacement.push_back('\t');
        replacement += option;
      }
    } else {
      replacement.push_back('\t');
      replacement += entry.character;
      replacement.push_back('\t');
      replacement += entry.text;
    }

    replacement += entry.suffix;

    const size_t carriageReturn = textEditorLines[entry.lineIndex].find('\r');
    if (carriageReturn != std::string::npos) {
      replacement += textEditorLines[entry.lineIndex].substr(carriageReturn);
    }

    textEditorLines[entry.lineIndex] = std::move(replacement);
  }

  sceneEditorText = JoinLines(textEditorLines, textEditorFinalNewline);
  textEditorSyncedScript = sceneEditorText;
  textEditorDirty = true;
}

} // namespace Kotonoha::Debug
