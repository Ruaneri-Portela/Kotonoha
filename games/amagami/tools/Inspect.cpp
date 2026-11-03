#include <Amagami/Session.hpp>
#include <Amagami/Source.hpp>
#include <Amagami/Vm.hpp>
#include <Kotonoha/media/Ps2Tim2.hpp>
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
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
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout
          << "AmagamiInspect DISC.iso [--verify | --opcodes | --script NAME | "
             "--class NAME |\n"
             "  --extract ARCHIVE INDEX OUTPUT | --png GRAPH_ARCHIVE "
             "INDEX OUTPUT]\n";
      return argc < 2 ? 1 : 0;
    }
    Amagami::Source source(argv[1]);
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
    if (argc >= 4 && action == "--play") {
      Amagami::Session session(std::make_shared<Amagami::Source>(argv[1]));
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
          session.advance();
        }
      }
      std::cout << "END steps=" << steps
                << " dialogue=" << session.history().size() << '\n';
      return session.ended() ? 0 : 1;
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
