#include <SummerRadish/Native.hpp>
#include <SummerRadish/Pack.hpp>
#include <SummerRadish/Script.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace SummerRadish;
int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      std::cout << "Usage: SummerInspect PACK-DIRECTORY [--validate]\n";
      return 0;
    }
    auto root = std::filesystem::path(argv[1]);
    if (std::filesystem::exists(root / "DISC1/PACK"))
      root /= "DISC1/PACK";
    else if (std::filesystem::exists(root / "PACK"))
      root /= "PACK";
    Machine vm;
    for (auto type : {"INI", "SCRIPT", "GRAPHIC", "SOUND", "VOICE"}) {
      Pack pack(root / (std::string(type) + ".cmp"));
      std::cout << type << ": " << pack.entries().size() << " entries\n";
      if (std::string(type) == "SCRIPT")
        for (auto &[name, e] : pack.entries()) {
          auto d = pack.read(name);
          vm.add(name.substr(0, name.size() - 4),
                 std::string(d.begin(), d.end()));
        }
      if (argc > 2 && std::string(type) == "GRAPHIC") {
        size_t count = 0, regions = 0;
        for (auto &[name, e] : pack.entries()) {
          auto ext = name.substr(name.find_last_of('.') + 1);
          if (ext == "CMAP")
            continue;
          try {
            auto bytes = pack.read(name);
            auto img = decodeImage(bytes, ext);
            if (ext == "CHR")
              regions += decodeCharacterAnimations(bytes).size();
            ++count;
            if (name == "BG001_0.BGD" || name == "TITLE/TITLE.CGD" ||
                name == "ST_KGR1A.CHR") {
              std::string out = "/tmp/summer-" + std::to_string(count) + ".ppm";
              std::ofstream f(out, std::ios::binary);
              f << "P6\n" << img.width << ' ' << img.height << "\n255\n";
              for (size_t i = 0; i < img.rgba.size(); i += 4)
                f.write(reinterpret_cast<char *>(img.rgba.data() + i), 3);
              std::cout << name << " -> " << out << '\n';
            }
          } catch (const std::exception &e) {
            std::cerr << name << ": " << e.what() << '\n';
            return 1;
          }
        }
        std::cout << "Decoded " << count << " pictures; " << regions
                  << " animation regions\n";
      }
    }
    std::cout << "Parsed " << vm.programs().size() << " scripts\n";
    size_t messages = 0, choices = 0, functions = 0;
    std::string last;
    vm.function = [&](const std::string &name, const std::vector<Value> &args) {
      ++functions;
      last = name;
      if (name == "MESSAGE") {
        ++messages;
        vm.waiting = true;
      }
      if (name == "SELECT") {
        ++choices;
        vm.variables["@RESULT"] = int64_t(0);
      }
      if (name == "UNISELECT") {
        vm.variables["@制服番号"] = int64_t(2);
        vm.variables["@EV制服"] = int64_t(24);
      }
      if (name == "EASYTITLE")
        vm.variables["@RESULT"] = std::string("START");
      if (name == "MAPSELECT") {
        vm.variables["@移動場所"] = int64_t(0);
      }
      if (name == "TOPLADY")
        vm.variables["@FLAG020"] = int64_t(topLady(
            {number(vm.variables["@FLAG001"]), number(vm.variables["@FLAG002"]),
             number(vm.variables["@FLAG003"]),
             number(vm.variables["@FLAG004"])}));
      if (name == "SETQUITCODE" && !args.empty() && number(args[0]))
        vm.ended = true;
    };
    vm.start();
    vm.run();
    while (!vm.ended && messages < 3000 && functions < 100000) {
      vm.resume();
    }
    std::cout << messages << " messages, " << choices << " choices, "
              << functions << " functions; at " << vm.position.script << ':'
              << vm.position.pc << " (" << last << ")\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
