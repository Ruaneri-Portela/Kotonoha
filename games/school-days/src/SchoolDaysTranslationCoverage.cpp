#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include "SchoolDaysAssets.hpp"
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/routing/SchoolDaysGpkLanguageProfile.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <system_error>

namespace Kotonoha {
namespace {
const std::string *FindValue(const Translation &dictionary, int node,
                             const std::string &original) {
  const auto &sections = dictionary.getSections();
  for (const auto &sectionName : {std::to_string(node), std::string("nodo")}) {
    if (const auto section = sections.find(sectionName);
        section != sections.end()) {
      if (const auto item = section->second.find(original);
          item != section->second.end())
        return &item->second;
    }
  }
  return nullptr;
}

const std::string *FindDetailedValue(const Translation &dictionary, int node,
                                     std::size_t ordinal, std::size_t slot,
                                     const std::string &original,
                                     bool *contextual = nullptr) {
  if (contextual) *contextual = false;
  const auto &sections = dictionary.getSections();
  if (const auto section = sections.find(std::to_string(node));
      section != sections.end()) {
    if (const auto item = section->second.find(
            SchoolDaysTranslationGate::ContextKey(ordinal, slot));
        item != section->second.end()) {
      if (contextual) *contextual = true;
      return &item->second;
    }
  }
  return FindValue(dictionary, node, original);
}

const std::string *FindDlcValue(const Translation &dictionary, int node,
                                std::size_t ordinal, std::size_t slot,
                                const std::string &original,
                                bool *contextual) {
  if (contextual) *contextual = false;
  const auto &sections = dictionary.getSections();
  const auto section = sections.find(std::to_string(node));
  if (section == sections.end()) return nullptr;
  if (const auto item = section->second.find(
          SchoolDaysTranslationGate::ContextKey(ordinal, slot));
      item != section->second.end()) {
    if (contextual) *contextual = true;
    return &item->second;
  }
  if (const auto item = section->second.find(original);
      item != section->second.end())
    return &item->second;
  return nullptr;
}

const std::string *FindLayeredValue(
    const Translation &base, const Translation &dlc, int node,
    std::size_t ordinal, std::size_t slot, const std::string &original,
    bool lxh, bool *contextual = nullptr, bool *fromDlc = nullptr) {
  if (fromDlc) *fromDlc = false;
  if (lxh) {
    if (const auto *value = FindDlcValue(
            dlc, node, ordinal, slot, original, contextual)) {
      if (fromDlc) *fromDlc = true;
      return value;
    }
  }
  return FindDetailedValue(base, node, ordinal, slot, original, contextual);
}

std::string NormalizedPath(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (unsigned char ch : value)
    normalized.push_back(ch == '\\' ? '/' :
        static_cast<char>(std::tolower(ch)));
  return normalized;
}

const std::string *FindReviewedValue(const Translation &reviewed, int node,
                                     std::size_t ordinal, std::size_t slot,
                                     const std::string &original) {
  // A generic nodo identity approval could accidentally certify unrelated
  // English text in other nodes. Reviews are always scoped to one KTRF NODE.
  const auto &sections = reviewed.getSections();
  const auto section = sections.find(std::to_string(node));
  if (section == sections.end()) return nullptr;
  if (const auto contextual = section->second.find(
          SchoolDaysTranslationGate::ContextKey(ordinal, slot));
      contextual != section->second.end())
    return &contextual->second;
  if (const auto literal = section->second.find(original);
      literal != section->second.end())
    return &literal->second;
  return nullptr;
}

bool ReviewedReason(const std::string *value) {
  if (!value) return false;
  std::string reason;
  reason.reserve(value->size());
  for (unsigned char ch : *value)
    if (!std::isspace(ch)) reason.push_back(static_cast<char>(std::tolower(ch)));
  // Only independently reviewable, source-identity reasons are allowed.
  // Unknown/free-form labels (including "looks_english") fail closed.
  static const std::set<std::string> approved = {
      "proper_name", "punctuation", "interjection", "shared_spanish_word",
      "onomatopoeia", "spanish_original_lxh", "numeric_format",
      "genuinely_bilingual_term", "honorific", "emoticon", "sound_effect",
      "brand_name", "location_name", "nickname", "foreign_name"};
  return approved.find(reason) != approved.end();
}

std::vector<std::filesystem::path> LocaleJsonDirectories(
    const std::filesystem::path &baseFile) {
  namespace fs = std::filesystem;
  const auto folder = baseFile.parent_path().empty()
                          ? fs::path(".") : baseFile.parent_path();
  // A previously selected flat locale moves to translations/jsons. Probe
  // the organized directory FIRST, then legacy flat, no matter where the
  // saved base JSON still resides. For a custom external JSON, only its
  // adjacent sidecar may be automatically matched.
  if (folder.filename() == "translations")
    return {folder / "jsons", folder};
  if (folder.filename() == "jsons" &&
      folder.parent_path().filename() == "translations")
    return {folder, folder.parent_path()};
  return {folder};
}

std::optional<std::filesystem::path> FindExactLocaleJson(
    const std::vector<std::filesystem::path> &folders,
    const std::vector<std::string> &names) {
  // Preference is by folder (organized first), then exact filename casing.
  // No case-insensitive choice of an unrelated locale is ever permitted.
  for (const auto &folder : folders)
    for (const auto &name : names)
      if (auto path = SchoolDaysGpkLanguageProfile::detail::ExactFile(
              folder, name))
        return path;
  return std::nullopt;
}
} // namespace

std::string SchoolDaysTranslationCoverage::summary() const {
  return "scenes=" + std::to_string(scenes) +
         " entries=" + std::to_string(checked) +
         " print_text=" + std::to_string(printTextSlots) +
         " choices=" + std::to_string(choiceSlots) +
         " translated_keys=" + std::to_string(found) +
         " missing=" + std::to_string(missing) +
         " blank=" + std::to_string(blank) +
         " identity_needs_review=" + std::to_string(identical) +
         " reviewed_identity=" + std::to_string(reviewedIdentical) +
         " supplied_identity=" + std::to_string(suppliedIdentical) +
         " contextual=" + std::to_string(contextual) +
         " invalid_dlc_keys=" + std::to_string(invalidDlcKeys) +
         " overridden_base_dlc_slots=" + std::to_string(overriddenBaseSlots) +
         " resolved_collisions=" + std::to_string(resolvedCollisions) +
         " affected_nodes=" + std::to_string(incompleteNodes.size()) +
         (issues.empty() ? std::string{} :
          " first_issue=NODE " + std::to_string(issues.front().node) + " " +
          SchoolDaysTranslationGate::ContextKey(issues.front().eventOrdinal,
                                                 issues.front().slot));
}

std::string SchoolDaysTranslationGate::ContextKey(
    std::size_t eventOrdinal, std::size_t slot) {
  return "@ors:" + std::to_string(eventOrdinal) + ":" + std::to_string(slot);
}

std::size_t SchoolDaysTranslationGate::EventOrdinal(
    const Kotonoha_orsData &script, const Kotonoha_orsEvent *target) {
  std::size_t ordinal = 0;
  for (const auto *event = script.data; event; event = event->next, ++ordinal)
    if (event == target) return ordinal;
  throw std::runtime_error("School Days locale event is absent from the active ORS");
}

bool SchoolDaysTranslationGate::LxhEnding(const std::string &sceneKey) {
  const auto key = NormalizedPath(sceneKey);
  for (const auto ending : SchoolDaysLxhContent::Endings)
    if (key == "05/" + NormalizedPath(ending)) return true;
  return false;
}

std::optional<std::size_t> SchoolDaysTranslationGate::LxhFirstEvent(
    const Kotonoha_orsData &script, const std::string &sceneKey) {
  if (!LxhEnding(sceneKey)) return std::nullopt;
  const auto key = NormalizedPath(sceneKey.substr(3));
  const auto moviePath = "lxh/media/" + key + "-lxh";
  std::size_t ordinal = 0, creditOrdinal = 0, movieOrdinal = 0;
  uint32_t creditsEnd = 0, movieStart = 0;
  int creditCount = 0, movieCount = 0, nextCount = 0;
  for (const auto *event = script.data; event;
       event = event->next, ++ordinal) {
    if (event->command == END_ROLL && event->data.path_end) {
      ++creditCount;
      creditOrdinal = ordinal;
      creditsEnd = static_cast<std::uint32_t>(event->endTick);
    } else if (event->command == PLAY_MOVIE && event->data.play_movie &&
               event->data.play_movie->path &&
               NormalizedPath(event->data.play_movie->path).find(moviePath) !=
                   std::string::npos) {
      ++movieCount;
      movieOrdinal = ordinal;
      movieStart = static_cast<std::uint32_t>(event->startTick);
    } else if (event->command == Next) {
      ++nextCount;
    }
  }
  // The marker must authenticate a graft after the commercial credits, not
  // just a scene name or a count of 273 strings supplied by a translator.
  if (creditCount != 1 || movieCount != 1 || nextCount != 1 ||
      movieOrdinal <= creditOrdinal || movieStart < creditsEnd)
    return std::nullopt;
  return movieOrdinal;
}

SchoolDaysTranslationGate::DetailedSources SchoolDaysTranslationGate::CollectDetailed(
    const SchoolDays::Assets &assets, const std::string &route) {
  SchoolDaysKtrfSession session;
  Kotonoha_KtrfError error{};
  const std::string root = assets.orsRoot().string();
  if (!session.Open(route.c_str(), root.c_str(), &error))
    throw std::runtime_error("Cannot audit School Days translation route: " +
                             std::string(error.message));
  const auto *document = session.Adapter()->document;
  const auto *nodes = Kotonoha_KtrfFindSection(document, "NODE");
  if (!nodes) throw std::runtime_error("No KTRF NODE table for locale audit");

  DetailedSources sources;
  sources.reserve(32768u);
  for (uint32_t index = 0; index < nodes->item_count; ++index) {
    Kotonoha_KtrfNodeView node{};
    if (!Kotonoha_KtrfGetNodeView(session.Adapter(), index, &node, &error))
      throw std::runtime_error("Cannot inspect translation NODE " +
                               std::to_string(index) + ": " + error.message);
    if (node.kind != KOTONOHA_KTRF_NODE_SCENE) continue;
    SchoolDaysKtrfScene scene;
    if (!session.ResolveSceneNode(index, &scene, &error) ||
        scene.orsPath.empty())
      throw std::runtime_error("Cannot resolve translated scene NODE " +
                               std::to_string(index) + ": " + error.message);

    const auto bytes = assets.read(scene.orsPath);
    if (bytes.empty())
      throw std::runtime_error("Cannot audit ORS for NODE " +
                               std::to_string(index));
    OrsDocument ors(Kotonoha_OrsParserBytes(bytes.data(), bytes.size()));
    if (!ors.get().data || !ors.get().size)
      throw std::runtime_error("Cannot parse translated scene NODE " +
                               std::to_string(index));
    const auto lxhStart = LxhFirstEvent(ors.get(), scene.sceneKey);
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    if (assets.usesPackedResources() && assets.lxhMounted() &&
        LxhEnding(scene.sceneKey) && !lxhStart)
      throw std::runtime_error("Integrated LxH ORS marker is missing at NODE " +
                               std::to_string(index));
#endif
    std::size_t ordinal = 0;
    for (const auto *event = ors.get().data; event;
         event = event->next, ++ordinal) {
      if (event->command == PRINT_TEXT && event->data.print_text) {
        const auto *text = event->data.print_text->text;
        if (text && *text)
          sources.push_back({static_cast<int>(index), ordinal, 0, text,
                             SourceSlot::Kind::PrintText,
                             lxhStart && ordinal >= *lxhStart});
      } else if (event->command == SetSELECT && event->data.set_select) {
        const auto *options = event->data.set_select;
        for (uint64_t choice = 0; choice < options->size; ++choice) {
          const char *text = options->options[choice];
          if (text && *text)
            sources.push_back({static_cast<int>(index), ordinal,
                               static_cast<std::size_t>(choice), text,
                               SourceSlot::Kind::Choice,
                               lxhStart && ordinal >= *lxhStart});
        }
      }
    }
  }
  if (sources.empty())
    throw std::runtime_error("KTRF translation audit found no dialogue or choices");
  return sources;
}

SchoolDaysTranslationGate::SourceTexts SchoolDaysTranslationGate::Collect(
    const SchoolDays::Assets &assets, const std::string &route) {
  SourceTexts sources;
  for (const auto &entry : CollectDetailed(assets, route))
    sources[entry.node].push_back(entry.original);
  return sources;
}

SchoolDaysTranslationCoverage SchoolDaysTranslationGate::Check(
    const SourceTexts &sources, const Translation &dictionary) {
  SchoolDaysTranslationCoverage status;
  for (const auto &[node, strings] : sources) {
    if (strings.empty()) continue;
    ++status.scenes;
    bool incomplete = false;
    for (const auto &original : strings) {
      if (original.empty()) continue;
      ++status.checked;
      const std::string *value = FindValue(dictionary, node, original);
      if (!value) {
        ++status.missing;
        incomplete = true;
      } else if (value->empty()) {
        ++status.blank;
        incomplete = true;
      } else {
        ++status.found;
        if (*value == original) ++status.identical;
      }
    }
    if (incomplete) status.incompleteNodes.push_back(node);
  }
  return status;
}

SchoolDaysTranslationCoverage SchoolDaysTranslationGate::CheckDetailed(
    const DetailedSources &sources, const Translation &dictionary,
    const Translation &reviewedIdentities) {
  return CheckLayered(sources, dictionary, reviewedIdentities, {}, {}, false);
}

SchoolDaysTranslationCoverage SchoolDaysTranslationGate::CheckLayered(
    const DetailedSources &sources, const Translation &base,
    const Translation &baseReviews, const Translation &dlc,
    const Translation &dlcReviews, bool nativeHq,
    bool explicitSourceIdentities) {
  using Section = std::unordered_map<std::string, std::string>;
  const auto findSection = [](const Translation &dict,
                              const std::string &name) -> const Section * {
    const auto &sections = dict.getSections();
    const auto it = sections.find(name);
    return it == sections.end() ? nullptr : &it->second;
  };
  const auto lookupInSection =
      [](const Section *section, const Section *fallbackNodo,
         const std::string &contextKey, const std::string &original,
         bool *contextual) -> const std::string * {
    if (contextual) *contextual = false;
    if (section) {
      if (const auto it = section->find(contextKey); it != section->end()) {
        if (contextual) *contextual = true;
        return &it->second;
      }
      if (const auto it = section->find(original); it != section->end())
        return &it->second;
    }
    if (fallbackNodo) {
      if (const auto it = fallbackNodo->find(original);
          it != fallbackNodo->end())
        return &it->second;
    }
    return nullptr;
  };

  SchoolDaysTranslationCoverage status;
  std::unordered_set<int> visited;
  visited.reserve(2048u);
  std::unordered_set<int> incomplete;
  std::set<std::pair<std::string, std::string>> permittedDlcKeys;
  struct VariantTrack {
    std::string_view firstValue;
    bool collided = false;
  };
  std::unordered_map<int, std::unordered_map<std::string_view, VariantTrack>>
      variants;
  variants.reserve(2048u);

  const Section *baseNodo = findSection(base, "nodo");
  int cachedNode = -1;
  bool hasCachedNode = false;
  std::string cachedNodeKey;
  const Section *baseSection = nullptr;
  const Section *baseReviewSection = nullptr;
  const Section *dlcSection = nullptr;
  const Section *dlcReviewSection = nullptr;
  std::unordered_map<std::string_view, VariantTrack> *nodeVariants = nullptr;

  for (const auto &entry : sources) {
    if (entry.original.empty()) continue;
    if (!hasCachedNode || entry.node != cachedNode) {
      cachedNode = entry.node;
      hasCachedNode = true;
      cachedNodeKey = std::to_string(entry.node);
      baseSection = findSection(base, cachedNodeKey);
      baseReviewSection = findSection(baseReviews, cachedNodeKey);
      dlcSection = findSection(dlc, cachedNodeKey);
      dlcReviewSection = findSection(dlcReviews, cachedNodeKey);
      nodeVariants = &variants[entry.node];
    }
    const std::string contextKey = ContextKey(entry.eventOrdinal, entry.slot);
    if (entry.lxh) {
      permittedDlcKeys.emplace(cachedNodeKey, entry.original);
      permittedDlcKeys.emplace(cachedNodeKey, contextKey);
    }
    if (nativeHq && !entry.lxh) continue;
    visited.insert(entry.node);
    ++status.checked;
    if (entry.kind == SourceSlot::Kind::Choice) ++status.choiceSlots;
    else ++status.printTextSlots;
    bool contextual = false;
    bool fromDlc = false;
    const std::string *value = nullptr;
    if (entry.lxh && dlcSection) {
      value = lookupInSection(dlcSection, nullptr, contextKey, entry.original,
                              &contextual);
      if (value) fromDlc = true;
    }
    if (!value) {
      value = lookupInSection(baseSection, baseNodo, contextKey, entry.original,
                              &contextual);
    }
    if (entry.lxh && fromDlc && value && !value->empty()) {
      const auto *existing = lookupInSection(baseSection, baseNodo, contextKey,
                                             entry.original, nullptr);
      if (existing && !existing->empty() && *existing != *value)
        ++status.overriddenBaseSlots;
    }
    if (contextual) ++status.contextual;
    if (!value) {
      ++status.missing;
      incomplete.insert(entry.node);
      status.issues.push_back({entry.node, entry.eventOrdinal, entry.slot,
                               SchoolDaysTranslationIssue::Kind::Missing});
    } else if (value->empty()) {
      ++status.blank;
      incomplete.insert(entry.node);
      status.issues.push_back({entry.node, entry.eventOrdinal, entry.slot,
                               SchoolDaysTranslationIssue::Kind::Blank});
    } else {
      ++status.found;
      auto [vit, inserted] = nodeVariants->emplace(
          std::string_view(entry.original),
          VariantTrack{std::string_view(*value), false});
      if (!inserted && !vit->second.collided &&
          vit->second.firstValue != std::string_view(*value)) {
        vit->second.collided = true;
        ++status.resolvedCollisions;
      }
      if (*value == entry.original) {
        const auto *reason = lookupInSection(
            fromDlc ? dlcReviewSection : baseReviewSection, nullptr, contextKey,
            entry.original, nullptr);
        if (ReviewedReason(reason)) {
          ++status.reviewedIdentical;
        } else if (explicitSourceIdentities) {
          ++status.suppliedIdentical;
        } else {
          ++status.identical;
          incomplete.insert(entry.node);
          status.issues.push_back({entry.node, entry.eventOrdinal, entry.slot,
                                   SchoolDaysTranslationIssue::Kind::UnreviewedIdentity});
        }
      }
    }
  }
  status.scenes = visited.size();
  status.incompleteNodes.assign(incomplete.begin(), incomplete.end());
  std::sort(status.incompleteNodes.begin(), status.incompleteNodes.end());
  // Context keys computed against the wrong parser ordering can otherwise
  // remain unused while a literal fallback makes coverage look successful.
  // A DLC sidecar must contain keys only for actual grafted ORS slots.
  for (const auto &[node, entries] : dlc.getSections())
    for (const auto &[key, value] : entries)
      if (!permittedDlcKeys.count({node, key}))
        ++status.invalidDlcKeys;
  return status;
}

Translation SchoolDaysTranslationGate::LoadReviewedIdentities(
    const std::string &localeJsonPath) {
  Translation reviewed;
  if (localeJsonPath.empty()) return reviewed;
  const auto resolved = FileResolver::findAsset(localeJsonPath)
                            .value_or(std::filesystem::path(localeJsonPath));
  const auto name = resolved.stem().string() + "-reviewed-identities.json";
  if (const auto manifest = FindExactLocaleJson(
          LocaleJsonDirectories(resolved), {name}))
    reviewed.load(manifest->string());
  return reviewed;
}

std::string SchoolDaysTranslationGate::LocaleId(
    const std::string &baseJsonPath) {
  if (baseJsonPath.empty()) return "en";
  std::string stem = NormalizedPath(
      std::filesystem::path(baseJsonPath).stem().string());
  if (stem.compare(0, 12, "school-days-") == 0) stem.erase(0, 12);
  if (stem == "spanish" || stem == "es-es" || stem == "es-mx") return "es";
  if (stem == "english" || stem == "en-us" || stem == "en-gb") return "en";
  if (stem.empty() || stem.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
    throw std::runtime_error("School Days locale JSON filename has no stable locale ID");
  return stem;
}

std::string SchoolDaysTranslationGate::DlcJsonPath(
    const std::string &locale, const std::string &baseJsonPath) {
  if (locale.empty() || locale.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
    throw std::runtime_error("Invalid School Days DLC locale ID");
  const std::string name = locale + "-lxh.json";
  if (!baseJsonPath.empty()) {
    const auto base = FileResolver::findAsset(baseJsonPath)
                          .value_or(std::filesystem::path(baseJsonPath));
    // Preserve the selected base JSON's casing for adjacent LxH discovery:
    // zh-CN.json pairs with zh-CN-lxh.json on case-sensitive filesystems.
    // The canonical lower-case locale remains the fallback for prior packs.
    if (const auto adjacent = FindExactLocaleJson(
            LocaleJsonDirectories(base),
            {base.stem().string() + "-lxh.json", name}))
      return adjacent->string();
    const auto folder = base.parent_path();
    const bool installedLocale =
        folder.filename() == "translations" ||
        (folder.filename() == "jsons" &&
         folder.parent_path().filename() == "translations");
    // An explicitly chosen external/custom HQ JSON must not silently inherit
    // an installed game's different-revision LxH dictionary. Only a sidecar
    // adjacent to that custom source is considered a valid automatic pair.
    if (!installedLocale) return {};
  }
  for (const auto &relative : {"translations/jsons/" + name,
                               "translations/" + name}) {
    if (const auto installed = FileResolver::findGameAsset(
            "school-days", relative)) {
      if (auto exact = SchoolDaysGpkLanguageProfile::detail::ExactFile(
              installed->parent_path(), name))
        return exact->string();
      // The game resolver must not reinterpret a case-mismatched locale path.
      throw std::runtime_error("School Days DLC JSON filename mismatch: " +
                               installed->string());
    }
  }
  return {};
}

void SchoolDaysTranslationGate::RequireLxhSidecar(
    const std::string &locale, const std::string &dlcJsonPath) {
  // Spanish V4b already embeds the reviewed 273 LxH slots. All other locales
  // must supply an independently selected DLC dictionary, even when their HQ
  // JSON happens to contain values for those same ORS occurrences.
  if (locale != "es" && dlcJsonPath.empty())
    throw std::runtime_error("School Days LxH requires " + locale +
                             "-lxh.json for locale '" + locale +
                             "'. Select a complete LxH DLC translation.");
}

std::string SchoolDaysTranslationGate::Translate(
    const Translation &dictionary, int node, const std::string &original) {
  // No JSON loaded: the commercial English text is the correct source.
  if (dictionary.getSections().empty() || original.empty()) return original;
  const std::string *value = FindValue(dictionary, node, original);
  if (!value || value->empty())
    throw std::runtime_error(
        "School Days locale incomplete: missing dialogue/choice at NODE " +
        std::to_string(node) + " (English fallback refused)");
  return *value;
}

std::string SchoolDaysTranslationGate::TranslateLayered(
    const Translation &base, const Translation &dlc, int node,
    std::size_t eventOrdinal, std::size_t slot,
    const std::string &original, bool lxh) {
  if (original.empty()) return original;
  if (!lxh && base.getSections().empty()) return original;
  const auto *value = FindLayeredValue(base, dlc, node, eventOrdinal, slot,
                                       original, lxh);
  if (!value || value->empty())
    throw std::runtime_error("School Days locale incomplete at NODE " +
                             std::to_string(node) + " " +
                             ContextKey(eventOrdinal, slot) +
                             (lxh ? " (LxH translation missing)" :
                                    " (English fallback refused)"));
  return *value;
}

std::string SchoolDaysTranslationGate::Translate(
    const Translation &dictionary, int node, std::size_t eventOrdinal,
    std::size_t slot, const std::string &original) {
  if (dictionary.getSections().empty() || original.empty()) return original;
  const auto *value = FindDetailedValue(dictionary, node, eventOrdinal, slot,
                                        original);
  if (!value || value->empty())
    throw std::runtime_error("School Days locale incomplete at NODE " +
                             std::to_string(node) + " " +
                             ContextKey(eventOrdinal, slot) +
                             " (English fallback refused)");
  return *value;
}

} // namespace Kotonoha
