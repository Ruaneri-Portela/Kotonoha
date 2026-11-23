#include <Ddlc/DdlcAssets.hpp>
#include <Ddlc/Runtime.hpp>

#include <SDL3/SDL.h>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

// Minimal protocol-2 serializer for synthetic Ren'Py AST objects. No retail
// DDLC/Katawa assets or Ren'Py implementation code are used or committed.
struct Pickle {
  Bytes data{0x80, 2};
  void byte(uint8_t b) { data.push_back(b); }
  void u32(uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      byte(uint8_t(v >> (8 * i)));
  }
  void text(const std::string &v) {
    byte('X');
    u32(uint32_t(v.size()));
    data.insert(data.end(), v.begin(), v.end());
  }
  void raw(const std::string &v) {
    data.insert(data.end(), v.begin(), v.end());
  }
};
using Emit = std::function<void(Pickle &)>;

Emit nothing() {
  return [](Pickle &p) { p.byte('N'); };
}
Emit text(std::string value) {
  return [value = std::move(value)](Pickle &p) { p.text(value); };
}
Emit sequence(std::vector<Emit> values, bool tuple = false) {
  return [values = std::move(values), tuple](Pickle &p) {
    p.byte('(');
    for (const auto &write : values)
      write(p);
    p.byte(tuple ? 't' : 'l');
  };
}
Emit list(std::vector<Emit> values) { return sequence(std::move(values)); }
Emit tuple(std::vector<Emit> values) {
  return sequence(std::move(values), true);
}
Emit object(std::string name,
            std::vector<std::pair<std::string, Emit>> properties) {
  return
      [name = std::move(name), properties = std::move(properties)](Pickle &p) {
        p.byte('c');
        p.raw("renpy.ast\n" + name + "\n");
        p.byte(')');
        p.byte(0x81); // NEWOBJ
        p.byte('}');  // state dict
        for (const auto &field : properties) {
          p.text(field.first);
          field.second(p);
          p.byte('s');
        }
        p.byte('b'); // BUILD
      };
}
Emit ast(std::string name,
         std::vector<std::pair<std::string, Emit>> properties) {
  properties.emplace_back("linenumber", [](Pickle &p) {
    p.byte('K');
    p.byte(1);
  });
  return object(std::move(name), std::move(properties));
}
Emit label(std::string name, std::vector<Emit> block) {
  return ast("Label", {{"name", text(std::move(name))},
                       {"block", list(std::move(block))},
                       {"parameters", nothing()}});
}
Emit ret() { return ast("Return", {{"expression", nothing()}}); }
Emit say(std::string message) {
  return ast("Say", {{"who", nothing()},
                     {"what", text(std::move(message))},
                     {"attributes", nothing()},
                     {"with_", nothing()}});
}
// A compiled `$ ...` statement: the VM reads ast["code"]["source"].
Emit python(std::string code) {
  return ast("Python",
             {{"code", object("PyCode", {{"source", text(std::move(code))}})}});
}
Bytes compile(const Emit &script) {
  Pickle pickle;
  script(pickle);
  pickle.byte('.');
  uLongf compressedSize = compressBound(pickle.data.size());
  Bytes compressed(compressedSize);
  require(compress(compressed.data(), &compressedSize, pickle.data.data(),
                   pickle.data.size()) == Z_OK,
          "Compress synthetic compiled Ren'Py script");
  compressed.resize(compressedSize);
  return compressed;
}
struct Backend : Ddlc::Backend {
  bool imageSize(const std::string &, int &w, int &h) override {
    w = 1280;
    h = 720;
    return true;
  }
  void textSize(const std::string &value, const Ddlc::TextStyle &style,
                float &w, float &h) override {
    w = float(value.size()) * style.size * .5f;
    h = style.size;
  }
};
struct Temp {
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("kotonoha-renpy-movie-" + std::to_string(SDL_GetPerformanceCounter()));
  Temp() { std::filesystem::create_directories(directory); }
  ~Temp() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
};
} // namespace

int main() {
  try {
    const auto start = label(
        "movie_test",
        {python("cutsceneResult = renpy.movie_cutscene('video/synthetic.mkv')"),
         say("after the cutscene"), ret()});
    const auto root = tuple({nothing(), list({start})});

    Temp temp;
    const auto scriptPath = temp.directory / "script.rpyc";
    {
      const auto bytes = compile(root);
      std::ofstream output(scriptPath, std::ios::binary);
      output.write(reinterpret_cast<const char *>(bytes.data()),
                   std::streamsize(bytes.size()));
      require(bool(output), "Write synthetic script.rpyc");
    }
    Ddlc::Assets assets;
    require(assets.open(temp.directory.string()),
            "Open synthetic script source");
    Ddlc::Script script;
    std::string error;
    require(script.load(assets, &error), "Load synthetic Ren'Py AST");

    Backend backend;
    Ddlc::Vm vm(assets, script, backend);
    vm.init("");
    require(vm.errors.empty(), "Initialize synthetic VM");
    vm.start("movie_test");

    require(vm.interaction().k == Ddlc::Interaction::K::MovieCutscene,
            "renpy.movie_cutscene yields a MovieCutscene interaction");
    require(vm.interaction().movie == "video/synthetic.mkv",
            "Interaction carries the logical movie asset id");
    require(vm.interaction().action.cls() == "MovieCutscene",
            "Builtin returns a recognizable MovieCutscene action value");
    require(vm.interaction().action.get("filename").text() ==
                "video/synthetic.mkv",
            "Action value carries the filename");
    require(vm.interaction().dismiss, "Cutscene can be skipped with a click");

    vm.resume();
    for (int i = 0;
         i < 8 && (vm.interaction().k == Ddlc::Interaction::K::Transition ||
                   vm.interaction().k == Ddlc::Interaction::K::Pause);
         ++i)
      vm.resume();
    require(vm.interaction().k == Ddlc::Interaction::K::Say,
            "Script resumes after the cutscene");
    require(vm.scene.window.what == "after the cutscene",
            "Execution continues at the statement after the cutscene");
    require(vm.py.lookup("cutsceneResult").cls() == "MovieCutscene",
            "The cutscene action value is available to the script");
    vm.resume();
    require(vm.finished() && vm.errors.empty(),
            "Synthetic cutscene flow finishes without VM errors");
    std::cout << "DdlcMovieCutsceneTest: OK\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DdlcMovieCutsceneTest: " << error.what() << '\n';
    return 1;
  }
}
