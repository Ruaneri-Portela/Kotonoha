// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ONScripterConverter.hpp>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace N2System {

namespace {
std::string normalizePath(std::string_view path) {
  std::string result(path);
  for (char &c : result) {
    if (c == '\\')
      c = '/';
  }
  return result;
}

std::string trim(std::string_view s) {
  std::size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return "";
  std::size_t last = s.find_last_of(" \t\r\n");
  return std::string(s.substr(first, last - first + 1));
}

std::string sanitizeStringLiteral(std::string_view text) {
  std::string res;
  res.reserve(text.size());
  for (char c : text) {
    if (c == '"')
      res += "\\\"";
    else if (c == '\n')
      res += "\\n";
    else if (c == '\r')
      continue;
    else
      res += c;
  }
  return res;
}

bool isIgnoredSystemCommand(std::string_view line) {
  static const std::vector<std::string_view> ignored = {
      "mode800", "globalon", "monocro", "trans", "print", "delay",
      "vsp", "amsp", "systemcall", "windowback", "version", "rubyon",
      "rubyoff", "locate", "puttext", "clsp", "textoff", "texton",
      "setwindow", "btndef", "spbtn", "btnwait", "br", "game", "caption",
      "versionstr", "reset", "wave", "waveoff", "bgmstop", "dwaveoff"
  };
  for (auto cmd : ignored) {
    if (line.compare(0, cmd.size(), cmd) == 0)
      return true;
  }
  return false;
}
} // namespace

