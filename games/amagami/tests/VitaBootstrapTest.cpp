#include <Amagami/VitaScripts.hpp>
#include <Amagami/VitaVmBridge.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::string;
void check(bool pass, const char *message) {
  if (!pass)
    throw std::runtime_error(message);
}
template <class Fn> void rejects(Fn fn) {
  bool failed = false;
  try {
    fn();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Expected invalid input to be rejected");
}
void le16(Bytes &out, uint16_t n) {
  out += char(n);
  out += char(n >> 8);
}
void le32(Bytes &out, uint32_t n) {
  le16(out, uint16_t(n));
  le16(out, uint16_t(n >> 16));
}
void be16(Bytes &out, uint16_t n) {
  out += char(n >> 8);
  out += char(n);
}
void be32(Bytes &out, uint32_t n) {
  be16(out, uint16_t(n >> 16));
  be16(out, uint16_t(n));
}
void be64(Bytes &out, uint64_t n) {
  be32(out, uint32_t(n >> 32));
  be32(out, uint32_t(n));
}
void le64(Bytes &out, uint64_t n) {
  le32(out, uint32_t(n));
  le32(out, uint32_t(n >> 32));
}
Bytes scf(const std::string &name, bool run) {
  Bytes out("SCF\x1a", 4);
  out += char(0); // flags
  out += char(4); // SCF v4
  le16(out, uint16_t(name.size()));
  out += name;
  le16(out, 0);           // no parent
  le16(out, 0);           // instance fields
  le16(out, 0);           // class fields
  le16(out, run ? 1 : 0); // instance method count
  if (run) {
    le16(out, 3);
    out += "run";
    le16(out, 0);                        // arguments
    le16(out, 4);                        // bytecode length
    out += Bytes("\x24\x07\x03\x02", 4); // push 7, return
  }
  le16(out, 0); // class methods
  le16(out, 0); // constants
  return out;
}
Bytes systemImage() {
  const auto system = scf("System", false);
  Bytes raw;
  const uint32_t classOffset = 16 + 7; // fixed header + "System\0"
  le32(raw, classOffset + uint32_t(system.size()));
  le32(raw, 1);           // one class
  le32(raw, 16);          // name pointer
  le32(raw, classOffset); // SCF pointer
  raw += Bytes("System\0", 7);
  raw += system;
  return raw;
}
Bytes packLz(const Bytes &raw) {
  Bytes out;
  le32(out, uint32_t(raw.size()));
  for (size_t i = 0; i < raw.size();) {
    unsigned n = unsigned(std::min<size_t>(8, raw.size() - i));
    out += char((1u << n) - 1u); // literal flags, LSB first
    out.append(raw, i, n);
    i += n;
  }
  return out;
}
Bytes utf(Bytes table) {
  Bytes out("@UTF", 4);
  be32(out, uint32_t(table.size()));
  return out + table;
}
Bytes headerUtf() {
  Bytes t;
  be32(t, 50);
  be32(t, 50);
  be32(t, 74);
  be32(t, 0);
  be16(t, 2);
  be16(t, 0);
  be32(t, 1);
  t += char(0x36);
  be32(t, 0);
  be64(t, 128);
  t += char(0x36);
  be32(t, 10);
  be64(t, 256);
  t += Bytes("TocOffset\0ContentOffset\0", 24);
  return utf(t);
}
Bytes tocUtf(uint32_t scriptSize) {
  Bytes t;
  be32(t, 39);
  be32(t, 55);
  be32(t, 93);
  be32(t, 0);
  be16(t, 3);
  be16(t, 16);
  be32(t, 1);
  t += char(0x5a);
  be32(t, 0); // FileName
  t += char(0x56);
  be32(t, 9); // FileOffset
  t += char(0x54);
  be32(t, 20); // FileSize
  be32(t, 29);
  be64(t, 128);
  be32(t, scriptSize);
  t += Bytes("FileName\0FileOffset\0FileSize\0Test.scf\0", 38);
  return utf(t);
}
Bytes packet(const char *magic, const Bytes &data) {
  Bytes out(magic, 4);
  out += Bytes(4, '\0');
  le64(out, data.size());
  return out + data;
}
Bytes scenarioCpk() {
  const Bytes scenario = scf("Test", true);
  const Bytes header = packet("CPK ", headerUtf());
  const Bytes toc = packet("TOC ", tocUtf(uint32_t(scenario.size())));
  check(header.size() <= 128 && toc.size() <= 128, "Synthetic CPK offsets");
  Bytes out = header + Bytes(128 - header.size(), '\0') + toc;
  out += Bytes(256 - out.size(), '\0');
  out += scenario;
  return out;
}
void write(const std::filesystem::path &path, const Bytes &contents) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    throw std::runtime_error("Failed writing test input");
  out.write(contents.data(), std::streamsize(contents.size()));
  if (!out)
    throw std::runtime_error("Incomplete test write");
}
void testBridge() {
  using namespace Amagami;
  Vita::VitaVmBridge host;
  Vm vm;
  vm.scopedPrimitive = [&host](const std::string &owner,
                               const std::string &method, bool cls,
                               const VmValue &self, unsigned opcode,
                               const std::vector<VmValue> &args) {
    return host.primitive(owner, method, cls, self, opcode, args);
  };
  Script k2x;
  k2x.name = "K2X_Script";
  for (const auto &spec :
       std::vector<std::pair<std::string, unsigned>>{{"getClock", 5},
                                                     {"setScrScl", 6},
                                                     {"setScrOrgY", 7},
                                                     {"unlockTrophy", 8}}) {
    Method method;
    method.name = spec.first;
    method.classMethod = true;
    method.arguments = spec.second == 5 ? 0 : 1;
    method.code = {0x34, uint8_t(spec.second), 2};
    k2x.methods.push_back(std::move(method));
  }
  vm.add(k2x);
  const auto cls = VmValue::str("K2X_Script", VmValue::Class);
  vm.call(cls, "setScrScl", {VmValue::num(0.775, true)});
  vm.run();
  check(vm.finished() && std::abs(host.view().scale - 0.775) < 1e-9,
        "Vita setScrScl changes only the view");
  vm.call(cls, "setScrOrgY", {VmValue::num(16)});
  vm.run();
  check(host.view().originY == 16, "Vita setScrOrgY stores view origin");
  vm.call(cls, "getClock");
  vm.run();
  const auto now = vm.result();
  check(now.kind == VmValue::Array && now.object &&
            now.object->fields.size() == 4 &&
            now.object->fields[0].number >= 1 &&
            now.object->fields[0].number <= 12 &&
            now.object->fields[1].number >= 1 &&
            now.object->fields[1].number <= 31,
        "Vita getClock month/day/hour/minute array");
  vm.call(cls, "unlockTrophy", {VmValue::num(123)});
  vm.run();
  check(vm.finished(), "Trophy native returns without a platform service");
  rejects([&] {
    host.primitive("K2X_Script", "setScrScl", true, cls, 6, {VmValue::num(-1)});
  });
  rejects([&] {
    host.primitive("K2X_Script", "setScrOrgY", true, cls, 7,
                   {VmValue::num(100000)});
  });
  check(!host.primitive("OtherClass", "setScrScl", true, cls, 6,
                        {VmValue::num(0.5)}),
        "Native implementation does not swallow unrelated classes");
  check(
      !host.primitive("K2X_Script", "other", true, cls, 6, {VmValue::num(0.5)}),
      "Native implementation does not swallow unknown method");
}
} // namespace

