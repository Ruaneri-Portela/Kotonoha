#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/script/KtrfScript.hpp>
#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <typename F> void rejects(F operation) {
  bool failed = false;
  try {
    operation();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, "Invalid KTRF must be rejected");
}
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {(std::istreambuf_iterator<char>(input)), {}};
}
} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("kotonoha-ktrf-" + std::to_string(SDL_GetPerformanceCounter()));
  std::filesystem::create_directory(root);
  try {
    Kotonoha::KtrfScript original;
    original.entryNodeId = 4;
    Kotonoha::ScriptNode dialogue;
    dialogue.id = 4;
    dialogue.name = "Opening";
    dialogue.speaker = "Test";
    dialogue.text = "Original dialogue";
    dialogue.backgroundAsset = "background/test.png";
    dialogue.nextNodeId = 7;
    Kotonoha::ScriptNode choice;
    choice.id = 7;
    choice.type = Kotonoha::NodeType::Choice;
    choice.choiceQuestion = "Continue?";
    choice.choices = {{"Yes", 10}, {"No", 12}};
    choice.duration = 2;
    choice.timeout = Kotonoha::ChoiceTimeout::Select;
    choice.timeoutTargetNodeId = 12;
    Kotonoha::ScriptNode timed;
    timed.id = 10;
    timed.type = Kotonoha::NodeType::Timeline;
    timed.timeline = {3, {{0, 3, Kotonoha::CueType::Subtitle, "Subtitle"}}};
    timed.nextNodeId = 12;
    Kotonoha::ScriptNode end;
    end.id = 12;
    end.type = Kotonoha::NodeType::End;
    original.nodes = {end, timed, choice, dialogue};
    const auto binary = (root / "story.ktnroute").string();
    const auto json = (root / "story.json").string();
    original.dump(binary);
    original.dump(json);
    require(read(binary).compare(0, 4, "KTRF") == 0,
            "Original KTRF container magic");
    Kotonoha_KtrfDocument container{};
    Kotonoha_KtrfError error{};
    require(Kotonoha_KtrfLoadFile(binary.c_str(), &container, &error),
            "The original Kotonoha parser accepts dumped KTRF");
    require(Kotonoha_KtrfFindSection(&container, "VNSC") != nullptr,
            "VN profile section exists");
    std::vector<Kotonoha_KtrfSection> sections(
        container.sections, container.sections + container.section_count);
    for (auto &section : sections)
      if (std::string(section.type, 4) == "VNSC")
        ++section.item_count;
    Kotonoha_KtrfDocument mismatched{};
    require(Kotonoha_KtrfBuild(sections.data(), sections.size(), &mismatched,
                               &error),
            "Container integrity can be valid with invalid VN metadata");
    const auto mismatch = root / "mismatched.ktnroute";
    std::ofstream(mismatch, std::ios::binary)
        .write(reinterpret_cast<const char *>(mismatched.bytes),
               mismatched.size);
    Kotonoha_KtrfClean(&mismatched);
    rejects([&] { Kotonoha::KtrfScript::load(mismatch.string()); });
    Kotonoha_KtrfClean(&container);
    auto loaded = Kotonoha::KtrfScript::load(binary);
    auto editable = Kotonoha::KtrfScript::load(json);
    require(loaded.nodes.size() == 4 && loaded.entryNodeId == 4,
            "Node graph round trips");
    require(editable.nodes[0].text == "Original dialogue" &&
                editable.nodes[0].backgroundAsset == "background/test.png",
            "Node text and asset references round trip");
    require(loaded.nodes[1].choices[0].targetNodeId == 10 &&
                loaded.nodes[1].timeoutTargetNodeId == 12 &&
                loaded.nodes[2].timeline.cues[0].value == "Subtitle",
            "Choices, deadlines and timeline cues round trip");
    loaded.dump((root / "deterministic.ktnroute").string());
    require(read(binary) == read(root / "deterministic.ktnroute"),
            "Dumped KTRF is deterministic");
    editable.nodes[0].text = "Customized dialogue";
    editable.nodes[0].nextNodeId = 12;
    editable.dump(json);
    require(Kotonoha::KtrfScript::load(json).nodes[0].text ==
                "Customized dialogue",
            "Custom nodes can be loaded");
    editable.nodes[0].nextNodeId = 999;
    rejects([&] { editable.dump(json); });
    auto corrupt = read(binary);
    corrupt.back() ^= 1;
    std::ofstream(root / "corrupt.ktnroute", std::ios::binary)
        .write(corrupt.data(), corrupt.size());
    rejects([&] {
      Kotonoha::KtrfScript::load((root / "corrupt.ktnroute").string());
    });
    original.nodes[0].id = 4;
    rejects([&] { original.dump(binary); });

    int calls = 0;
    Kotonoha::ScriptNode action{0, Kotonoha::NodeType::Action, [&] { ++calls; },
                                1};
    end.id = 1;
    Kotonoha::KtrfScript native{
        {action, end}, Kotonoha::ScriptMode::Standard, 0};
    native.dump(binary);
    rejects([&] { Kotonoha::KtrfScript::load(binary); });
    auto rebound = Kotonoha::KtrfScript::load(binary, native.nodes);
    rebound.nodes[0].actionCallback();
    require(calls == 1,
            "Native callback is rebound, never loaded as executable code");
    std::filesystem::remove_all(root);
    std::cout << "KTRF script tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::filesystem::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
