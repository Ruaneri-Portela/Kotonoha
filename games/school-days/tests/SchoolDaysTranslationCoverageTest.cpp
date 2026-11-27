#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include <iostream>
#include <stdexcept>

namespace {
using Gate = Kotonoha::SchoolDaysTranslationGate;
using Source = Gate::SourceTexts;

void Expect(bool condition, const char *why) {
  if (!condition) throw std::runtime_error(why);
}
}

int main() {
  try {
    Source sources{{5, {"Hello there.", "Stay here", "Leave now"}},
                   {8, {"Hello there.", "Thanks!", "Thanks!"}}};
    Kotonoha::Translation partial;
    partial.mutableSections()["nodo"]["Hello there."] = "Hola.";
    partial.mutableSections()["5"]["Stay here"] = "Quédate aquí";
    auto status = Gate::Check(sources, partial);
    Expect(status.checked == 6 && status.found == 3 &&
               status.missing == 3 && status.incompleteNodes.size() == 2 &&
               !status.certifiedForNonNativeDisplay(),
           "Missing choice or dialogue did not block partial Spanish JSON");
    bool blocked = false;
    try { (void)Gate::Translate(partial, 5, "Leave now"); }
    catch (const std::runtime_error &) { blocked = true; }
    Expect(blocked, "Non-native locale fell back to English for a missing choice");
    Expect(Gate::Translate(partial, 5, "Hello there.") == "Hola." &&
               Gate::Translate(partial, 5, "Stay here") == "Quédate aquí",
           "NODE-specific choice and global nodo fallback are broken");

    partial.mutableSections()["5"]["Leave now"] = "Salir ahora";
    partial.mutableSections()["8"]["Thanks!"] = "Gracias";
    status = Gate::Check(sources, partial);
    Expect(status.certifiedForNonNativeDisplay() && status.missing == 0 &&
               status.blank == 0 && status.identical == 0,
           "Complete distinct Spanish fixture failed a full corpus audit");

    partial.mutableSections()["8"]["Thanks!"] = "";
    status = Gate::Check(sources, partial);
    Expect(status.blank == 2 && !status.certifiedForNonNativeDisplay(),
           "Empty JSON values must not display blank/English dialogues");
    partial.mutableSections()["8"]["Thanks!"] = "Thanks!";
    status = Gate::Check(sources, partial);
    Expect(status.identical == 2 && status.allKeysPresent() &&
               !status.certifiedForNonNativeDisplay(),
           "Unreviewed unchanged English was incorrectly certified Spanish");

    Kotonoha::Translation commercialEnglish;
    Expect(Gate::Translate(commercialEnglish, 5, "Hello there.") ==
               "Hello there.",
           "The English original must not require a JSON translation");
    std::cout << "SchoolDaysTranslationCoverageTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysTranslationCoverageTest FAIL: " << error.what()
              << '\n';
    return 1;
  }
}
