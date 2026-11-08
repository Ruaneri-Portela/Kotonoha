#include <RealLive/Script.hpp>
#include <iostream>
#include <libreallive/scenario.h>
#include <map>
#include <sstream>
#include <tuple>
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      throw std::runtime_error(
          "RealLiveInspect Seen.txt [scenario | --opcodes | --find text]");
    RealLive::ScriptArchive a(argv[1]);
    if (argc == 2) {
      std::cout << a.ScenarioNumbers().size() << " scenarios; "
                << a.ValidateAll() << " elements\n";
      return 0;
    }
    if (std::string(argv[2]) == "--opcodes") {
      using Opcode = std::tuple<int, int, int, int>;
      struct Entry {
        size_t count = 0;
        int scenario = 0;
        size_t index = 0;
      };
      std::map<Opcode, Entry> inventory;
      for (int scenario : a.ScenarioNumbers()) {
        size_t index = 0;
        for (const auto &element : a.Scenario(scenario)) {
          if (auto *c = dynamic_cast<const libreallive::CommandElement *>(
                  element.get())) {
            auto &entry = inventory[{c->modtype(), c->module(), c->opcode(),
                                     c->overload()}];
            if (!entry.count) {
              entry.scenario = scenario;
              entry.index = index;
            }
            ++entry.count;
          }
          ++index;
        }
      }
      std::cout << "type,module,opcode,overload,count,first_scenario,first_"
                   "instruction\n";
      for (const auto &[key, entry] : inventory) {
        const auto &[type, module, opcode, overload] = key;
        std::cout << type << ',' << module << ',' << opcode << ',' << overload
                  << ',' << entry.count << ',' << entry.scenario << ','
                  << entry.index << '\n';
      }
      return 0;
    }
    if (argc > 3 && std::string(argv[2]) == "--find") {
      for (int scenario : a.ScenarioNumbers()) {
        std::size_t index = 0;
        RLMachine machine;
        for (const auto &element : a.Scenario(scenario)) {
          std::ostringstream out;
          element->PrintSourceRepresentation(&machine, out);
          if (out.str().find(argv[3]) != std::string::npos)
            std::cout << scenario << ':' << index << ' ' << out.str() << '\n';
          ++index;
        }
      }
      return 0;
    }
    auto &s = a.Scenario(std::stoi(argv[2]));
    RLMachine m;
    size_t n = 0;
    for (auto &i : s) {
      std::cout << n++ << " ";
      i->PrintSourceRepresentation(&m, std::cout);
      std::cout << "\n";
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
