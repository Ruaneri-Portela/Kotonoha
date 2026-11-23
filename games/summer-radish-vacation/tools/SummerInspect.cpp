#include <SummerRadish/Codec.hpp>
#include <SummerRadish/Images.hpp>
#include <SummerRadish/Map.hpp>
#include <SummerRadish/Native.hpp>
#include <SummerRadish/Pack.hpp>

#include <SummerRadish/Script.hpp>
#include <SummerRadish/SummerAssets.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace SummerRadish;
int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      std::cout << "Usage: SummerInspect PACK-DIRECTORY [--validate | --list | "
                   "--read PACK/ASSET]\n";
      return 0;
    }
    Kotonoha::Providers::SummerAssets assets(argv[1]);
    const std::string mode = argc > 2 ? argv[2] : "";
    if (mode == "--read" && argc > 3) {
      const auto id = canonical(argv[3]);
      if (!assets.contains(id))
        throw std::runtime_error("Expected an existing PACK/ASSET identifier");
      const auto data = assets.read(id);
      std::cout.write(reinterpret_cast<const char *>(data.data()), data.size());
      return 0;
    }
    if (mode == "--list") {
      for (const auto &id : assets.list())
        std::cout << id << '\n';
      return 0;
    }
    Machine vm;
    bool isMeo = false;
    size_t meoScriptCount = 0;
    for (auto type : {"INI", "SCRIPT", "GRAPHIC", "SOUND", "VOICE"}) {
      const auto prefix = std::string(type) + "/";
      const auto entries = assets.list(prefix);
      if (entries.empty())
        continue;
      std::cout << type << ": " << entries.size() << " entries\n";
      if (std::string(type) == "SCRIPT") {
        for (const auto &id : entries) {
          const auto name = id.substr(prefix.size());
          if (name.size() <= 4)
            continue;
          const auto ext = canonical(name.substr(name.find_last_of('.') + 1));
          if (ext != "XML")
            continue;
          auto d = assets.read(id);
          const std::string xml(d.begin(), d.end());
          if (xml.find("<Script>") != std::string::npos) {
            isMeo = true;
            parseXml(xml);
            ++meoScriptCount;
          } else {
            vm.add(name.substr(0, name.size() - 4), xml);
          }
        }
      }
      if (mode == "--validate" && std::string(type) == "GRAPHIC") {
        size_t count = 0, regions = 0;
        for (const auto &id : entries) {
          const auto name = id.substr(prefix.size());
          auto ext = canonical(name.substr(name.find_last_of('.') + 1));
          if (ext == "CMAP" || ext == "M1V" || ext == "SFD" || ext == "MAP" ||
              ext == "DAT")
            continue;
          try {
            auto bytes = assets.read(id);
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
    if (isMeo) {
      std::cout << "Parsed " << meoScriptCount << " MEO timeline scripts\n";
      return 0;
    }
    std::cout << "Parsed " << vm.programs().size() << " scripts\n";
    size_t messages = 0, choices = 0, functions = 0;
    std::string last;
    const bool isSrv2 = assets.contains("GRAPHIC/MESFRAME/MESFRAME.TEXB");
    vm.function = [&](const std::string &name, const std::vector<Value> &args) {
      ++functions;
      last = name;
      if (name == "MESSAGE") {
        ++messages;
        vm.waiting = true;
      }
      if (name == "SELECT") {
        ++choices;
        const auto &node = vm.programs()
                               .at(vm.position.script)
                               .code.at(vm.position.pc - 1)
                               .node;
        const std::string target = node.children.empty()
                                       ? "@RESULT"
                                       : vm.referenceKey(node.children.front());
        vm.variables[target] = int64_t(0);
      }
      if (name == "UNISELECT") {
        vm.variables["@制服番号"] = int64_t(2);
        vm.variables["@EV制服"] = int64_t(24);
      }
      if (name == "EASYTITLE")
        vm.variables["@RESULT"] = std::string("START");
      if (name == "MAPSELECT") {
        int mode = args.empty() ? 0 : int(number(args[0]));
        const auto &layout = mapLayout(mode, isSrv2);
        int chosen = 0;
        for (const auto &location : layout.locations) {
          auto key = mapCharacterVariable(location.result, isSrv2);
          key.replace(0, 5, "FILE_");
          auto it = vm.variables.find("@" + key);
          if (mode == 2 || (it != vm.variables.end() &&
                            std::holds_alternative<std::string>(it->second) &&
                            !string(it->second).empty())) {
            chosen = location.result;
            break;
          }
        }
        vm.variables["@移動場所"] = int64_t(chosen);
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
