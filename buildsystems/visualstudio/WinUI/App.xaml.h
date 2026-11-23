// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "App.g.h"

namespace winrt::Kotonoha::WinUI::implementation {
struct App : AppT2<App> {
  App() { InitializeComponent(); }
  void OnLaunched(
      winrt::Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const
          &args);
};
} // namespace winrt::Kotonoha::WinUI::implementation
