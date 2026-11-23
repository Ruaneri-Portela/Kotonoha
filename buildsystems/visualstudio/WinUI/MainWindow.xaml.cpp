// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "pch.h"
#include "MainWindow.xaml.h"

#include <sstream>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Data::Json;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace Kotonoha::WinUI::implementation {
namespace {

std::wstring widen(std::string_view value) {
  if (value.empty())
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), size);
  return out;
}

std::string narrow(std::wstring_view value) {
  if (value.empty())
    return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring jsonWString(const JsonObject &object, std::wstring_view key) {
  if (object.HasKey(key))
    return object.GetNamedString(key).c_str();
  return {};
}

std::string jsonString(const JsonObject &object, std::wstring_view key) {
  return narrow(jsonWString(object, key));
}

void addCombo(ComboBox const &combo, std::wstring_view value) {
  combo.Items().Append(box_value(std::wstring(value)));
}

} // namespace

MainWindow::MainWindow() {
  InitializeComponent();
  m_state = Kotonoha::WinUI::LoadLauncherState();

  addCombo(RendererCombo(), L"");
  addCombo(RendererCombo(), L"direct3d11");
  addCombo(RendererCombo(), L"direct3d12");
  addCombo(RendererCombo(), L"opengl");
  addCombo(RendererCombo(), L"software");
  addCombo(UpscalerCombo(), L"");
  addCombo(UpscalerCombo(), L"Auto");
  addCombo(UpscalerCombo(), L"Linear");
  addCombo(UpscalerCombo(), L"MPV");
  addCombo(UpscalerCombo(), L"None");

  const std::string presets = Kotonoha::WinUI::UpscalePresets();
  try {
    auto items = JsonArray::Parse(widen(presets));
    for (const auto &item : items) {
      auto object = item.GetObject();
      const auto name = jsonWString(object, L"name");
      addCombo(PresetCombo(), name);
    }
  } catch (...) {
  }

  FontBox().Text(box_value(widen(m_state.theme.fontFamily)));
  AccentBox().Text(box_value(widen(m_state.theme.accentColor)));
  VolumeSlider().Value(m_state.global.masterVolume);
  FactorSlider().Value(m_state.global.upscalerFactor);
  RefreshLibraryList();
}

void MainWindow::RefreshLibraryList() {
  GamesList().Items().Clear();
  for (const auto &game : m_games)
    GamesList().Items().Append(box_value(game.title));
  for (const auto &root : m_state.libraryRoots) {
    if (!root.empty())
      StatusText().Text(box_value(std::wstring(L"Library: ") + widen(root)));
  }
}

void MainWindow::ShowError(std::wstring_view message) {
  ErrorBar().Message(box_value(std::wstring(message)));
  ErrorBar().IsOpen(true);
}

void MainWindow::OnAdd(IInspectable const &, RoutedEventArgs const &) {
  try {
    winrt::Windows::Storage::Pickers::FolderPicker picker;
    picker.FileTypeFilter().Append(L"*");
    auto folder = picker.PickSingleFolderAsync().get();
    if (folder) {
      m_state.libraryRoots.push_back(narrow(folder.Path().c_str()));
      Kotonoha::WinUI::SaveLauncherState(m_state);
      StatusText().Text(box_value(std::wstring(L"Added: ") + folder.Path()));
    }
  } catch (const hresult_error &error) {
    ShowError(error.message().c_str());
  }
}

void MainWindow::OnScan(IInspectable const &, RoutedEventArgs const &) {
  m_games.clear();
  GamesList().Items().Clear();
  try {
    const std::string json = Kotonoha::WinUI::ScanRoots(m_state);
    auto items = JsonArray::Parse(widen(json));
    for (const auto &item : items) {
      auto object = item.GetObject();
      GameEntry entry;
      entry.title = jsonWString(object, L"title");
      entry.path = jsonString(object, L"path");
      entry.profileId = jsonString(object, L"saveProfileId");
      entry.engineSlug = jsonString(object, L"engineSlug");
      entry.targetBinary = jsonWString(object, L"targetBinary");
      if (entry.title.empty())
        entry.title = widen(entry.path);
      m_games.push_back(std::move(entry));
      GamesList().Items().Append(box_value(m_games.back().title));
    }
    StatusText().Text(box_value(L"Found " + std::to_wstring(m_games.size()) +
                                L" game(s)."));
  } catch (const hresult_error &error) {
    ShowError(error.message().c_str());
  }
}

