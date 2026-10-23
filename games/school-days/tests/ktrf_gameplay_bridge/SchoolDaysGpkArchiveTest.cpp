#include <Kotonoha/SchoolDaysGpkArchive.hpp>

#include <zlib.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr std::array<uint8_t, 16> kXorKey = {
    0x82, 0xEE, 0x1D, 0xB3, 0x57, 0xE9, 0x2C, 0xC2,
    0x2F, 0x54, 0x7B, 0x10, 0x4C, 0x9A, 0x75, 0x49,
};

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void U16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value));
    out->push_back(static_cast<uint8_t>(value >> 8u));
}

void U32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>(value));
    out->push_back(static_cast<uint8_t>(value >> 8u));
    out->push_back(static_cast<uint8_t>(value >> 16u));
    out->push_back(static_cast<uint8_t>(value >> 24u));
}

std::vector<uint8_t> Deflate(const std::vector<uint8_t>& input) {
    uLongf size = compressBound(static_cast<uLong>(input.size()));
    std::vector<uint8_t> out(static_cast<size_t>(size));
    const int result = compress2(out.data(), &size, input.data(),
                                 static_cast<uLong>(input.size()), Z_BEST_COMPRESSION);
    Check(result == Z_OK, "synthetic zlib compression failed");
    out.resize(static_cast<size_t>(size));
    return out;
}

struct SyntheticEntry {
    std::u16string name;
    std::vector<uint8_t> logical;
    bool deflate = false;
    size_t prefixLength = 0u;
};

std::vector<uint8_t> BuildPack(const std::vector<SyntheticEntry>& entries) {
    constexpr uint32_t firstOffset = 5120u;
    std::vector<uint8_t> pack(firstOffset, 0u);
    std::vector<uint8_t> index;
    uint32_t offset = firstOffset;

    for (const auto& item : entries) {
        std::vector<uint8_t> stored = item.deflate ? Deflate(item.logical) : item.logical;
        Check(item.prefixLength <= stored.size() && item.prefixLength <= 255u,
              "synthetic prefix length is invalid");
        Check(stored.size() <= static_cast<size_t>(UINT32_MAX),
              "synthetic stored stream is too large");

        U16(&index, static_cast<uint16_t>(item.name.size()));
        for (char16_t unit : item.name) U16(&index, static_cast<uint16_t>(unit));
        U16(&index, 0u); U16(&index, 0u); U16(&index, 0u);
        U32(&index, offset);
        U32(&index, static_cast<uint32_t>(stored.size()));
        const char* magic = item.deflate ? "DFLT" : "    ";
        index.insert(index.end(), magic, magic + 4);
        U32(&index, item.deflate ? static_cast<uint32_t>(item.logical.size()) : 0u);
        index.push_back(static_cast<uint8_t>(item.prefixLength));
        index.insert(index.end(), stored.begin(), stored.begin() + item.prefixLength);

        pack.insert(pack.end(), stored.begin() + item.prefixLength, stored.end());
        offset += static_cast<uint32_t>(stored.size() - item.prefixLength);
    }
    U16(&index, 0u); U16(&index, 0u);

    std::vector<uint8_t> compressedIndex = Deflate(index);
    std::vector<uint8_t> encoded;
    U32(&encoded, static_cast<uint32_t>(index.size()));
    encoded.insert(encoded.end(), compressedIndex.begin(), compressedIndex.end());
    for (size_t i = 0u; i < encoded.size(); ++i) encoded[i] ^= kXorKey[i & 15u];
    pack.insert(pack.end(), encoded.begin(), encoded.end());
    const char sig0[] = "STKFile0PIDX";
    pack.insert(pack.end(), sig0, sig0 + 12);
    U32(&pack, static_cast<uint32_t>(encoded.size()));
    const char sig1[] = "STKFile0PACKFILE";
    pack.insert(pack.end(), sig1, sig1 + 16);
    return pack;
}

void WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Check(static_cast<bool>(out), "synthetic GPK could not be created");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(out), "synthetic GPK write failed");
}

