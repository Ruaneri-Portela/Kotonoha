#include <Ddlc/DdlcAssets.hpp>
#include <Ddlc/Runtime.hpp>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

struct HeadlessBackend : Ddlc::Backend {
  bool imageSize(const std::string &, int &width, int &height) override {
    width = 800;
    height = 600;
    return true;
  }
  void textSize(const std::string &text, const Ddlc::TextStyle &style,
                float &width, float &height) override {
    width = float(text.size()) * style.size * .5f;
    height = style.size;
  }
  void log(const std::string &message) override {
    if (message.find("unknown image") != std::string::npos)
      std::fprintf(stderr, "%s\n", message.c_str());
  }
};

} // namespace

int main(int argc, char **argv) {
  if (argc != 2)
    return 77;
  try {
    Kotonoha::Providers::DdlcAssets game(argv[1]);
    HeadlessBackend backend;
    Ddlc::Vm vm(game.source(), game.script(), backend);
    const auto user =
        std::filesystem::temp_directory_path() /
        ("katawa-story-" + std::to_string(SDL_GetPerformanceCounter()));
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
      }
    } cleanup{user};
    std::filesystem::create_directories(user);
    vm.userDirectory = user;
    double clock = 0;
    vm.clock = [&clock] { return clock; };
    vm.init({});
    for (const auto &error : vm.errors)
      std::fprintf(stderr, "init: %s\n", error.c_str());
    vm.errors.clear();
    vm.py.set("playthroughflag", Ddlc::Value::integer(1));
    vm.start("start");
    int dialogue = 0;
    int choices = 0;
    for (int step = 0; step < 3000 && vm.running() && !vm.finished(); ++step) {
      const auto &interaction = vm.interaction();
      if (interaction.k == Ddlc::Interaction::K::Say)
        ++dialogue;
      if (interaction.k == Ddlc::Interaction::K::Menu)
        ++choices;
      clock += interaction.deadline > 0 ? interaction.deadline + 1 : 10;
      if (interaction.k == Ddlc::Interaction::K::Menu &&
          !interaction.items.empty())
        vm.resume(interaction.items.front().value);
      else
        vm.resume();
      if (!vm.errors.empty())
        throw std::runtime_error(vm.errors.front());
    }
    std::printf("Katawa story: %d dialogue, %d choices, file %s\n", dialogue,
                choices,
                vm.currentNode() ? vm.currentNode()->file.c_str() : "end");
    if (dialogue < 100 || choices < 1)
      throw std::runtime_error("Katawa story did not reach a playable route");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "KatawaStoryTest: %s\n", error.what());
    return 1;
  }
  return 0;
}
