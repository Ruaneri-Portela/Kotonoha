// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "MainWindow.g.h"
#include "LauncherModel.h"

#include <winrt/Windows.Data.Json.h>
#include <vector>

namespace winrt::Kotonoha::WinUI::implementation {

struct GameEntry {
  std::wstring title;
  std::string path;
  std::string profileId;
  std::string engineSlug;
  std::wstring targetBinary;
};

struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();

  void OnAdd(winrt::Windows::Foundation::IInspectable const &sender,
             winrt::Microsoft::UI::Xaml::RoutedEventArgs const &args);
  void OnScan(winrt::Windows::Foundation::IInspectable const &sender,
              winrt::Microsoft::UI::Xaml::RoutedEventArgs const &args);
  void OnGameSelected(
      winrt::Windows::Foundation::IInspectable const &sender,
      winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const &args);
  void OnSave(winrt::Windows::Foundation::IInspectable const &sender,
              winrt::Microsoft::UI::Xaml::RoutedEventArgs const &args);
  void OnLaunch(winrt::Windows::Foundation::IInspectable const &sender,
                winrt::Microsoft::UI::Xaml::RoutedEventArgs const &args);

private:
  void RefreshLibraryList();
  void ShowError(std::wstring_view message);
  void PersistSettings();
  std::size_t m_selected = static_cast<std::size_t>(-1);
  Kotonoha::WinUI::LauncherState m_state;
  std::vector<GameEntry> m_games;
};

} // namespace winrt::Kotonoha::WinUI::implementation
