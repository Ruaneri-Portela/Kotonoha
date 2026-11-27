// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// The macOS KotonohaImGui.framework is a thin container: Dear ImGui and its
// SDL3 backends are supplied by the vcpkg `imgui` port (feature bundle
// core+sdl3-binding+sdl3-renderer-binding). This translation unit keeps Xcode
// producing a valid framework with real debug info and the expected product.
extern "C" int KotonohaImGuiVcpkgProvider(void) { return 1; }
