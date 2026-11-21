// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/DebugCatalog.hpp>
#include <N2System/ScriptIndex.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool result, const char *message) {
  if (!result)
    throw std::runtime_error(message);
}
bool has(const std::vector<std::string> &values, const std::string &value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}
} // namespace

int main() {
  try {
    const std::string source = R"N2(
chapter main {
  // SetDialogue("Fake", "Do not extract");
  SetDialogue("Alice", "Hello", "voice/100.ogg");
  SetDialogue($character, "Variables preserve argument positions", "voice/101.ogg");
  CreateChoice("menu", "Yes", "No");
  SetDialogue("Alice", "Hello");
  /* Choice("fake", "fake answer"); */
}
chapter later {
  Talk("Bob", "Another chapter");
  Message('Speaker', 'Single quotes');
}
)N2";
    const auto index = N2System::ScriptIndex::Parse(source);
    const auto keys = N2System::ExtractTranslationTexts(index, source);
    require(keys.size() == 2 && keys.count(0) && keys.count(1),
            "source routes follow chapter order");
    const auto &first = keys.at(0);
    require(has(first, "Alice") && has(first, "Hello") &&
                has(first, "Variables preserve argument positions") &&
                has(first, "Yes") && has(first, "No"),
            "correct speakers, lines and choice labels");
    require(std::count(first.begin(), first.end(), "Hello") == 1,
            "deduplicate source keys");
    require(!has(first, "menu") && !has(first, "voice/100.ogg") &&
                !has(first, "voice/101.ogg") && !has(first, "Fake") &&
                !has(first, "fake answer"),
            "do not translate control IDs, audio IDs or comments");
    require(has(keys.at(1), "Another chapter") &&
                has(keys.at(1), "Single quotes"),
            "nested chapter and single-quoted arguments");
    std::cout << "N2System debug translation catalog passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
