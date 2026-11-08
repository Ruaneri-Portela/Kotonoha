#include <Amagami/Session.hpp>
#include <Amagami/Source.hpp>
#include <Amagami/Vm.hpp>
#include <Kotonoha/media/Ps2Tim2.hpp>
#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <json-c/json.h>
#include <map>
#include <stdexcept>

namespace {
void print(const Amagami::Value &v) {
  if (v.type == Amagami::Value::Array) {
    std::cout << '[';
    for (const auto &e : v.elements) {
      print(e);
      std::cout << ' ';
    }
    std::cout << ']';
  } else if (v.type >= Amagami::Value::String)
    std::cout << v.text;
  else
    std::cout << "type=" << unsigned(v.type) << " bits=" << v.bits;
}
void dump(const Amagami::Script &s) {
  std::cout << s.name << " : " << s.parent << '\n';
  for (const auto &v : s.classVariables)
    std::cout << "class var slot=" << unsigned(v.slot) << " name=" << v.name
              << '\n';
  for (const auto &m : s.methods) {
    std::cout << (m.classMethod ? "+ " : "- ") << m.name
              << " args=" << m.arguments << " code=" << m.code.size() << '\n';
  }
  for (size_t i = 0; i < s.constants.size(); ++i) {
    std::cout << i << ": ";
    print(s.constants[i]);
    std::cout << '\n';
  }
}
size_t number(const char *text) {
  size_t consumed = 0;
  auto n = std::stoull(text, &consumed);
  if (consumed != std::string(text).size())
    throw std::invalid_argument("Invalid index");
  return n;
}
void printConstant(const Amagami::Value &v) {
  switch (v.type) {
  case Amagami::Value::Integer:
    std::cout << "int(" << int32_t(v.bits) << ")";
    break;
  case Amagami::Value::Float: {
    float f;
    std::memcpy(&f, &v.bits, 4);
    std::cout << "float(" << f << ")";
    break;
  }
  case Amagami::Value::False:
    std::cout << "false";
    break;
  case Amagami::Value::True:
    std::cout << "true";
    break;
  case Amagami::Value::String:
    std::cout << "\"" << v.text << "\"";
    break;
  case Amagami::Value::Symbol:
    std::cout << "#" << v.text;
    break;
  case Amagami::Value::Class:
    std::cout << "Class(" << v.text << ")";
    break;
  case Amagami::Value::Array:
    std::cout << "array(" << v.elements.size() << " elements)";
    break;
  default:
    std::cout << "type(" << unsigned(v.type) << ")";
    break;
  }
}
void disassemble(const Amagami::Script &s, const Amagami::Method &m) {
  std::cout << s.name << "." << m.name << " args=" << m.arguments
            << " code=" << m.code.size() << '\n';
  size_t pc = 0;
  while (pc < m.code.size()) {
    const size_t off = pc;
    const uint8_t op = m.code[pc++];
    auto byte = [&]() {
      if (pc >= m.code.size())
        throw std::runtime_error("truncated");
      return m.code[pc++];
    };
    auto word = [&]() { return unsigned(byte()) | (unsigned(byte()) << 8); };
    auto constIndex = [&](bool wide) { return wide ? word() : byte(); };
    const auto *info = Amagami::Vm::opcodeInfo(op);
    std::cout << '[' << off << "] " << (info ? info->name : "unknown") << ' ';
    auto printConst = [&](size_t i) {
      if (i < s.constants.size()) {
        std::cout << "  const#" << i << "=";
        printConstant(s.constants[i]);
      } else
        std::cout << "  const#" << i << "=<out-of-range>";
    };
    switch (op) {
    case 0x01:
      std::cout << "marker" << int(byte());
      break;
    case 0x08:
    case 0x0a:
      printConst(byte());
      std::cout << " slot=" << int(byte());
      break;
    case 0x09:
    case 0x0b:
      printConst(word());
      std::cout << " slot=" << int(byte());
      break;
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x34:
      std::cout << int(byte());
      break;
    case 0x30:
    case 0x31: {
      unsigned count = byte(), index = byte();
      std::cout << "argc=" << count << " ";
      printConst(index);
      break;
    }
    case 0x32:
    case 0x33: {
      unsigned count = byte(), index = word();
      std::cout << "argc=" << count << " ";
      printConst(index);
      break;
    }
    case 0x40:
    case 0x44:
      std::cout << "off=" << int(int8_t(byte()));
      break;
    case 0x41:
    case 0x45:
      std::cout << "off=" << int(int16_t(word()));
      break;
    case 0x50:
      printConst(byte());
      break;
    case 0x51:
      printConst(word());
      break;
    case 0x69:
      std::cout << "target=" << word();
      break;
    case 0x6c:
      std::cout << "n=" << int(byte());
      break;
    case 0x6d:
      std::cout << "marker16=" << word();
      break;
    case 0x1c:
    case 0x1b:
    case 0x1a:
    case 0x19:
    case 0x1d:
    case 0x18:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
      break;
    default:
      break;
    }
    std::cout << '\n';
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout
          << "AmagamiInspect DISC.iso [--verify | --opcodes | --script NAME | "
             "--class NAME |\n"
             "--bytecode CLASS METHOD | --classes | --primitives | --audit "
             "REPORT.json |\n"
             "  --senders SELECTOR | --extract ARCHIVE INDEX OUTPUT | "
             "--png GRAPH_ARCHIVE INDEX OUTPUT]\n";
      return argc < 2 ? 1 : 0;
    }
    auto sharedSource = std::make_shared<Amagami::Source>(argv[1]);
    auto &source = *sharedSource;
    if (argc == 2 || (argc == 3 && std::string(argv[2]) == "--verify")) {
      size_t methods = 0, texts = 0, pictures = 0;
      for (size_t i = 0; i < source.scenarios().size(); ++i) {
        auto s = source.script(i);
        methods += s.methods.size();
        texts += s.texts().size();
      }
      for (const auto &a :
           {"GRAPH/GRAPH0.ARC", "GRAPH/GRAPH1.ARC", "GRAPH/GRAPH2.ARC"}) {
        const auto &archive = source.archive(a);
        std::cout << a << ": " << archive.entries.size() << " entries\n";
        if (argc == 3)
          for (size_t i = 0; i < archive.entries.size(); ++i) {
            auto b = source.read(a, i);
            if (b.size() >= 4 &&
                std::string(b.begin(), b.begin() + 4) == "TIM2") {
              Kotonoha::Ps2Image image;
              if (!Kotonoha::DecodePs2Tim2(b.data(), b.size(), &image))
                throw std::runtime_error(std::string(a) + " invalid TIM2 #" +
                                         std::to_string(i));
              ++pictures;
            }
          }
      }
      for (const auto &a :
           {"SOUND/MUSIC.AFS", "SOUND/SOUND.AFS", "SOUND/VOICE.AFS"})
        std::cout << a << ": " << source.archive(a).entries.size()
                  << " entries\n";
      std::cout << "SCF scenarios=" << source.scenarios().size()
                << " methods=" << methods << " string_literals=" << texts
                << " system_classes=" << source.classes().size()
                << " TIM2_decoded=" << pictures << '\n';
      if (argc == 3) {
        auto unpacked =
            Amagami::Unpack(source.disc()->read("GRAPH/GRAPH0.PAC"));
        if (unpacked != source.disc()->read("GRAPH/GRAPH0.ARC"))
          throw std::runtime_error("GRAPH0.PAC does not match GRAPH0.ARC");
        std::cout << "GRAPH0.PAC matches GRAPH0.ARC byte for byte\n";
      }
      return 0;
    }
    const std::string action = argv[2];
    if (argc == 3 && action == "--primitives") {
      struct Entry {
        size_t count = 0;
        std::string first;
      };
      std::map<unsigned, Entry> inventory;
      auto inspect = [&](const Amagami::Script &script) {
        for (const auto &method : script.methods)
          for (size_t pc = 0; pc < method.code.size();) {
            const auto op = method.code[pc];
            const auto *info = Amagami::Vm::opcodeInfo(op);
            if (!info || pc + 1 + info->operands > method.code.size())
              throw std::runtime_error("Invalid instruction in " + script.name +
                                       ":" + method.name + ":" +
                                       std::to_string(pc));
            if (op == 0x34) {
              auto &entry = inventory[method.code[pc + 1]];
              if (!entry.count)
                entry.first =
                    script.name + ":" + method.name + ":" + std::to_string(pc);
              ++entry.count;
            }
            pc += 1 + info->operands;
          }
      };
      for (size_t i = 0; i < source.scenarios().size(); ++i)
        inspect(source.script(i));
      for (size_t i = 0; i < source.classes().size(); ++i)
        inspect(source.systemClass(i));
      std::cout << "primitive,count,first_location\n";
      for (const auto &[prim, entry] : inventory)
        std::cout << prim << ',' << entry.count << ',' << entry.first << '\n';
      return 0;
    }
    if (argc == 4 && action == "--senders") {
      const std::string needle = argv[3];
      auto inspect = [&](const Amagami::Script &script) {
        for (const auto &m : script.methods)
          for (size_t pc = 0; pc < m.code.size();) {
            const auto op = m.code[pc];
            const auto *info = Amagami::Vm::opcodeInfo(op);
            if (!info || pc + 1 + info->operands > m.code.size())
              throw std::runtime_error("Invalid instruction in " + script.name +
                                       ":" + m.name + ":" + std::to_string(pc));
            const size_t operand = pc + 1;
            if (op == 0x30 || op == 0x31 || op == 0x32 || op == 0x33) {
              const bool wide = (op & 2) != 0;
              const unsigned index =
                  wide ? unsigned(m.code[operand + 1]) |
                             (unsigned(m.code[operand + 2]) << 8)
                       : m.code[operand + 1];
              if (index < script.constants.size()) {
                const auto &c = script.constants[index];
                if (c.text == needle)
                  std::cout << script.name << '.' << m.name << ':' << pc
                            << '\n';
              }
            }
            pc += 1 + info->operands;
          }
      };
      for (size_t i = 0; i < source.scenarios().size(); ++i)
        inspect(source.script(i));
      for (size_t i = 0; i < source.classes().size(); ++i)
        inspect(source.systemClass(i));
      return 0;
    }
    if (action == "--opcodes") {
      struct Entry {
        size_t count = 0;
        std::string first;
      };
      std::map<uint8_t, Entry> inventory;
      auto inspect = [&](const Amagami::Script &script) {
        for (const auto &method : script.methods)
          for (size_t pc = 0; pc < method.code.size();) {
            const auto op = method.code[pc];
            const auto *info = Amagami::Vm::opcodeInfo(op);
            if (!info || pc + 1 + info->operands > method.code.size())
              throw std::runtime_error("Invalid instruction in " + script.name +
                                       ":" + method.name + ":" +
                                       std::to_string(pc));
            auto &entry = inventory[op];
            if (!entry.count)
              entry.first =
                  script.name + ":" + method.name + ":" + std::to_string(pc);
            ++entry.count;
            pc += 1 + info->operands;
          }
      };
      for (size_t i = 0; i < source.scenarios().size(); ++i)
        inspect(source.script(i));
      for (size_t i = 0; i < source.classes().size(); ++i)
        inspect(source.systemClass(i));
      std::cout << "opcode,name,supported,count,first_location\n";
      for (const auto &[op, entry] : inventory) {
        const auto *info = Amagami::Vm::opcodeInfo(op);
        std::cout << unsigned(op) << ',' << info->name << ',' << info->supported
                  << ',' << entry.count << ',' << entry.first << '\n';
      }
      return 0;
    }
    if (argc == 4 && action == "--audit") {
      auto *report = json_object_new_object();
      auto *scenes = json_object_new_array();
      auto *failures = json_object_new_object();
      std::map<std::string, size_t> counts;
      size_t passed = 0;
      Amagami::Session session(sharedSource);
      for (const auto &name : source.scenarios()) {
        auto *record = json_object_new_object();
        json_object_object_add(record, "scenario",
                               json_object_new_string(name.c_str()));
        size_t steps = 0, choices = 0;
        std::string error;
        try {
          session.start(name);
          while (!session.ended() && steps++ < 10000) {
            if (session.waiting() || session.vmPaused())
              session.tick(1.0 / 60.0);
            if (session.choices().empty())
              session.advance();
            else {
              ++choices;
              session.choose(0);
            }
          }
          if (!session.ended())
            error = "Scenario exceeded 5000 advances";
        } catch (const std::exception &e) {
          error = e.what();
        }
        if (error.empty())
          ++passed;
        else
          ++counts[error];
        json_object_object_add(
            record, "status",
            json_object_new_string(error.empty() ? "ended" : "blocked"));
        json_object_object_add(record, "pages",
                               json_object_new_int64(session.history().size()));
        json_object_object_add(record, "choices",
                               json_object_new_int64(choices));
        json_object_object_add(
            record, "location",
            json_object_new_string(session.location().c_str()));
        if (!error.empty())
          json_object_object_add(record, "error",
                                 json_object_new_string(error.c_str()));
        json_object_array_add(scenes, record);
      }
      for (const auto &[error, count] : counts)
        json_object_object_add(failures, error.c_str(),
                               json_object_new_int64(count));
      json_object_object_add(
          report, "mode",
          json_object_new_string("standalone-headless-first-answer"));
      json_object_object_add(report, "total",
                             json_object_new_int64(source.scenarios().size()));
      json_object_object_add(report, "ended", json_object_new_int64(passed));
      json_object_object_add(report, "scenes", scenes);
      json_object_object_add(report, "failures", failures);
      std::ofstream output(argv[3]);
      output << json_object_to_json_string_ext(report, JSON_C_TO_STRING_PRETTY)
             << '\n';
      json_object_put(report);
      if (!output)
        throw std::runtime_error("Cannot write audit report");
      std::cout
          << "Standalone scenarios ended " << passed << "/"
          << source.scenarios().size()
          << "; this is not a campaign or audiovisual equivalence check\n";
      return 0;
    }
    if (argc >= 4 && action == "--play") {
      Amagami::Session session(sharedSource);
      session.start(argv[3]);
      size_t steps = 0;
      size_t choice = argc >= 5 ? number(argv[4]) : 0;
      while (!session.ended() && steps++ < 10000) {
        if (!session.choices().empty()) {
          std::cout << "CHOICE " << session.location() << '\n';
          for (const auto &a : session.choices())
            std::cout << a.value << ": " << a.text << '\n';
          session.choose(std::min(choice, session.choices().size() - 1));
        } else {
          std::cout << session.dialogue().speaker << ": "
                    << session.dialogue().text << '\n';
          // A VM pausada por sleep (Object.sleep rende em qualquer modo) ou
          // por shouldYield (input sintetico) precisa que o tempo avance para
          // prosseguir.
          if (session.waiting() || session.vmPaused())
            session.tick(1.0 / 60.0);
          session.advance();
        }
      }
      std::cout << "END steps=" << steps
                << " dialogue=" << session.history().size() << '\n';
      return session.ended() ? 0 : 1;
    }

    if (argc == 3 && action == "--classes") {
      for (size_t i = 0; i < source.classes().size(); ++i)
        std::cout << source.classes()[i].name << '\n';
      return 0;
    }
    if (argc == 4 && action == "--script") {
      dump(source.script(source.scenarioIndex(argv[3])));
      return 0;
    }
    if (argc == 4 && action == "--class") {
      for (size_t i = 0; i < source.classes().size(); ++i)
        if (source.classes()[i].name == argv[3]) {
          dump(source.systemClass(i));
          return 0;
        }
      throw std::runtime_error("Unknown class");
    }
    if ((argc == 5 || argc == 6) && action == "--bytecode") {
      const std::string className = argv[3], methodName = argv[4];
      auto tryDisasm = [&](const Amagami::Script &s) {
        for (const auto &m : s.methods)
          if (m.name == methodName &&
              (argc < 6 || m.arguments == number(argv[5]))) {
            disassemble(s, m);
            return true;
          }
        return false;
      };
      for (size_t i = 0; i < source.classes().size(); ++i)
        if (source.classes()[i].name == className &&
            tryDisasm(source.systemClass(i)))
          return 0;
      for (size_t j = 0; j < source.scenarios().size(); ++j) {
        const auto s = source.script(j);
        if (s.name == className && tryDisasm(s))
          return 0;
      }
      throw std::runtime_error("Unknown class or method");
    }
    if (argc == 6 && (action == "--extract" || action == "--png")) {
      auto b = source.read(argv[3], number(argv[4]));
      // Do not overwrite an existing export.
      if (std::filesystem::exists(argv[5]))
        throw std::runtime_error("Output already exists");
      if (action == "--extract") {
        std::ofstream f(argv[5], std::ios::binary);
        f.write(reinterpret_cast<const char *>(b.data()), b.size());
        if (!f)
          throw std::runtime_error("Cannot write output");
      } else {
        Kotonoha::Ps2Image image;
        if (!Kotonoha::DecodePs2Tim2(b.data(), b.size(), &image))
          throw std::runtime_error("Entry is not supported TIM2");
        auto *surface = SDL_CreateSurfaceFrom(
            image.width, image.height, SDL_PIXELFORMAT_RGBA32,
            image.rgba.data(), image.width * 4);
        if (!surface)
          throw std::runtime_error(SDL_GetError());
        bool ok = SDL_SavePNG(surface, argv[5]);
        SDL_DestroySurface(surface);
        if (!ok)
          throw std::runtime_error(SDL_GetError());
      }
      return 0;
    }
    throw std::invalid_argument("Invalid arguments; use --help");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
