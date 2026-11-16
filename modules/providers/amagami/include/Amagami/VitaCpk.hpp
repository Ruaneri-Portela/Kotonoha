// Adapted from hokagocafe/open-amagami (MIT, copyright 2026 hokagocafe).
// Read-only CRI CPK support for decrypted PS Vita game data. Both TOC
// (SCENARIO/GRAPH) and ITOC (MUSIC/SOUND/VOICE) archive indexes are handled.
// A separate Vita asset provider and native runtime are still required.
// Format documentation: https://github.com/hokagocafe/open-amagami/blob/main/docs/technical/vita-cpk-format.md.
// Licence notice: third_party/open-amagami-LICENSE.txt.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Amagami::Vita {

// One cell of an @UTF table (CRI's own binary table format: CPK headers, and the Atom sound banks, ACB).
struct UtfValue {
    bool is_string = false;
    bool is_blob = false;  // `s` holds the bytes of a "data" column (often a nested table)
    std::string s;
    int64_t i = 0;
    bool present = false;
};
// row -> column name -> value. Floats are not read.
struct UtfTable {
    std::vector<std::unordered_map<std::string, UtfValue>> rows;
};
// A whole table given as bytes (masked or not): an ACB file, or a blob column of one.
UtfTable cpk_parse_utf(const std::string& bytes);

struct CpkEntry {
    uint64_t offset = 0, size = 0, extract_size = 0;
};

// Keyed by "dir/name" exactly as stored (e.g. "bg/001_10.gxt").
using CpkIndex = std::unordered_map<std::string, CpkEntry>;

CpkIndex cpk_read_index(const std::string& path);
// Every entry of a CPK in either layout, in storage order (a TOC archive's rows, an ITOC archive's ids). `start` is the
// archive's offset in the file, for one nested in another (VOICE.CPK holds one archive per speaker).
std::vector<CpkEntry> cpk_read_entries(const std::string& path, uint64_t start = 0);
std::string cpk_decompress_crilayla(const std::string& data);
// Reads and, if needed, decompresses one entry. Returns false if `key` is not in `index`.
bool cpk_read(const std::string& cpk_path, const CpkIndex& index, const std::string& key, std::string& out);

}  // namespace Amagami::Vita
