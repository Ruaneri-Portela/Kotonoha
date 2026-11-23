// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::Kotonoha::WinUI::implementation {

void App::OnLaunched(LaunchActivatedEventArgs const &) {
  Window window = nullptr;
  if (window == nullptr)
    window = make<MainWindow>();
  window.Activate();
}

} // namespace winrt::Kotonoha::WinUI::implementation
