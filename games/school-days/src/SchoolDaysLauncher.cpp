#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysAssets.hpp"

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
           "directory with Script/English. Data is read in place, never "
           "copied."}) {
  loadPath(std::move(directory));
  if (route.empty())
    route = Assets::findDefaultRoute().string();
  if (route.empty())
    route = rememberedPath(getSettingsPath(), RouteRememberFile);
  routeOption = addPathOption(
      {"Route file (.ktnroute)",
       PathKind::File,
       {{"KTRF routes", "ktnroute;ktrf"}, {"All files", "*"}},
       RouteRememberFile,
       "Select a compatible School Days HQ KTRF route."},
      [](const std::string &file, std::string &info) {
        Kotonoha::SchoolDaysKtrfSession session;
        Kotonoha_KtrfError error{};
        // Opening validates the route; scene assets are not accessed until
        // resolved.
        if (!session.Open(file.c_str(), ".", &error)) {
          info = std::string("Invalid School Days route: ") + error.message;
          return false;
        }
        info = "School Days route is valid";
        return true;
      },
      std::move(route));
}

bool Launcher::checkPath(const std::string &path, std::string &info) {
  return Assets::checkDirectory(path, info);
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
