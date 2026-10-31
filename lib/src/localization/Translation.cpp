#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/localization/Translation.hpp>

#include <json-c/json.h>

#include <charconv>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace Kotonoha {
namespace {
struct JSONDeleter {
  void operator()(json_object *value) const { json_object_put(value); }
};
using JSON = std::unique_ptr<json_object, JSONDeleter>;

bool validSection(const std::string &name) {
  if (name == "nodo")
    return true;
  int id = -1;
  const auto parsed =
      std::from_chars(name.data(), name.data() + name.size(), id);
  return parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() &&
         id >= 0 && name == std::to_string(id);
}
} // namespace

void Translation::load(const std::string &path) {
  std::ifstream input(FileResolver::findGameAsset("snow-radish-vacation", path).value_or(path), std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open translation: " + path);
  std::string source((std::istreambuf_iterator<char>(input)), {});
  if (input.bad())
    throw std::runtime_error("Cannot read translation: " + path);
  if (source.compare(0, 3, "\xef\xbb\xbf") == 0)
    source.erase(0, 3);
  if (source.size() >=
      static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::runtime_error("Translation JSON is too large: " + path);
  // json-c stores object keys as C strings; reject NUL before a key is
  // truncated.
  for (std::size_t i = 0; i < source.size(); ++i)
    if (source[i] == '\\') {
      ++i;
      if (source.compare(i, 5, "u0000") == 0)
        throw std::runtime_error(path + ": embedded NUL in translation JSON");
    }

  std::unique_ptr<json_tokener, decltype(&json_tokener_free)> parser(
      json_tokener_new(), json_tokener_free);
  if (!parser)
    throw std::bad_alloc();
  json_tokener_set_flags(parser.get(),
                         JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  JSON root{json_tokener_parse_ex(parser.get(), source.c_str(),
                                  static_cast<int>(source.size() + 1))};
  const auto consumed = json_tokener_get_parse_end(parser.get());
  const auto error = json_tokener_get_error(parser.get());
  if (error != json_tokener_success ||
      (consumed < source.size() &&
       source.find_first_not_of(" \r\n\t", consumed) != std::string::npos))
    throw std::runtime_error(path + ": invalid translation JSON at byte " +
                             std::to_string(consumed) + " (" +
                             json_tokener_error_desc(error) + ")");
  if (!root || !json_object_is_type(root.get(), json_type_object))
    throw std::runtime_error(path +
                             ": expected an object of node dictionaries");

  decltype(sections) parsed;
  json_object_object_foreach(root.get(), node, dictionary) {
    if (!validSection(node) ||
        !json_object_is_type(dictionary, json_type_object))
      throw std::runtime_error(path + ": expected a dictionary at node " +
                               node + " (nonnegative ID or nodo)");
    auto &entries = parsed[node];
    json_object_object_foreach(dictionary, original, translated) {
      if (!*original || !json_object_is_type(translated, json_type_string))
        throw std::runtime_error(path + ": node " + node +
                                 " requires nonempty keys and string values");
      const std::string value(json_object_get_string(translated),
                              json_object_get_string_len(translated));
      if (value.find('\0') != std::string::npos)
        throw std::runtime_error(path + ": embedded NUL in translation value");
      entries.emplace(original, value);
    }
  }
  sections = std::move(parsed);
}

std::string Translation::text(int nodeId, const std::string &original) const {
  for (const auto &name : {std::to_string(nodeId), std::string("nodo")}) {
    const auto section = sections.find(name);
    if (section == sections.end())
      continue;
    const auto entry = section->second.find(original);
    if (entry != section->second.end())
      return entry->second;
  }
  return original;
}

void Translation::dump(const std::string &path,
                       const SourceTexts &texts) const {
  JSON root{json_object_new_object()};
  for (const auto &node : texts) {
    JSON dictionary{json_object_new_object()};
    for (const auto &original : node.second) {
      if (original.empty())
        continue;
      const auto translated = text(node.first, original);
      json_object_object_add(
          dictionary.get(), original.c_str(),
          json_object_new_string_len(translated.data(),
                                     static_cast<int>(translated.size())));
    }
    json_object_object_add(root.get(), std::to_string(node.first).c_str(),
                           dictionary.release());
  }
  const std::string output =
      std::string(json_object_to_json_string_ext(
          root.get(),
          JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE)) +
      "\n";
  writeTextFileAtomically(path, output);
}

} // namespace Kotonoha
