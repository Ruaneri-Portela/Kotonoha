#include <Kotonoha/routing/SchoolDaysSaveService.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using Service = Kotonoha::SchoolDaysSaveService;

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysSaveServiceReadTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

Kotonoha::SchoolDaysFlgHEntry StringEntry(const char16_t* key,
                                          const char16_t* value) {
    Kotonoha::SchoolDaysFlgHEntry entry;
    entry.key = key;
    entry.type = 8;
    entry.identifier = -1;
    entry.stringValue = value;
    return entry;
}

} // namespace

int main() {
    Kotonoha::SchoolDaysSaveProfile profile;
    profile.saveConfig = "FILMEngine/SaveFile00%d";
    profile.useEnglish = true;

    Kotonoha_KtrfError error{};
    Service::SlotMetadata metadata;
    Kotonoha::SchoolDaysFlgH globals;

    Check(Service::ReadSlotMetadata(profile, 10, false, &globals, &metadata,
                                    &error) &&
              !metadata.occupied,
          "closed SaveFile did not force empty slot");
    Check(Service::ReadSlotMetadata(profile, 10, true, nullptr, &metadata,
                                    &error) &&
              !metadata.occupied,
          "missing GlobalFlag did not project empty slot");
    Check(Service::ReadSlotMetadata(profile, 10, true, &globals, &metadata,
                                    &error) &&
              !metadata.occupied,
          "missing SaveConfig did not project empty slot");

    globals.entries.push_back(StringEntry(
        u"FILMEngine/SaveFile0010", u"10/20/2026(Tue.)00:4001"));
    globals.entries.push_back(StringEntry(u"FILMEngine/SaveFile0010_Sub",
                                          u"Kotonoha"));
    Check(Service::ReadSlotMetadata(profile, 10, true, &globals, &metadata,
                                    &error) &&
              metadata.occupied && metadata.date == u"10/20/2026(Tue.)00:40" &&
              metadata.story == u"01" && metadata.comment == u"Kotonoha",
          "English occupied metadata projection mismatch");

    globals.entries.pop_back();
    Check(Service::ReadSlotMetadata(profile, 10, true, &globals, &metadata,
                                    &error) &&
              metadata.occupied && metadata.comment == u"0",
          "missing _Sub did not reproduce native default BSTR zero");

    profile.useEnglish = false;
    globals.entries[0].stringValue =
        u"2012\u5e74 5\u670828\u65e5(\u6708)19:18\u7b2c5\u8a71";
    Check(Service::ReadSlotMetadata(profile, 10, true, &globals, &metadata,
                                    &error) &&
              metadata.occupied &&
              metadata.date == u"2012\u5e74 5\u670828\u65e5(\u6708)19:18" &&
              metadata.story == u"\u7b2c5\u8a71",
          "Japanese occupied metadata projection mismatch");

    globals.entries[0].type = 3;
    Check(!Service::ReadSlotMetadata(profile, 10, true, &globals, &metadata,
                                     &error) &&
              error.code == KOTONOHA_KTRF_ERROR_FORMAT,
          "non-type8 SaveConfig was not rejected by safe projection hardening");
    globals.entries[0].type = 8;
    globals.entries[0].stringValue = u"0";
    Check(Service::ReadSlotMetadata(profile, 10, true, &globals, &metadata,
                                    &error) &&
              !metadata.occupied,
          "one-code-unit SaveConfig did not project empty slot");
    Check(!Service::ReadSlotMetadata(profile, -1, true, &globals, &metadata,
                                     &error) &&
              !Service::ReadSlotMetadata(profile, 100, true, &globals,
                                         &metadata, &error),
          "out-of-range metadata slot was accepted");

    std::cout << "SCHOOL DAYS SAVE SERVICE READ GATE PASS\n"
              << "file_open_gate=PASS\n"
              << "missing_metadata_empty=PASS\n"
              << "english_split_comment=PASS\n"
              << "japanese_split=PASS\n"
              << "missing_sub_native_zero=PASS\n"
              << "unproven_variant_coercion_fail_closed=PASS\n"
              << "length_one_empty=PASS\n";
    return 0;
}
