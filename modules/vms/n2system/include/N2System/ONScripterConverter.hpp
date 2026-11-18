// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

namespace N2System {

class ONScripterConverter {
public:
  // Converts ONScripter / PONScripter .utf script content (e.g. Saya no Uta 00.utf/01.utf)
  // into valid N2System NsScript syntax.
  static std::string convert(std::string_view onscripterSource);
};

} // namespace N2System
