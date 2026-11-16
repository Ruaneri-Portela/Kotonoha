#include "PmailMovieSelection.hpp"
#include <Pmail/Vm.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
struct Host : Pmail::Host {
  void native(Pmail::Vm &, int, const std::vector<Pmail::Arg> &) override {}
  Pmail::ObjectPtr loadByName(const std::string &) override { return {}; }
  Pmail::ObjectPtr loadById(uint32_t) override { return {}; }
};
void checkRng() {
  Host host;
  Pmail::Vm vm(host);
  auto obj = std::make_shared<Pmail::ScriptObject>();
  obj->name = "synthetic-start.obj";
  obj->id = 7;
  obj->strings = {"@"};
  obj->codeOffset = 0;
  obj->codeWords = 3;
  obj->data.resize(12);
  const uint32_t words[] = {
      0x11050000u, // accumulator = eval("@")
      0x10204001u, // var(0x4001) = accumulator
      0x00000000u  // halt
  };
  for (size_t i = 0; i < 3; ++i)
    for (unsigned b = 0; b < 4; ++b)
      obj->data[i * 4 + b] = uint8_t(words[i] >> (8 * b));
  auto verify = [&] {
    vm.registerObject(obj);
    vm.start(obj);
    vm.run(5);
    // 1 * 1103515245 + 12345; high 15 bits = 16838.
    check(vm.var(0x4001) == 16838,
          "PMAIL original PRNG initial value does not match");
  };
  verify();
  vm.reset();
  verify();
}
void checkMovies() {
  using Kotonoha::PmailPort::selectOpeningMovie;
  std::set<std::string> files{"data/movie/PM01L_I3.avi",
                              "data/movie/PM01L_I3.avi.webm",
                              "data/movie/PM01S_I3.avi"};
  auto exists = [&](const std::string &name) {
    return files.find(name) != files.end();
  };
  check(selectOpeningMovie(exists, false) == "data/movie/PM01L_I3.avi",
        "Native selector must recognize PM01L_I3 and prefer original AVI");
  check(selectOpeningMovie(exists, true) == "data/movie/PM01L_I3.avi.webm",
        "Web selector must prefer playable sidecar");
  check(selectOpeningMovie(exists, false, "PM01S_I3") ==
            "data/movie/PM01S_I3.avi",
        "Explicit movie variant ignored");
  check(selectOpeningMovie(exists, false, "PM01S_I5") ==
            "data/movie/PM01L_I3.avi",
        "Missing preferred movie must fall back");
  files.clear();
  files.insert("movie/PM01S_I5.mp4");
  check(selectOpeningMovie(exists, true) == "movie/PM01S_I5.mp4",
        "Alternative movie path ignored");
  files.clear();
  check(selectOpeningMovie(exists, false).empty(),
        "Missing movies must not produce a fabricated path");
}
} // namespace

int main() {
  try {
    checkRng();
    checkMovies();
    std::cout << "PMAIL startup synthetic checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
