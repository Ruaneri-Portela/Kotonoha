// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <N2System/ScriptIndex.hpp>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace N2System {
// Non-executing translation catalog for NsScript VM scripts, grouped by the
// same chapter ordinal used by N2Runtime::translationRoute(). Only literal
// source strings are included; runtime variables are reported separately by
// activeTranslationKeys().
std::map<int, std::vector<std::string>>
ExtractTranslationTexts(const ScriptIndex &index, std::string_view source);
} // namespace N2System
