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

// Minimal protocol-2 serializer for fresh, synthetic Ren'Py AST objects.
// Neither Ren'Py's copyrighted implementation nor retail game files are
// needed to check the actual Kotonoha script loader and VM.
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
Emit truth(bool value) {
  return [value](Pickle &p) { p.byte(value ? 0x88 : 0x89); };
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
Emit mkLabel(std::string name, std::vector<Emit> block, Emit params = nothing()) {
  return ast("Label", {{"name", text(std::move(name))},
                       {"block", list(std::move(block))},
                       {"parameters", std::move(params)}});
}
Emit ret(Emit expression = nothing()) {
  return ast("Return", {{"expression", std::move(expression)}});
}
Emit say(std::string message) {
  return ast("Say", {{"who", nothing()},
                     {"what", text(std::move(message))},
                     {"attributes", nothing()},
                     {"with_", nothing()}});
}
Emit dynamicJump(const std::string &expression) {
  return ast("Jump", {{"target", text(expression)},
                      {"expression", truth(true)},
                      {"global_label", text("start")}});
}
Emit dynamicCall(const std::string &expression, Emit args) {
  return ast("Call", {{"label", text(expression)},
                      {"expression", truth(true)},
                      {"global_label", text("start")},
                      {"arguments", std::move(args)}});
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
  void textSize(const std::string &text, const Ddlc::TextStyle &style, float &w,
                float &h) override {
    w = float(text.size()) * style.size * .5f;
    h = style.size;
  }
};
struct Temp {
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("kotonoha-renpy-flow-" + std::to_string(SDL_GetPerformanceCounter()));
  Temp() { std::filesystem::create_directories(directory); }
  ~Temp() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
};
} // namespace

int main() {
  try {
    // The synthetic source uses both .rpyc and compiled .rpymc modules.
    auto parameters =
        object("ParameterInfo",
               {{"parameters", list({tuple({text("a"), nothing()}),
                                     tuple({text("b"), text("3")})})}});
    auto args = object("ArgumentInfo",
                       {{"arguments", list({tuple({nothing(), text("7")}),
                                            tuple({text("b"), text("9")})})}});
    auto start = mkLabel("start", {dynamicJump("'.branch'")});
    auto branch = mkLabel("start.branch", {dynamicCall("'.helper'", args),
                                         say("back from call"), ret()});
    auto helper = mkLabel("start.helper", {ret(text("a + b"))}, parameters);
    auto menu =
        ast("Menu", {{"items", list({tuple({text("Enabled"), nothing(),
                                            list({ret(text("123"))})}),
                                     tuple({text("Disabled"), text("False"),
                                            list({ret(text("456"))})})})}});
    auto choices = mkLabel("menu_test", {menu, ret(text("555"))});
    auto emptyMenu =
        ast("Menu", {{"items", list({tuple({text("Unavailable"), text("False"),
                                            list({ret(text("456"))})})})}});
    auto disabled = mkLabel("menu_empty", {emptyMenu, ret(text("789"))});
    auto root =
        tuple({nothing(), list({start, branch, helper, choices, disabled})});

    Temp temp;
    const auto scriptPath = temp.directory / "script.rpyc";
    const auto modulePath = temp.directory / "compat.rpymc";
    {
      const auto bytes = compile(root);
      std::ofstream output(scriptPath, std::ios::binary);
      output.write(reinterpret_cast<const char *>(bytes.data()),
                   std::streamsize(bytes.size()));
      require(bool(output), "Write synthetic script.rpyc");
    }
    {
      const auto bytes =
          compile(tuple({nothing(), list({mkLabel("module_label", {ret()})})}));
      std::ofstream output(modulePath, std::ios::binary);
      output.write(reinterpret_cast<const char *>(bytes.data()),
                   std::streamsize(bytes.size()));
      require(bool(output), "Write synthetic compat.rpymc");
    }
    // A standard Ren'Py project may use script.rpyc without DDLC archives
    // and without a splashscreen. The generic provider must accept it.
    Kotonoha::Providers::DdlcAssets generic(temp.directory);
    require(!generic.isDdlc() && !generic.isKatawa() &&
                generic.script().hasLabel("start") &&
                generic.name().rfind("Ren'Py", 0) == 0,
            "Load generic compiled Ren'Py game without DDLC assumptions");

    Ddlc::Assets assets;
    require(assets.open(temp.directory.string()),
            "Open synthetic script source");
    Ddlc::Script script;
    std::string error;
    require(script.load(assets, &error), "Load synthetic Ren'Py AST");
    require(script.fileCount() == 2 && script.hasLabel("module_label"),
            "Scan compiled .rpymc alongside .rpyc");
    Backend backend;
    Ddlc::Vm vm(assets, script, backend);
    vm.init("");
    require(vm.errors.empty(), "Initialize synthetic VM");

    vm.start("start");
    // A fresh say window may begin with a dissolve, before dialogue itself.
    for (int i = 0;
         i < 8 && (vm.interaction().k == Ddlc::Interaction::K::Transition ||
                   vm.interaction().k == Ddlc::Interaction::K::Pause);
         ++i)
      vm.resume();
    if (vm.interaction().k != Ddlc::Interaction::K::Say) {
      std::cerr << "Expected Say, got interaction kind "
                << int(vm.interaction().k) << "\n";
      for (const auto &error : vm.errors)
        std::cerr << "VM: " << error << "\n";
    }
    require(vm.interaction().k == Ddlc::Interaction::K::Say,
            "Dynamic relative jump and call reach dialogue");
    require(vm.scene.window.what == "back from call",
            "Dialogue resumes after a relative call");
    require(vm.py.lookup("_return").integer() == 16,
            "Positional and keyword arguments reach a local helper label");
    vm.resume();
    require(vm.finished() && vm.errors.empty(),
            "Relative label flow finishes without VM errors");

    vm.start("menu_test");
    require(vm.interaction().k == Ddlc::Interaction::K::Menu,
            "Present Ren'Py menu");
    require(vm.interaction().items.size() == 1,
            "Missing menu condition is unconditional; False is hidden");
    vm.resume(vm.interaction().items.front().value);
    require(vm.finished() && vm.py.lookup("_return").integer() == 123,
            "Menu dispatches selected original choice");
    require(vm.errors.empty(), "Menu flow has no VM errors");

    vm.start("menu_test");
    require(vm.interaction().k == Ddlc::Interaction::K::Menu,
            "Menu can be presented again");
    vm.resume(); // No choice selected.
    require(vm.finished() && vm.py.lookup("_return").integer() == 555,
            "Cancelled menu falls through without selecting the first item");

    vm.start("menu_empty");
    require(vm.finished() && vm.py.lookup("_return").integer() == 789,
            "Menu without eligible choices falls through without blocking");
    require(vm.errors.empty(), "Synthetic flow reports no errors");
    std::cout << "DdlcSyntheticFlowTest: OK\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DdlcSyntheticFlowTest: " << error.what() << '\n';
    return 1;
  }
}