void TestSynthetic() {
    const fs::path folder = fs::temp_directory_path() / "kotonoha_sd_gpk_archive";
    std::error_code ec;
    fs::remove_all(folder, ec);
    fs::create_directories(folder);

    const std::vector<uint8_t> raw = {'R','A','W','-','O','K'};
    const std::vector<uint8_t> compressed = {
        'D','F','L','T','-','O','K','-','1','2','3','4','5','6','7','8','9'
    };
    const auto bytes = BuildPack({
        {u"Folder/RAW.BIN", raw, false, 3u},
        {u"Folder/Compressed.TXT", compressed, true, 8u},
    });
    const fs::path packPath = folder / "Synthetic.GPK";
    WriteFile(packPath, bytes);

    Kotonoha::SchoolDaysGpkArchive archive;
    std::string error;
    Check(archive.Open(packPath, &error), error);
    Check(archive.EntryCount() == 2u, "synthetic entry count drifted");
    Check(archive.PidxOffset() > 5120u, "synthetic PIDX boundary was not located");

    std::vector<uint8_t> actual;
    Check(archive.Read("folder\\raw.bin", &actual, &error), error);
    Check(actual == raw, "raw logical stream reconstruction drifted");
    Check(archive.Read("FOLDER/compressed.txt", &actual, &error), error);
    Check(actual == compressed, "DFLT logical stream reconstruction drifted");
    std::vector<std::string> children;
    Check(archive.ListDirectory("fOlDeR", &children) ==
              Kotonoha::SchoolDaysGpkLookupStatus::Present &&
              children == std::vector<std::string>({
                  "Folder/Compressed.TXT", "Folder/RAW.BIN"}),
          "synthetic canonical directory listing drifted");
    Check(archive.Lookup("folder/missing.bin") ==
              Kotonoha::SchoolDaysGpkLookupStatus::Missing,
          "missing synthetic entry did not stay missing");
    Check(archive.Lookup("../folder/raw.bin") ==
              Kotonoha::SchoolDaysGpkLookupStatus::Invalid,
          "traversal synthetic lookup was accepted");

    auto corrupt = bytes;
    corrupt.back() ^= 1u;
    const fs::path corruptPath = folder / "Corrupt.GPK";
    WriteFile(corruptPath, corrupt);
    Kotonoha::SchoolDaysGpkArchive bad;
    Check(!bad.Open(corruptPath, &error), "corrupt retail trailer was accepted");

    const auto collisionBytes = BuildPack({
        {u"Asset.DAT", {'A'}, false, 1u},
        {u"ASSET.dat", {'B'}, false, 1u},
    });
    const fs::path collisionPath = folder / "Collision.GPK";
    WriteFile(collisionPath, collisionBytes);
    Check(bad.Open(collisionPath, &error), error);
    Check(bad.Lookup("asset.dat") == Kotonoha::SchoolDaysGpkLookupStatus::Collision,
          "case-fold collision did not fail closed");
    Check(bad.ListDirectory("", &children) ==
              Kotonoha::SchoolDaysGpkLookupStatus::Collision && children.empty(),
          "case-fold listing collision did not fail closed");

    const auto& specs = Kotonoha::SchoolDaysRetailGpkSpecs();
    Check(specs.size() == 29u && std::string(specs.front().filename) == "BGM.GPK" &&
              std::string(specs.front().logicalPrefix) == "BGM" &&
              std::string(specs[22].filename) == "System.gpk" &&
              std::string(specs[22].logicalPrefix) == "System" &&
              std::string(specs.back().filename) == "Voice05.GPK",
          "retail pack prefix map drifted");

    fs::remove_all(folder, ec);
}

void TestRetail(const fs::path& packDirectory) {
    Kotonoha::SchoolDaysGpkAssetStore store;
    std::string error;
    Check(store.OpenRetailDirectory(packDirectory, &error), error);
    Check(store.ArchiveCount() == 29u, "retail pack count drifted");
    Check(store.EntryCount() == 69936u, "retail entry census drifted");

    std::vector<uint8_t> debugInfo;
    Check(store.Read("ini/debuginfo.ini", &debugInfo, &error), error);
    const std::string expectedDebug =
        "[NoCheck]=\"1\"\r\n\r\n"
        "[CheckInterval]=\"10\";\r\n\r\n"
        "[LogCreate]=\"0\";\r\n\r\n"
        "[SerialInput]=\"0\"\r\n\r\n";
    Check(debugInfo == std::vector<uint8_t>(expectedDebug.begin(), expectedDebug.end()),
          "retail DFLT DEBUGINFO.INI witness drifted");

    std::vector<uint8_t> backlog;
    Check(store.Read("SYSTEM/backlog/backlog_horizon.cmap", &backlog, &error), error);
    Check(backlog.size() == 480008u,
          "retail raw BACKLOG_HORIZON.CMAP witness size drifted");
    const uLong backlogCrc = crc32(
        0u, reinterpret_cast<const Bytef*>(backlog.data()),
        static_cast<uInt>(backlog.size()));
    Check(static_cast<uint32_t>(backlogCrc) == 0xBCCA9F39u,
          "retail raw BACKLOG_HORIZON.CMAP witness CRC drifted");
    std::vector<std::string> backlogChildren;
    Check(store.ListDirectory("system/backlog", &backlogChildren) ==
              Kotonoha::SchoolDaysGpkLookupStatus::Present,
          "retail virtual directory listing failed");
    Check(std::find(backlogChildren.begin(), backlogChildren.end(),
                    "System/BACKLOG/BACKLOG_HORIZON.CMAP") != backlogChildren.end(),
          "retail virtual directory listing lost canonical CMAP sibling");
    Check(store.Lookup("system/backlog/does_not_exist.png") ==
              Kotonoha::SchoolDaysGpkLookupStatus::Missing,
          "retail missing lookup did not stay missing");
}

} // namespace

int main(int argc, char** argv) {
    try {
        TestSynthetic();
        if (argc >= 2) TestRetail(fs::path(argv[1]));
        std::cout << "SCHOOL DAYS GPK ARCHIVE GATE PASS\n"
                  << "retail_envelope_xor_zlib=PASS\n"
                  << "raw_inline_prefix=PASS\n"
                  << "dflt_inline_prefix=PASS\n"
                  << "casefold_unique_lookup=PASS\n"
                  << "retail_pack_prefixes_29=PASS\n"
                  << "retail_witness=" << (argc >= 2 ? "PASS" : "SKIP") << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SchoolDaysGpkArchiveTest: " << e.what() << '\n';
        return 2;
    }
}
