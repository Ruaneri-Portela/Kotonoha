#include <Amagami/Session.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool c, const std::string &why) {
  if (!c)
    throw std::runtime_error(why);
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected disc path");
    auto source = std::make_shared<Amagami::Source>(argv[1]);
    require(source->scenarios().size() == 2308, "All scenarios parsed");
    require(source->classes().size() == 222, "All system classes parsed");
    require(source->find("GRAPH/GRAPH1.ARC", "bg/122_10.tm2") == 723,
            "Native ARC filename hash");
    for (size_t i = 0; i < source->scenarios().size(); ++i)
      require(source->find("SCENARIO.ARC", source->scenarios()[i] + ".scf") ==
                  int(i),
              "Scenario hash round trip");
    require(!source->voice("MIY", 1858).empty(),
            "Nested AFS voice name lookup");
    require(source->voice("XYZ", 9999).empty(), "Missing voice lookup");
    auto tmp = std::filesystem::temp_directory_path() /
               ("amagami-test-" + std::to_string(SDL_GetPerformanceCounter()) +
                ".txt");
    struct Cleanup {
      std::filesystem::path p;
      ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove(p, ec);
      }
    } cleanup{tmp};
    std::vector<std::string> branchTexts;
    for (size_t branch = 0; branch < 3; ++branch) {
      Amagami::Session session(source);
      session.start();
      size_t steps = 0, choices = 0;
      while (!session.ended() && steps++ < 2000) {
        if (!session.choices().empty()) {
          require(session.choices().size() == 3,
                  "Three original prologue answers");
          session.save(tmp.string());
          auto location = session.location();
          Amagami::Session restored(source);
          restored.load(tmp.string());
          require(restored.location() == location &&
                      restored.choices().size() == 3,
                  "Restore pending choice");
          session.choose(branch);
          branchTexts.push_back(session.dialogue().text);
          ++choices;
          session.save(tmp.string());
          restored.load(tmp.string());
          require(restored.dialogue().text == session.dialogue().text &&
                      restored.location() == session.location(),
                  "Restore selected branch");
        } else
          session.advance();
      }
      require(session.ended(), "Prologue reaches original EoS");
      require(choices == 1, "Original prologue choice count");
      require(session.history().size() > 400, "Complete prologue dialogue");
      std::cout << "Branch " << branch << ": " << session.history().size()
                << " dialogue pages, EoS\n";
    }
    require(branchTexts[0] != branchTexts[1] &&
                branchTexts[1] != branchTexts[2],
            "Choices execute distinct original blocks");
    require(Amagami::DisplayText("V1234helloW15／world") == "hello\nworld",
            "Text controls removed");
    require(Amagami::DisplayText("R美也R2みや") == "美也",
            "Ruby base retained");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
