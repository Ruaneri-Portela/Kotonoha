#include "SchoolDaysLauncher.hpp"
#include <SchoolDays/SchoolDaysAssets.hpp>

#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

namespace SchoolDays {

Launcher::Launcher(std::string settingsPath, std::string directory,
                   std::string route)
    : GameLauncher(
          "School Days HQ - Launcher", std::move(settingsPath),
          {"Game folder or Packs folder",
           PathKind::Folder,
           {},
           "dir.txt",
           "Use your own School Days HQ retail data (29 GPKs), or an extracted "
           "directory with Script/English, or an ISO containing these files. "
           "ISO data uses a temporary internal mount."}) {
  setAudioChannels({"BGM", "Voice", "SE", "Movie"});
  loadPath(std::move(directory));
  route = findGamePath(
      "school-days",
      {route, rememberedPath(getSettingsPath(), RouteRememberFile),
       Kotonoha::Providers::SchoolDaysAssets::findDefaultRoute().string()},
      validateRoute, {"school-days-hq.ktnroute"});
  routeOption =
      addPathOption({"Route file (.ktnroute)",
                     PathKind::File,
                     {{"KTRF routes", "ktnroute;ktrf"}, {"All files", "*"}},
                     RouteRememberFile,
                     "Select a compatible School Days HQ KTRF route."},
                    validateRoute, std::move(route));
  if (!optionPathValid(routeOption))
    setOptionPath(routeOption, {});
}

bool Launcher::validateRoute(const std::string &file, std::string &info) {
  Kotonoha::SchoolDaysKtrfSession session;
  Kotonoha_KtrfError error{};
  if (!session.Open(file.c_str(), ".", &error)) {
    info = std::string("Invalid School Days route: ") + error.message;
    return false;
  }
  info = "School Days route is valid";
  return true;
}

bool Launcher::checkPath(const std::string &path, std::string &info) {
  return Kotonoha::Providers::SchoolDaysAssets::checkDirectory(path, info);
}

const std::string &Launcher::routePath() const {
  return optionPath(routeOption);
}

void Launcher::setRoutePath(std::string path) {
  setOptionPath(routeOption, std::move(path));
}

bool Launcher::routeValid() const { return optionPathValid(routeOption); }

const std::string &Launcher::routeInfo() const {
  return optionPathInfo(routeOption);
}

} // namespace SchoolDays