int main() {
  try {
    using namespace Amagami;
    testBridge();
    const auto path =
        std::filesystem::temp_directory_path() /
        ("kotonoha-vita-scripts-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
      }
    } cleanup{path};
    write(path / "AMAGAMI.IMG", packLz(systemImage()));
    write(path / "SCENARIO.CPK", scenarioCpk());
    Amagami::Vita::VitaScripts source(path);
    check(source.systemCount() == 1 && source.scenarioCount() == 1 &&
              source.hasScenario("Test") && !source.hasScenario("Unknown"),
          "Vita class and SCF scenario inventory");
    check(source.scenario("Test").name == "Test", "Vita CPK SCF parse");
    rejects([&] { source.scenario("Unknown"); });
    Amagami::Vm vm;
    source.installSystem(vm);
    check(vm.hasClass("System") && !vm.hasClass("Test"),
          "Install system image without scenarios");

    // A Vita game must not preload the entire SCENARIO.CPK; otherwise
    // Class.findClass() reports scenario classes that haven't been loaded.
    Vm lazy;
    source.installSystem(lazy);
    lazy.resolveClass = [&source](const std::string &name)
        -> std::optional<Script> {
      return source.hasScenario(name)
                 ? std::optional<Script>(source.scenario(name))
                 : std::nullopt;
    };
    auto classReceiver = VmValue::str("Class", VmValue::Class);
    lazy.call(classReceiver, "findClass", {VmValue::str("Test")});
    check(lazy.finished() && lazy.result().kind == VmValue::Nil &&
              !lazy.hasClass("Test"),
          "findClass must not implicitly load a Vita scenario");

    Vita::VitaVmBridge runtime;
    runtime.setClassLoader([&lazy](const std::string &name) {
      return lazy.loadClass(name);
    });
    Script k2xLoad;
    k2xLoad.name = "K2X_Script";
    Method load;
    load.name = "loadClass";
    load.arguments = 1;
    load.code = {0x34, 2, 0x03};
    k2xLoad.methods.push_back(load);
    lazy.add(k2xLoad);
    lazy.scopedPrimitive = [&runtime](
                               const std::string &owner,
                               const std::string &method, bool classMethod,
                               const VmValue &receiver, unsigned native,
                               const std::vector<VmValue> &args) {
      return runtime.primitive(owner, method, classMethod, receiver, native,
                               args);
    };
    lazy.call(lazy.instance("K2X_Script"), "loadClass",
              {VmValue::str("Test", VmValue::Symbol)});
    lazy.run();
    check(lazy.finished() && lazy.result().kind == VmValue::Class &&
              lazy.result().text == "Test" && lazy.hasClass("Test"),
          "K2X loadClass must fetch the requested SCF from the CPK");
    lazy.call(classReceiver, "findClass", {VmValue::str("Test")});
    check(lazy.result().kind == VmValue::Class,
          "findClass can now resolve the explicitly loaded scenario");
    lazy.call(lazy.instance("K2X_Script"), "loadClass",
              {VmValue::str("Missing", VmValue::Symbol)});
    lazy.run();
    check(lazy.result().kind == VmValue::Nil && !lazy.hasClass("Missing"),
          "Missing Vita scenarios return nil, never a phantom class");

    // A class literal loads its scenario when first evaluated, independently
    // of explicit K2X_Script.loadClass.
    Vm literalVm;
    source.installSystem(literalVm);
    literalVm.resolveClass = lazy.resolveClass;
    Script literalCaller;
    literalCaller.name = "ClassLiteralTest";
    Value klass;
    klass.type = Value::Class;
    klass.text = "Test";
    literalCaller.constants.push_back(klass);
    Method entry;
    entry.name = "run";
    entry.code = {0x50, 0, 0x03};
    literalCaller.methods.push_back(entry);
    literalVm.add(literalCaller);
    check(!literalVm.hasClass("Test"), "Scenario begins unloaded");
    literalVm.call(literalVm.instance("ClassLiteralTest"), "run");
    literalVm.run();
    check(literalVm.finished() && literalVm.hasClass("Test") &&
              literalVm.result().kind == VmValue::Class,
          "Explicit class literals resolve SCF scenarios only on access");

    const auto k2xSelf = VmValue::ref(std::make_shared<VmObject>());
    runtime.tick(3);
    const auto clock = runtime.primitive("K2X_Script", "getTick", false,
                                         k2xSelf, 4, {});
    check(clock && clock->number == 3 && runtime.frameCount() == 3,
          "K2X frame clock advances without any scene objects");
    check(runtime.primitive("K2X_Script", "hasChanged", false,
                            k2xSelf, 1, {})->kind == VmValue::Boolean &&
              runtime.primitive("K2X_Script", "isReady", false,
                                k2xSelf, 3, {})->number == 1,
          "K2X status natives return typed values");
    rejects([&] {
      runtime.primitive("K2X_Script", "loadClass", false, k2xSelf, 2,
                        {VmValue::num(42)});
    });

    source.installAll(vm);
    check(vm.hasClass("Test"), "Install all Vita script classes");
    vm.call(vm.instance("Test"), "run");
    vm.run();
    check(vm.finished() && vm.result().number == 7,
          "Run Vita SCF scenario through existing VM");
    std::cout << "Vita SCF VM bootstrap and native view tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
