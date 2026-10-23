#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "KtrfSchoolDaysPersistenceCodecTest: " << message << '\n';
    std::exit(2);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) Fail("cannot open " + path.string());
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) Fail("cannot size " + path.string());
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) Fail("cannot read " + path.string());
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: KtrfSchoolDaysPersistenceCodecTest <copied-save-dir>\n";
        return 2;
    }

    const fs::path root(argv[1]);
    if (!fs::is_directory(root)) Fail("save directory does not exist");

    std::vector<fs::path> saves;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("SaveFile", 0u) == 0u && entry.path().extension() == ".DAT") {
            saves.push_back(entry.path());
        }
    }
    std::sort(saves.begin(), saves.end());
    if (saves.size() != 27u) {
        Fail("expected exactly 27 copied SaveFile*.DAT inputs");
    }

    size_t slogRecords = 0u;
    size_t saveFlgHEntries = 0u;
    for (const fs::path& path : saves) {
        const auto bytes = ReadBytes(path);
        Kotonoha::SchoolDaysSLog node;
        std::string error;
        if (!Kotonoha::SchoolDaysPersistenceCodec::ParseSLog(bytes, &node, &error)) {
            Fail(path.filename().string() + " parse: " + error);
        }
        std::vector<uint8_t> encoded;
        if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeSLog(node, &encoded, &error)) {
            Fail(path.filename().string() + " serialize: " + error);
        }
        if (encoded != bytes) {
            Fail(path.filename().string() + " is not byte-identical after round-trip");
        }
        slogRecords += node.records.size();
        for (const auto& record : node.records) {
            saveFlgHEntries += record.flags.entries.size();
        }
    }

    const fs::path globalPath = root / "GlobalFlag.DAT";
    const auto globalBytes = ReadBytes(globalPath);
    Kotonoha::SchoolDaysFlgH global;
    std::string error;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
            globalBytes, &global, &error)) {
        Fail("GlobalFlag.DAT parse: " + error);
    }
    std::vector<uint8_t> globalEncoded;
    if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
            global, &globalEncoded, &error)) {
        Fail("GlobalFlag.DAT serialize: " + error);
    }
    if (globalEncoded != globalBytes) {
        Fail("GlobalFlag.DAT is not byte-identical after round-trip");
    }

    // Original FlgH writer behavior for programmatic values not present in
    // the authenticated corpus: unsupported VARIANT kinds normalize to
    // type3/value0, while type11 is sign-extended from int16 before varint.
    {
        Kotonoha::SchoolDaysFlgH synthetic;
        Kotonoha::SchoolDaysFlgHEntry unsupported;
        unsupported.key = u"Unsupported";
        unsupported.identifier = -1;
        unsupported.type = 99;
        unsupported.integer = 1234;
        synthetic.entries.push_back(unsupported);
        Kotonoha::SchoolDaysFlgHEntry boolish;
        boolish.key = u"Boolish";
        boolish.identifier = -1;
        boolish.type = 11;
        boolish.integer = 0x00018001;
        synthetic.entries.push_back(boolish);

        std::vector<uint8_t> syntheticBytes;
        if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
                synthetic, &syntheticBytes, &error)) {
            Fail("synthetic FlgH serialize: " + error);
        }
        Kotonoha::SchoolDaysFlgH normalized;
        if (!Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
                syntheticBytes, &normalized, &error)) {
            Fail("synthetic FlgH parse: " + error);
        }
        if (normalized.entries.size() != 2u ||
            normalized.entries[0].type != 3 ||
            normalized.entries[0].integer != 0 ||
            normalized.entries[1].type != 11 ||
            normalized.entries[1].integer != -32767) {
            Fail("FlgH writer normalization/sign-extension mismatch");
        }
    }

    size_t type3 = 0u, type4 = 0u, type8 = 0u, type11 = 0u;
    for (const auto& entry : global.entries) {
        if (entry.type == 3) ++type3;
        else if (entry.type == 4) ++type4;
        else if (entry.type == 8) ++type8;
        else if (entry.type == 11) ++type11;
    }
    if (global.entries.size() != 2186u || type3 != 1u || type4 != 0u ||
        type8 != 94u || type11 != 2091u) {
        Fail("GlobalFlag.DAT authenticated inventory mismatch");
    }

    std::cout << "SCHOOL DAYS PERSISTENCE CODEC GATE 6 PASS\n"
              << "savefiles=27\n"
              << "save_roundtrip_identical=27\n"
              << "slog_records=" << slogRecords << '\n'
              << "embedded_flgh_entries=" << saveFlgHEntries << '\n'
              << "globalflag_entries=" << global.entries.size() << '\n'
              << "globalflag_type3=" << type3 << '\n'
              << "globalflag_type8=" << type8 << '\n'
              << "globalflag_type11=" << type11 << '\n'
              << "writer_type_normalization=PASS\n"
              << "writer_type11_int16_sign_extension=PASS\n"
              << "globalflag_roundtrip_identical=PASS\n";
    return 0;
}