std::string ONScripterConverter::convert(std::string_view source) {
  std::string srcStr(source);
  std::istringstream stream(srcStr);
  std::ostringstream out;

  std::string line;
  bool inChapter = false;

  out << "chapter main {\n";
  inChapter = true;

  int choiceId = 0;

  while (std::getline(stream, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == ';')
      continue;

    // Handle ONScripter conditionals: if %cond command
    if (trimmed.rfind("if ", 0) == 0) {
      // Look for target command after conditional expression
      for (const char *kw : {"bg ", "dwave ", "dwaveloop ", "mp3loop ", "bgm ", "play ", "goto *", "gosub *", "select ", "lsp ", "csp "}) {
        auto pos = trimmed.find(kw);
        if (pos != std::string::npos) {
          trimmed = trimmed.substr(pos);
          break;
        }
      }
    }

    if (isIgnoredSystemCommand(trimmed))
      continue;

    // Handle labels (*001, *start, *define, etc.)
    if (trimmed.front() == '*') {
      std::string labelName = trimmed.substr(1);
      auto spacePos = labelName.find_first_of(" \t;");
      if (spacePos != std::string::npos)
        labelName = labelName.substr(0, spacePos);

      for (char &c : labelName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
          c = '_';
      }
      if (labelName.empty())
        labelName = "label";
      if (std::isdigit(static_cast<unsigned char>(labelName.front())))
        labelName = "_" + labelName;

      if (inChapter) {
        out << "}\n";
      }
      out << "chapter " << labelName << " {\n";
      inChapter = true;
      continue;
    }

    // stralias / numalias
    if (trimmed.rfind("stralias ", 0) == 0 || trimmed.rfind("numalias ", 0) == 0) {
      auto comma = trimmed.find(',');
      if (comma != std::string::npos) {
        auto space = trimmed.find(' ');
        std::string varName = trim(trimmed.substr(space + 1, comma - space - 1));
        std::string varVal = trim(trimmed.substr(comma + 1));
        if (!varVal.empty() && varVal.front() == '"' && varVal.back() == '"')
          varVal = varVal.substr(1, varVal.size() - 2);
        varVal = normalizePath(varVal);

        for (char &c : varName) {
          if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
        }
        if (varName.empty()) varName = "var";
        if (std::isdigit(static_cast<unsigned char>(varName.front())))
          varName = "_" + varName;

        out << "  SetVariable(\"$" << varName << "\", \"" << sanitizeStringLiteral(varVal) << "\");\n";
      }
      continue;
    }

    // Background: bg "cg\path.jpg", 10
    if (trimmed.rfind("bg ", 0) == 0) {
      auto q1 = trimmed.find('"');
      auto q2 = trimmed.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string imagePath = normalizePath(trimmed.substr(q1 + 1, q2 - q1 - 1));
        out << "  CreateSprite(\"bg\", \"" << sanitizeStringLiteral(imagePath) << "\");\n";
        out << "  SetPosition(\"bg\", \"0\", \"0\");\n";
        out << "  SetOpacity(\"bg\", \"255\");\n";
      }
      continue;
    }

    // Voice / DWAVE: dwave 0, "voice\file.ogg"
    if (trimmed.rfind("dwave ", 0) == 0 || trimmed.rfind("dwaveloop ", 0) == 0) {
      auto q1 = trimmed.find('"');
      auto q2 = trimmed.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string voicePath = normalizePath(trimmed.substr(q1 + 1, q2 - q1 - 1));
        out << "  PlayVoice(\"" << sanitizeStringLiteral(voicePath) << "\");\n";
      }
      continue;
    }

    // BGM: mp3loop "sound\file.ogg" or bgm "sound\file.ogg"
    if (trimmed.rfind("mp3loop ", 0) == 0 || trimmed.rfind("bgm ", 0) == 0 || trimmed.rfind("play ", 0) == 0) {
      auto q1 = trimmed.find('"');
      auto q2 = trimmed.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string bgmPath = normalizePath(trimmed.substr(q1 + 1, q2 - q1 - 1));
        out << "  PlayBgm(\"" << sanitizeStringLiteral(bgmPath) << "\", \"1\");\n";
      }
      continue;
    }

    // Jumps: goto *label
    if (trimmed.rfind("goto *", 0) == 0) {
      std::string target = trim(trimmed.substr(6));
      for (char &c : target) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
          c = '_';
      }
      if (!target.empty()) {
        if (std::isdigit(static_cast<unsigned char>(target.front())))
          target = "_" + target;
        out << "  Chapter(\"" << target << "\");\n";
      }
      continue;
    }

    // Subroutine: gosub *label
    if (trimmed.rfind("gosub *", 0) == 0) {
      std::string target = trim(trimmed.substr(7));
      for (char &c : target) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
          c = '_';
      }
      if (!target.empty()) {
        if (std::isdigit(static_cast<unsigned char>(target.front())))
          target = "_" + target;
        out << "  Call(\"" << target << "\");\n";
      }
      continue;
    }

    // Return
    if (trimmed == "return") {
      out << "  Return();\n";
      continue;
    }

    // Choices: select "Option 1", *label1, "Option 2", *label2
    if (trimmed.rfind("select ", 0) == 0) {
      std::string choiceName = "choice_" + std::to_string(++choiceId);
      std::vector<std::string> options;
      bool inQuote = false;
      std::string current;
      for (std::size_t i = 7; i < trimmed.size(); ++i) {
        char c = trimmed[i];
        if (c == '"') {
          inQuote = !inQuote;
        } else if (c == ',' && !inQuote) {
          std::string opt = trim(current);
          if (!opt.empty() && opt.front() == '"' && opt.back() == '"')
            opt = opt.substr(1, opt.size() - 2);
          if (!opt.empty() && opt.front() != '*')
            options.push_back(opt);
          current.clear();
        } else {
          current += c;
        }
      }
      if (!current.empty()) {
        std::string opt = trim(current);
        if (!opt.empty() && opt.front() == '"' && opt.back() == '"')
          opt = opt.substr(1, opt.size() - 2);
        if (!opt.empty() && opt.front() != '*')
          options.push_back(opt);
      }

      out << "  CreateChoice(\"" << choiceName << "\"";
      for (const auto &opt : options) {
        out << ", \"" << sanitizeStringLiteral(opt) << "\"";
      }
      out << ");\n";
      out << "  WaitKey();\n";
      continue;
    }

    // Dialogue text (lines starting with '^', or plain dialogue text)
    if (trimmed.front() == '^' || (trimmed.find("=\"") == std::string::npos && trimmed.find("=%") == std::string::npos)) {
      std::string text = trimmed;
      if (text.front() == '^')
        text = text.substr(1);

      bool waitKey = false;
      while (!text.empty()) {
        char back = text.back();
        if (back == '@' || back == '\\') {
          waitKey = true;
          text.pop_back();
        } else if (text.size() >= 2 && text.substr(text.size() - 2) == "br") {
          text.erase(text.size() - 2);
        } else if (back == ' ' || back == '\t' || back == '\r' || back == '\n') {
          text.pop_back();
        } else {
          break;
        }
      }

      text = trim(text);
      if (!text.empty()) {
        std::string speaker;
        if (text.front() == '[' || text.front() == '(') {
          auto closePos = text.find_first_of("])");
          if (closePos != std::string::npos) {
            speaker = text.substr(1, closePos - 1);
            text = trim(text.substr(closePos + 1));
          }
        }

        out << "  SetDialogue(\"" << sanitizeStringLiteral(speaker) << "\", \""
            << sanitizeStringLiteral(text) << "\");\n";
        if (waitKey)
          out << "  WaitKey();\n";
      }
    }
  }

  if (inChapter) {
    out << "}\n";
  }

  return out.str();
}

} // namespace N2System
