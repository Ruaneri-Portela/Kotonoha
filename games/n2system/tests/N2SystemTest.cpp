// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2Runtime.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>

static void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}

int main() {
  try {
    auto assets = std::make_shared<N2System::N2ArchiveAssets>();
    N2System::N2Runtime runtime(assets);

    check(runtime.assets() != nullptr, "N2Runtime assets initialized");

    const std::string script = "chapter main {\n"
                               "SetDialogue(\"Kurisu\", \"Mad scientist!\");\n"
                               "WaitKey();\n"
                               "Finish();\n"
                               "}\n";

    runtime.vm().loadScript(script, "main");
    runtime.update();

    check(runtime.dialogueSpeaker() == "Kurisu", "N2Runtime speaker match");
    check(runtime.dialogueText() == "Mad scientist!",
          "N2Runtime dialogue match");

    bool advanceResult = runtime.handleAdvanceAction();
    check(advanceResult, "N2Runtime handleAdvanceAction");

    runtime.update();
    check(runtime.vm().finished(), "N2Runtime script finished");

    std::cout << "All N2Runtime integration tests passed!\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "N2SystemTest error: " << e.what() << "\n";
    return 1;
  }
}
