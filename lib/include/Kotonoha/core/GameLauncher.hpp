#pragma once

#include <Kotonoha/core/Launcher.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha {

// Launcher for games that read the user's own data from a disc or folder,
// never copied. Games can add other required paths with their own validators.
class GameLauncher : public Launcher {
public:
  enum class PathKind { File, Folder };
  struct PathOptions {
    std::string label; // field title, e.g. "Disc image"
    PathKind kind = PathKind::File;
    FileFilters filters;      // File only
    std::string rememberFile; // e.g. "disc.txt"; empty = not remembered
    std::string hint;         // help text under the field
  };

  GameLauncher(std::string title, std::string settingsPath, PathOptions options,
               EngineSettings defaults = {});

  // The path saved by a previous launch (empty when none), so games started
  // without the launcher reuse it.
  static std::string rememberedPath(const std::string &settingsPath,
                                    const std::string &rememberFile);

  const std::string &path() const;
  void setPath(std::string path);
  bool pathValid() const;
  const std::string &pathInfo() const;

protected:
  using PathValidator =
      std::function<bool(const std::string &path, std::string &info)>;

  // Checks `path` (never empty); `info` is shown in green when valid, in
  // orange with the reason otherwise.
  virtual bool checkPath(const std::string &path, std::string &info) = 0;
  // Call from the derived constructor (so checkPath() dispatches to it):
  // `path` when not empty, otherwise the remembered one.
  void loadPath(std::string path);

  std::size_t addPathOption(PathOptions options, PathValidator validator,
                            std::string path = {});
  const std::string &optionPath(std::size_t index) const;
  void setOptionPath(std::size_t index, std::string path);
  bool optionPathValid(std::size_t index) const;
  const std::string &optionPathInfo(std::size_t index) const;

  void drawGameOptions() override;
  bool validate(std::string &error) override;
  void save() override;

private:
  struct PathField {
    PathOptions options;
    PathValidator validator;
    std::string current, checked, info, rememberPath;
    bool valid = false, checkedOnce = false;
  };
  PathField game;
  std::vector<std::unique_ptr<PathField>> additionalPaths;
  void setFieldPath(PathField &field, std::string path);
  void check(PathField &field, bool force = false);
  void drawPathField(PathField &field);
};

} // namespace Kotonoha
