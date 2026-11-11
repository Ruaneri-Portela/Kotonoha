// Validate actual user-supplied archives; never writes the installation.
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/parsers/Documents.hpp>
#include <SchoolDays/SchoolDaysAssets.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace Kotonoha;
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("Expected School Days HQ directory");
    const auto packs = std::filesystem::path(argv[1]) / "Packs";
    size_t archives = 0, scripts = 0, events = 0, bytesRead = 0;
    std::map<std::string, size_t> unknown;
    std::set<std::string> scriptNames;
    for (const auto &spec : SchoolDaysRetailGpkSpecs()) {
      SchoolDaysGpkArchive archive;
      std::string error;
      if (!archive.Open(packs / spec.filename, &error))
        throw std::runtime_error(error);
      ++archives;
      const auto &entries = archive.Index().Entries();
      for (size_t i = 0; i < entries.size(); ++i) {
        std::string name(entries[i].filename.begin(),
                         entries[i].filename.end());
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".ors")
          continue;
        std::vector<uint8_t> data;
        if (!archive.ReadEntry(i, &data, &error))
          throw std::runtime_error(name + ": " + error);
        OrsDocument parsed{Kotonoha_OrsParserBytes(data.data(), data.size())};
        if (!parsed.get().data || !parsed.get().size)
          throw std::runtime_error("Empty parsed retail script: " + name);
        scriptNames.insert(lower);
        if (scripts < 3)
          std::cout << "SCRIPT " << name << "\n";
        ++scripts;
        bytesRead += data.size();
        for (auto *event = parsed.get().data; event; event = event->next) {
          ++events;
          if (event->command == UNKNOWN)
            ++unknown[name];
        }
      }
    }
    if (archives != 29 || !scripts || !events)
      throw std::runtime_error("Incomplete retail census");
    std::cout << archives << " archives, " << scripts << " scripts, " << events
              << " events, " << bytesRead << " script bytes\n";
    for (const auto &[script, count] : unknown)
      std::cout << "UNKNOWN " << script << ": " << count << '\n';
    KtrfDocument route;
    Kotonoha_KtrfError error{};
    if (!Kotonoha_KtrfLoadFile(argv[2], &route.get(), &error))
      throw std::runtime_error(error.message);
    const auto *resources = Kotonoha_KtrfFindSection(&route.get(), "RSRC");
    if (!resources)
      throw std::runtime_error("Missing routing resources");
    size_t matched = 0;
    for (uint32_t i = 0; i < resources->item_count; ++i) {
      Kotonoha_KtrfResource resource{};
      const char *text = nullptr;
      size_t size = 0;
      if (!Kotonoha_KtrfGetResource(&route.get(), i, &resource, &error) ||
          !Kotonoha_KtrfGetString(&route.get(), resource.value_str, &text,
                                  &size, &error))
        throw std::runtime_error(error.message);
      std::string key = "english/" + std::string(text, size) + ".eng.ors";
      std::transform(key.begin(), key.end(), key.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (scriptNames.count(key))
        ++matched;
      else
        std::cout << "ROUTING_RESOURCE_MISSING " << std::string(text, size)
                  << '\n';
    }
    std::cout << matched << "/" << resources->item_count
              << " routing resources have retail ORS files\n";
    // The installed retail script has an embedded space in this one path.
    // Exercise the actual game resolver and virtual read against Voice03.GPK.
    Kotonoha::Providers::SchoolDaysAssets assets(argv[1]);
    const auto recovered = assets.readAsset(
        assets.resolve("Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG"));
    if (recovered.size() < 4 ||
        !std::equal(recovered.begin(), recovered.begin() + 4, "OggS"))
      throw std::runtime_error("Retail voice typo did not recover a valid OGG");
    std::cout << "Recovered retail ORS voice typo from Voice03.GPK\n";
    // Unknown native commands are audited explicitly; parsing is not parity.
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
