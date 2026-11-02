// Deterministic retail script coverage; this does not simulate presentation.
#include <SummerRadish/Map.hpp>
#include <SummerRadish/Native.hpp>
#include <SummerRadish/Script.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace SummerRadish;
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected retail disc root");
    auto root = std::filesystem::path(argv[1]);
    if (std::filesystem::is_directory(root / "DISC1/PACK"))
      root /= "DISC1/PACK";
    else if (std::filesystem::is_directory(root / "PACK"))
      root /= "PACK";
    Pack scripts(root / "SCRIPT.cmp");
    Machine index;
    for (auto &[name, entry] : scripts.entries()) {
      auto bytes = scripts.read(name);
      index.add(name.substr(0, name.size() - 4),
                std::string(bytes.begin(), bytes.end()));
    }
    std::set<std::string> scenarios, functions;
    std::size_t messages = 0, choices = 0, ended = 0;
    for (int uniform = 0; uniform < 3; ++uniform)
      for (int policy = 0; policy < 8; ++policy) {
        Machine vm = index;
        std::size_t steps = 0, selects = 0, maps = 0, titles = 0;
        vm.function = [&](const std::string &name,
                          const std::vector<Value> &args) {
          functions.insert(name);
          scenarios.insert(vm.position.script);
          vm.waiting = true;
          if (name == "MESSAGE") {
            ++messages;
            vm.markRead(vm.resource.children.at(1));
          } else if (name == "SELECT") {
            const auto count = (vm.resource.children.size() - 1) / 2;
            if (!count)
              throw std::runtime_error("Empty retail selection");
            const auto &instruction = vm.programs()
                                          .at(vm.position.script)
                                          .code.at(vm.position.pc - 1)
                                          .node;
            const auto &target = instruction.children.at(0);
            vm.variables[vm.referenceKey(target)] =
                int64_t((policy + (policy / 2 ? selects : 0)) % count);
            ++selects;
            ++choices;
          } else if (name == "UNISELECT") {
            vm.variables["@制服番号"] = int64_t(uniform + 2);
            vm.variables["@EV制服"] = int64_t(uniform + 24);
          } else if (name == "EASYTITLE") {
            if (titles++)
              vm.ended = true;
            else
              vm.variables["@RESULT"] = std::string("START");
          } else if (name == "MAPSELECT") {
            int mode = args.empty() ? 0 : int(number(args[0]));
            auto &layout = mapLayout(mode);
            std::vector<int> candidates;
            for (const auto &location : layout.locations) {
              auto key = mapCharacterVariable(location.result);
              key.replace(0, 5, "FILE_");
              auto it = vm.variables.find("@" + key);
              if (mode == 2 ||
                  (it != vm.variables.end() &&
                   std::holds_alternative<std::string>(it->second) &&
                   !string(it->second).empty()))
                candidates.push_back(location.result);
            }
            if (candidates.empty())
              throw std::runtime_error(
                  "No assigned script destination for map");
            vm.variables["@移動場所"] =
                int64_t(candidates.at((policy + maps++) % candidates.size()));
          } else if (name == "TOPLADY")
            vm.variables["@FLAG020"] =
                int64_t(topLady({number(vm.variables["@FLAG001"]),
                                 number(vm.variables["@FLAG002"]),
                                 number(vm.variables["@FLAG003"]),
                                 number(vm.variables["@FLAG004"])}));
          else if (name == "SETQUITCODE" && !args.empty() && number(args[0]))
            vm.ended = true;
        };
        try {
          vm.start();
          vm.run();
          while (!vm.ended && steps++ < 100000)
            vm.resume();
        } catch (const std::exception &e) {
          throw std::runtime_error("uniform=" + std::to_string(uniform) +
                                   " policy=" + std::to_string(policy) +
                                   " scenario=" + vm.position.script + ":" +
                                   std::to_string(vm.position.pc) + " " +
                                   e.what());
        }
        if (!vm.ended)
          throw std::runtime_error("Route policy did not terminate: uniform=" +
                                   std::to_string(uniform) +
                                   " policy=" + std::to_string(policy) +
                                   " at " + vm.position.script);
        ++ended;
      }
    if (scenarios.size() < 20 || messages < 1000 || choices < 100)
      throw std::runtime_error("Insufficient retail route coverage");
    std::cout << ended << " deterministic policies ended; " << scenarios.size()
              << " scenarios, " << messages << " messages, " << choices
              << " choices, " << functions.size() << " native function names\n";
    for (auto &s : scenarios)
      std::cout << s << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