void MainWindow::OnGameSelected(
    IInspectable const &,
    winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const &) {
  const auto index = GamesList().SelectedIndex();
  if (index < 0 || static_cast<std::size_t>(index) >= m_games.size())
    return;
  m_selected = static_cast<std::size_t>(index);
  const auto &game = m_games[m_selected];
  GamePathText().Text(box_value(widen(game.path)));

  auto params = Kotonoha::WinUI::LoadGameParams(game.profileId);
  OverlayBox().Text(box_value(widen(params.assetOverlayPath)));
  TranslationBox().Text(box_value(widen(params.translationPath)));
  SubtitleStyleBox().Text(box_value(widen(params.subtitleStylePath)));
  RouteBox().Text(box_value(widen(params.ktrfPath.empty() ? params.scriptsDir
                                                          : params.ktrfPath)));
  // Engine-owned setting, read from the profile's settings.json by the bridge
  // consumer; the UI only stages the value for the next save.
  const auto forced = Kotonoha::WinUI::SaveProfileEngineSettings(
      game.profileId, L"{}");
  (void)forced;
}

void MainWindow::PersistSettings() {
  m_state.theme.fontFamily = narrow(FontBox().Text().as<hstring>().c_str());
  m_state.theme.accentColor = narrow(AccentBox().Text().as<hstring>().c_str());
  m_state.global.masterVolume =
      static_cast<float>(VolumeSlider().Value());
  m_state.global.upscalerFactor = static_cast<float>(FactorSlider().Value());
  Kotonoha::WinUI::SaveLauncherState(m_state);

  if (m_selected >= m_games.size())
    return;
  const auto &game = m_games[m_selected];
  Kotonoha::WinUI::GameParams params;
  params.assetOverlayPath = narrow(OverlayBox().Text().as<hstring>().c_str());
  params.translationPath =
      narrow(TranslationBox().Text().as<hstring>().c_str());
  params.subtitleStylePath =
      narrow(SubtitleStyleBox().Text().as<hstring>().c_str());
  const std::string route = narrow(RouteBox().Text().as<hstring>().c_str());
  if (game.engineSlug == "school-days")
    params.ktrfPath = route;
  else
    params.scriptsDir = route;
  params.rendererBackend =
      narrow(RendererCombo().SelectedValue().as<hstring>().c_str());
  params.upscalerBackend =
      narrow(UpscalerCombo().SelectedValue().as<hstring>().c_str());
  params.upscalerPreset =
      narrow(PresetCombo().SelectedValue().as<hstring>().c_str());
  params.upscalerFactor = std::to_string(FactorSlider().Value());
  Kotonoha::WinUI::SaveGameParams(game.profileId, params);

  std::ostringstream partial;
  partial << "{\"masterVolume\":" << VolumeSlider().Value()
          << ",\"fontPath\":\"" << m_state.theme.fontFamily << "\""
          << ",\"enableForcedSubtitles\":"
          << (ForcedSubtitlesBox().IsChecked().GetBoolean() ? "true" : "false")
          << ",\"renderer\":\"" << params.rendererBackend << "\""
          << ",\"upscaling\":{\"factor\":" << FactorSlider().Value()
          << ",\"backend\":\"" << params.upscalerBackend << "\",\"preset\":\""
          << params.upscalerPreset << "\"}}";
  const std::string result = Kotonoha::WinUI::SaveProfileEngineSettings(
      game.profileId, partial.str());
  if (result.find("\"ok\":true") == std::string::npos)
    ShowError(widen(result));
}

void MainWindow::OnSave(IInspectable const &, RoutedEventArgs const &) {
  PersistSettings();
}

void MainWindow::OnLaunch(IInspectable const &, RoutedEventArgs const &) {
  PersistSettings();
  if (m_selected >= m_games.size())
    return;
  const auto &game = m_games[m_selected];
  auto params = Kotonoha::WinUI::LoadGameParams(game.profileId);
  std::wstring error;
  if (!Kotonoha::WinUI::LaunchGame(narrow(game.targetBinary), params,
                                   m_state.global, game.profileId, error))
    ShowError(error);
}

} // namespace winrt::Kotonoha::WinUI::implementation
