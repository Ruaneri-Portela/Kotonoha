#include <Meo/TimelineParser.hpp>
#include <filesystem>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace Meo {

// A very simple, minimal XML parser for MEO timelines.
// Not for general purpose use, just for the specific expected MEO format.
Kotonoha::TimelineScript TimelineParser::parse(const std::string &path) {
  Kotonoha::Filesystem::InputFile file(path);
  if (!file) {
    throw std::runtime_error("Cannot open MEO timeline: " + path);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  Kotonoha::TimelineScript script;

  // 1. Parse duration
  std::regex duration_regex("<timeline\\s+[^>]*duration=\"([^\"]+)\"");
  std::smatch match;
  if (std::regex_search(content, match, duration_regex)) {
    script.duration = std::stod(match[1]);
  } else {
    // If not found, maybe it's in a different attribute? Default to 0 and let
    // validation fail?
    script.duration = 0;
  }

  // 2. Parse cues
  // <cue at="0.0" type="video" value="video.mp4" channelName="video" />
  // <cue at="5.0" type="subtitle" value="Hello" />
  // <cue at="10.0" type="dialogue" speaker="Person" value="Hi" />
  // <cue at="15.0" type="choice" policy="pause" count="2">
  //    <option label="Yes" target="1" />
  //    <option label="No" target="2" />
  // </cue>

  // This regex is a bit complex because of the optional attributes.
  // Let's use a simpler approach: find all <cue ...> tags.

  std::regex cue_regex("<cue\\s+([^>]+)(/?)>");
  auto cue_begin =
      std::sregex_iterator(content.begin(), content.end(), cue_regex);
  auto cue_end = std::sregex_iterator();

  for (std::sregex_iterator i = cue_begin; i != cue_end; ++i) {
    std::smatch cue_match = *i;
    std::string attr_string = cue_match[1];

    Kotonoha::TimelineCue cue;

    // Helper to get attribute value
    auto get_attr = [&](const std::string &attr_name) -> std::string {
      std::regex attr_regex("\\b" + attr_name + "\\s*=\\s*\"([^\"]*)\"");
      std::smatch attr_match;
      if (std::regex_search(attr_string, attr_match, attr_regex)) {
        return std::string(attr_match[1]);
      }
      return "";
    };

    // Parse "at"
    std::string at_str = get_attr("at");
    if (!at_str.empty())
      cue.at = std::stod(at_str);

    // Parse "duration"
    std::string dur_str = get_attr("duration");
    if (!dur_str.empty())
      cue.duration = std::stod(dur_str);

    // Parse "type"
    std::string type_str = get_attr("type");
    if (type_str == "video")
      cue.type = Kotonoha::CueType::Video;
    else if (type_str == "audio")
      cue.type = Kotonoha::CueType::Audio;
    else if (type_str == "subtitle")
      cue.type = Kotonoha::CueType::Subtitle;
    else if (type_str == "dialogue")
      cue.type = Kotonoha::CueType::Dialogue;
    else if (type_str == "choice")
      cue.type = Kotonoha::CueType::Choice;

    // Parse "value"
    cue.value = get_attr("value");

    // Parse "speaker" (for dialogue)
    cue.speaker = get_attr("speaker");

    // Parse "channelName" (for audio/video)
    cue.channelName = get_attr("channelName");

    // Parse "policy" (for choice)
    std::string policy_str = get_attr("policy");
    if (type_str == "choice") {
      if (policy_str == "pause")
        cue.timeout = Kotonoha::ChoiceTimeout::Pause;
      else if (policy_str == "continue")
        cue.timeout = Kotonoha::ChoiceTimeout::Continue;
      else if (policy_str == "select") {
        cue.timeout = Kotonoha::ChoiceTimeout::Select;
        cue.timeoutTargetNodeId = std::stoi(get_attr("target"));
      }

      // Parse options for choice
      // We need to look ahead in the content for the children of this cue
      // This is tricky with regex alone. Let's find the end of this tag.
      // If it's not a self-closing tag, it might have children.

      // Check if it's NOT self-closing
      if (cue_match[2].str() != "/") {
        // It's an open tag, look for the matching </cue>
        size_t end_pos =
            content.find("</cue>", cue_match.position() + cue_match.length());
        if (end_pos != std::string::npos) {
          std::string children_content = content.substr(
              cue_match.position() + cue_match.length(),
              end_pos - (cue_match.position() + cue_match.length()));

          std::regex option_regex(
              "<option\\s+label=\"([^\"]*)\"\\s+target=\"([^\"]*)\"\\s*/>");
          auto opt_begin = std::sregex_iterator(
              children_content.begin(), children_content.end(), option_regex);
          auto opt_end = std::sregex_iterator();

          for (std::sregex_iterator j = opt_begin; j != opt_end; ++j) {
            std::smatch opt_match = *j;
            Kotonoha::TimedChoiceOption option;
            option.label = opt_match[1];
            option.targetNodeId = std::stoi(opt_match[2]);
            cue.choices.push_back(std::move(option));
          }
        }
      }
    }

    // Resolve relative paths for assets
    if (cue.type == Kotonoha::CueType::Video ||
        cue.type == Kotonoha::CueType::Audio) {
      if (!cue.value.empty()) {
        cue.value = (std::filesystem::path(path).parent_path() / cue.value)
                        .lexically_normal()
                        .string();
      }
    }

    script.cues.push_back(std::move(cue));
  }

  script.validate();
  return script;
}

} // namespace Meo
