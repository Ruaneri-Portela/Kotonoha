// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2Runtime.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <iostream>

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  try {
    Kotonoha::Engine engine("N2System Engine - NitroSharp Port", 1280, 720);
    auto assets = std::make_shared<N2System::N2ArchiveAssets>();
    auto runtime = std::make_unique<N2System::N2Runtime>(assets);
    engine.setRuntime(std::move(runtime));
    engine.run();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "N2System Fatal Error: " << e.what() << "\n";
    return 1;
  }
}
