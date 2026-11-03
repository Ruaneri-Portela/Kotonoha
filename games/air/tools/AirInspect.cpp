#include <Air/Script.hpp>
#include <iostream>
#include <libreallive/scenario.h>
#include <sstream>
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      throw std::runtime_error("AirInspect Seen.txt [scenario]");
    Air::ScriptArchive a(argv[1]);
    if (argc == 2) {
      std::cout << a.ScenarioNumbers().size() << " scenarios; "
                << a.ValidateAll() << " elements\n";
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
