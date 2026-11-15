#pragma once

#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/parsers/Ors.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace SchoolDays { class Assets; }

namespace Kotonoha {

// A non-native School Days language must never silently inherit a missing
// English subtitle/choice from Translation::text's generic fallback. This
// policy is game-specific; other Kotonoha games can still use partial JSONs.
struct SchoolDaysTranslationIssue {
  enum class Kind { Missing, Blank, UnreviewedIdentity };
  int node = -1;
  std::size_t eventOrdinal = 0;
  std::size_t slot = 0;
  Kind kind = Kind::Missing;
};

struct SchoolDaysTranslationCoverage {
  std::size_t checked = 0;
  std::size_t found = 0;
  std::size_t missing = 0;
  std::size_t blank = 0;
  std::size_t identical = 0;
  std::size_t reviewedIdentical = 0;
  std::size_t contextual = 0;
  std::size_t invalidDlcKeys = 0;
  // Values the DLC sidecar would change relative to a translation already
  // present in the HQ JSON at the same grafted ORS occurrence.
  std::size_t overriddenBaseSlots = 0;
  std::size_t resolvedCollisions = 0;
  std::size_t printTextSlots = 0;
  std::size_t choiceSlots = 0;
  std::size_t scenes = 0;
  std::vector<int> incompleteNodes;
  std::vector<SchoolDaysTranslationIssue> issues;

  // Identity entries may be legitimate (names, punctuation, or LxH's native
  // Spanish text), but are NOT proof of Spanish localization. Report them for
  // human language review rather than claiming automatic 100% completeness.
  bool allKeysPresent() const {
    return checked > 0 && missing == 0 && blank == 0;
  }
  // A source-identical phrase might be valid Spanish, but without a reviewed
  // provenance record it cannot establish the user's no-English guarantee.
  bool certifiedForNonNativeDisplay() const {
    return allKeysPresent() && identical == 0 && invalidDlcKeys == 0;
  }
  std::string summary() const;
};

class SchoolDaysTranslationGate final {
public:
  using SourceTexts = Translation::SourceTexts;
  struct SourceSlot {
    enum class Kind { PrintText, Choice };
    int node = -1;
    std::size_t eventOrdinal = 0;
    std::size_t slot = 0;
    std::string original;
    Kind kind = Kind::PrintText;
    // Grafted LxH dialogue shares the original ending's NODE. The native
    // PLAY_MOVIE boundary, not the NODE number, identifies the added slots.
    bool lxh = false;
  };
  using DetailedSources = std::vector<SourceSlot>;

  // A parsed ORS event's ordinal counts ALL commands, not just PRINT_TEXT and
  // SetSELECT. SetSELECT slots are 0-based after native parser null filtering;
  // PRINT_TEXT always uses slot 0. Worker JSON and runtime share this key.
  static std::string ContextKey(std::size_t eventOrdinal, std::size_t slot);
  static std::size_t EventOrdinal(const Kotonoha_orsData &script,
                                  const Kotonoha_orsEvent *event);
  // Requires a matching extra LxH movie after the original EndRoll in one
  // of the seven original ending scenes. All earlier slots are retail HQ.
  static std::optional<std::size_t> LxhFirstEvent(
      const Kotonoha_orsData &script, const std::string &sceneKey);
  static bool LxhEnding(const std::string &sceneKey);

  // Read the actual KTRF-mapped ORS supplied to this session, including
  // optional LxH overlays. Source strings are never added to the repository.
  static SourceTexts Collect(const SchoolDays::Assets &assets,
                             const std::string &route);
  static DetailedSources CollectDetailed(const SchoolDays::Assets &assets,
                                         const std::string &route);
  static SchoolDaysTranslationCoverage Check(const SourceTexts &sources,
                                             const Translation &dictionary);
  // Reviewed identities use the regular Translation JSON shape, adjacent to
  // the selected locale file: {"123":{"English source":"proper_name"}}
  // or {"123":{"@ors:45:0":"spanish_original_lxh"}}. The values are
  // explicit human-review reasons, never translated dialogue.
  static SchoolDaysTranslationCoverage CheckDetailed(
      const DetailedSources &sources, const Translation &dictionary,
      const Translation &reviewedIdentities);
  // DLC context -> DLC literal -> base context -> base literal -> base nodo.
  // Native HQ needs no base JSON; LxH always needs a translated entry.
  static SchoolDaysTranslationCoverage CheckLayered(
      const DetailedSources &sources, const Translation &base,
      const Translation &baseReviews, const Translation &dlc,
      const Translation &dlcReviews, bool nativeHq);
  // <locale-json-stem>-reviewed-identities.json in the same directory as the
  // effective locale JSON. Missing means no identities certified; malformed
  // manifests fail closed through Translation::load.
  static Translation LoadReviewedIdentities(const std::string &localeJsonPath);
  // Locale IDs come from the selected JSON's stable filename, never from a
  // GPK language directory. "en" is used when no HQ JSON is selected.
  static std::string LocaleId(const std::string &baseJsonPath);
  // Resolves a per-locale DLC sidecar, or returns empty when it is absent.
  // Only Spanish V4b may run LxH without a separate sidecar: all other
  // locales, including native English, explicitly require one when LxH is ON.
  static std::string DlcJsonPath(const std::string &locale,
                                 const std::string &baseJsonPath);
  // Call only when LxH is enabled, before the layered text coverage audit.
  // A complete base FR/EN/etc JSON is never a substitute for its DLC file.
  static void RequireLxhSidecar(const std::string &locale,
                                const std::string &dlcJsonPath);

  // Original English must be used only when no translation JSON is active.
  // Missing or blank keys in any active non-native JSON fail closed.
  static std::string Translate(const Translation &dictionary, int node,
                               const std::string &original);
  static std::string Translate(const Translation &dictionary, int node,
                               std::size_t eventOrdinal, std::size_t slot,
                               const std::string &original);
  static std::string TranslateLayered(
      const Translation &base, const Translation &dlc, int node,
      std::size_t eventOrdinal, std::size_t slot,
      const std::string &original, bool lxh);
};

} // namespace Kotonoha
