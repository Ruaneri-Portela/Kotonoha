#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>

#include <zlib.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace Kotonoha {
namespace {

constexpr size_t kMaxLogicalConfigBytes = 1024u * 1024u;

bool Fail(std::string* error, const char* text) {
    if (error != nullptr)
        *error = text != nullptr ? text : "School Days Config.DAT error";
    return false;
}

bool ParseInteger(const std::string& text, int* out) {
    if (out == nullptr || text.empty()) return false;
    long long value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ExtractLine(const std::string& line, std::string* key,
                 std::string* value) {
    if (key == nullptr || value == nullptr || line.size() < 6u ||
        line.front() != '[') {
        return false;
    }
    const size_t close = line.find(']');
    if (close == std::string::npos || close <= 1u ||
        close + 3u >= line.size() || line[close + 1u] != '=' ||
        line[close + 2u] != '"' || line.back() != '"') {
        return false;
    }
    if (line.find('"', close + 3u) != line.size() - 1u) return false;
    *key = line.substr(1u, close - 1u);
    *value = line.substr(close + 3u, line.size() - (close + 4u));
    return !key->empty();
}

std::string MakeLine(const std::string& key, const std::string& value) {
    return "[" + key + "]=\"" + value + "\"";
}

bool ReadWholeFile(const std::filesystem::path& path,
                   std::vector<uint8_t>* bytes, std::string* error) {
    if (bytes == nullptr) return Fail(error, "Config.DAT read output is null");
    bytes->clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return Fail(error, "Config.DAT could not be opened for read");
    const auto end = file.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) >
                       static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
        return Fail(error, "Config.DAT size is invalid");
    }
    bytes->resize(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes->empty() &&
        !file.read(reinterpret_cast<char*>(bytes->data()),
                   static_cast<std::streamsize>(bytes->size()))) {
        bytes->clear();
        return Fail(error, "Config.DAT read failed");
    }
    return true;
}

} // namespace

bool SchoolDaysConfigStorage::Initialize(
    const std::filesystem::path& executableDirectory, std::string* error) {
    if (error != nullptr) error->clear();
    initialized_ = false;
    loaded_ = false;
    dirty_ = false;
    path_.clear();
    lines_.clear();
    trailingLf_ = true;
    if (executableDirectory.empty())
        return Fail(error, "Config.DAT executable directory is empty");
    const auto base = std::filesystem::absolute(executableDirectory).lexically_normal();
    path_ = (base / "Config.DAT").lexically_normal();
    initialized_ = true;
    return true;
}

bool SchoolDaysConfigStorage::DecodeDocument(const std::vector<uint8_t>& bytes,
                                             std::string* text,
                                             std::string* error) {
    if (error != nullptr) error->clear();
    if (text == nullptr) return Fail(error, "Config.DAT decode output is null");
    text->clear();
    if (bytes.empty()) return Fail(error, "Config.DAT is empty");

    if (bytes.size() < 4u ||
        !(bytes[0] == 'D' && bytes[1] == 'F' && bytes[2] == 'L' &&
          bytes[3] == 'T')) {
        if (bytes.size() > kMaxLogicalConfigBytes)
            return Fail(error, "plain Config.DAT exceeds logical size limit");
        text->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }

    if (bytes.size() == 4u) return Fail(error, "DFLT Config.DAT has no zlib stream");
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(
        reinterpret_cast<const Bytef*>(bytes.data() + 4u));
    stream.avail_in = static_cast<uInt>(bytes.size() - 4u);
    if (inflateInit(&stream) != Z_OK)
        return Fail(error, "Config.DAT zlib inflate init failed");

    std::string decoded;
    decoded.reserve(4096u);
    unsigned char chunk[4096];
    int rc = Z_OK;
    while (rc == Z_OK) {
        stream.next_out = chunk;
        stream.avail_out = sizeof(chunk);
        rc = inflate(&stream, Z_NO_FLUSH);
        const size_t produced = sizeof(chunk) - stream.avail_out;
        if (decoded.size() + produced > kMaxLogicalConfigBytes) {
            inflateEnd(&stream);
            return Fail(error, "Config.DAT decompressed payload exceeds size limit");
        }
        decoded.append(reinterpret_cast<const char*>(chunk), produced);
    }
    const bool cleanEnd = rc == Z_STREAM_END && stream.avail_in == 0u;
    inflateEnd(&stream);
    if (!cleanEnd) return Fail(error, "Config.DAT zlib stream is invalid/trailing");
    *text = std::move(decoded);
    return true;
}

bool SchoolDaysConfigStorage::EncodeDocument(const std::string& text,
                                             std::vector<uint8_t>* bytes,
                                             std::string* error) {
    if (error != nullptr) error->clear();
    if (bytes == nullptr) return Fail(error, "Config.DAT encode output is null");
    bytes->clear();
    if (text.empty() || text.size() > kMaxLogicalConfigBytes)
        return Fail(error, "Config.DAT logical payload size is invalid");
    const uLong sourceSize = static_cast<uLong>(text.size());
    const uLong bound = compressBound(sourceSize);
    if (bound > static_cast<uLong>(std::numeric_limits<size_t>::max() - 4u))
        return Fail(error, "Config.DAT compressed size overflows host size");
    bytes->resize(static_cast<size_t>(bound) + 4u);
    (*bytes)[0] = 'D'; (*bytes)[1] = 'F'; (*bytes)[2] = 'L'; (*bytes)[3] = 'T';
    uLongf compressedSize = bound;
    const int rc = compress2(
        reinterpret_cast<Bytef*>(bytes->data() + 4u), &compressedSize,
        reinterpret_cast<const Bytef*>(text.data()), sourceSize,
        Z_BEST_COMPRESSION);
    if (rc != Z_OK) {
        bytes->clear();
        return Fail(error, "Config.DAT zlib compression failed");
    }
    bytes->resize(static_cast<size_t>(compressedSize) + 4u);
    return true;
}

bool SchoolDaysConfigStorage::Load(LoadResult* out, std::string* error) {
    if (error != nullptr) error->clear();
    if (!initialized_ || out == nullptr)
        return Fail(error, "Config.DAT storage is not initialized/load output null");
    *out = {};
    lines_.clear();
    trailingLf_ = true;
    loaded_ = false;
    dirty_ = false;

    std::error_code ec;
    const bool exists = std::filesystem::exists(path_, ec);
    if (ec) return Fail(error, "Config.DAT existence query failed");
    if (!exists) {
        lines_.push_back("< Config.dat >");
        out->present = false;
        loaded_ = true;
        return true;
    }

    std::vector<uint8_t> bytes;
    std::string text;
    if (!ReadWholeFile(path_, &bytes, error) || !DecodeDocument(bytes, &text, error))
        return false;
    out->present = true;
    if (!ParseLoadedDocument(text, out, error)) return false;
    loaded_ = true;
    dirty_ = false;
    return true;
}

bool SchoolDaysConfigStorage::ParseLoadedDocument(const std::string& text,
                                                  LoadResult* out,
                                                  std::string* error) {
    if (out == nullptr) return Fail(error, "Config.DAT parse output is null");
    if (text.find('\0') != std::string::npos)
        return Fail(error, "Config.DAT logical payload contains NUL");
    lines_.clear();
    trailingLf_ = !text.empty() && text.back() == '\n';
    size_t begin = 0u;
    while (begin < text.size()) {
        const size_t end = text.find('\n', begin);
        std::string line = end == std::string::npos
                               ? text.substr(begin)
                               : text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r')
            return Fail(error, "Config.DAT uses unsupported CR/CRLF line endings");
        lines_.push_back(std::move(line));
        if (end == std::string::npos) break;
        begin = end + 1u;
        if (begin == text.size()) break;
    }

    std::unordered_map<std::string, size_t> validKeys;
    auto readInt = [&](const std::string& key, const std::string& value,
                       int* target) -> bool {
        int parsed = 0;
        if (!ParseInteger(value, &parsed)) {
            if (error != nullptr) *error = "Config.DAT key has invalid integer: " + key;
            return false;
        }
        *target = parsed;
        return true;
    };

    for (size_t index = 0; index < lines_.size(); ++index) {
        std::string key;
        std::string value;
        if (!ExtractLine(lines_[index], &key, &value)) continue;
        if (!validKeys.emplace(key, index).second) {
            if (error != nullptr) *error = "Config.DAT contains duplicate valid key: " + key;
            return false;
        }

        int number = 0;
        if (key == "VoiceVolume") {
            if (!readInt(key, value, &number) || number < 0 || number > 10)
                return Fail(error, "Config.DAT VoiceVolume is outside 0..10");
            out->state.voiceVolume = number;
        } else if (key == "BgmVolume") {
            if (!readInt(key, value, &number) || number < 0 || number > 10)
                return Fail(error, "Config.DAT BgmVolume is outside 0..10");
            out->state.bgmVolume = number;
        } else if (key == "SeVolume") {
            if (!readInt(key, value, &number) || number < 0 || number > 10)
                return Fail(error, "Config.DAT SeVolume is outside 0..10");
            out->state.seVolume = number;
        } else if (key == "TextView") {
            if (!readInt(key, value, &number)) return false;
            out->state.textView = number != 0;
        } else if (key == "MenVoice") {
            if (!readInt(key, value, &number)) return false;
            out->state.menVoice = number != 0;
        } else if (key == "Mute") {
            if (!readInt(key, value, &number)) return false;
            out->state.mute = number != 0;
        } else if (key == "Skip") {
            if (!readInt(key, value, &number)) return false;
            out->state.skip = number != 0;
        } else if (key == "AutoDraw") {
            if (!readInt(key, value, &number)) return false;
            out->state.autoDraw = number != 0;
        } else if (key == "AutoMode") {
            if (!readInt(key, value, &number)) return false;
            out->state.autoMode = number != 0;
        } else if (key == "SuperSkip") {
            if (!readInt(key, value, &number)) return false;
            out->state.superSkip = number != 0;
        } else if (key == "UseSOM") {
            if (!readInt(key, value, &number)) return false;
            out->state.useSom = number != 0;
        } else if (key == "DisplayType") {
            if (!readInt(key, value, &number)) return false;
            out->display.hasDisplayType = true;
            out->display.displayType = number;
        } else if (key == "TypeMiniNote") {
            if (!readInt(key, value, &number)) return false;
            out->display.hasTypeMiniNote = true;
            out->display.typeMiniNote = number;
        } else if (key == "WindowMode") {
            if (!readInt(key, value, &number)) return false;
            out->display.hasWindowMode = true;
            out->display.windowMode = number;
        }
        // MasterVolume is intentionally preserved but not loaded into active
        // state: SysMenu ctor initializes -1.0 and no native loader xref was
        // authenticated for the saved MasterVolume key.
    }
    return SchoolDaysRetailConfigPolicy::Validate(out->state) ||
           Fail(error, "Config.DAT produced invalid retail state");
}

bool SchoolDaysConfigStorage::SetKeyValue(const std::string& key,
                                          const std::string& value,
                                          std::string* error) {
    if (!loaded_ || key.empty())
        return Fail(error, "Config.DAT cache is not loaded/key empty");
    size_t found = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < lines_.size(); ++i) {
        std::string existingKey;
        std::string existingValue;
        if (!ExtractLine(lines_[i], &existingKey, &existingValue) || existingKey != key)
            continue;
        if (found != std::numeric_limits<size_t>::max())
            return Fail(error, "Config.DAT cache contains duplicate valid key");
        found = i;
    }
    const std::string replacement = MakeLine(key, value);
    if (found == std::numeric_limits<size_t>::max()) {
        lines_.push_back(replacement);
        dirty_ = true;
    } else if (lines_[found] != replacement) {
        lines_[found] = replacement;
        dirty_ = true;
    }
    return true;
}

bool SchoolDaysConfigStorage::CacheState(
    const SchoolDaysRetailConfigPolicy::State& state, std::string* error) {
    if (error != nullptr) error->clear();
    if (!SchoolDaysRetailConfigPolicy::Validate(state))
        return Fail(error, "Config.DAT state is invalid");
    // The v1.02 writer for MasterVolume exists, but its load-back path has not
    // been authenticated. The active remaster state therefore intentionally
    // does not import the persisted value. Do not rewrite that opaque line
    // from the default runtime value when caching unrelated Config changes.
    return SetKeyValue("VoiceVolume", std::to_string(state.voiceVolume), error) &&
           SetKeyValue("BgmVolume", std::to_string(state.bgmVolume), error) &&
           SetKeyValue("SeVolume", std::to_string(state.seVolume), error) &&
           SetKeyValue("TextView", state.textView ? "1" : "0", error) &&
           SetKeyValue("MenVoice", state.menVoice ? "1" : "0", error) &&
           SetKeyValue("Mute", state.mute ? "1" : "0", error) &&
           SetKeyValue("Skip", state.skip ? "1" : "0", error) &&
           SetKeyValue("AutoDraw", state.autoDraw ? "1" : "0", error) &&
           SetKeyValue("AutoMode", state.autoMode ? "1" : "0", error) &&
           SetKeyValue("SuperSkip", state.superSkip ? "1" : "0", error) &&
           SetKeyValue("UseSOM", state.useSom ? "1" : "0", error);
}

bool SchoolDaysConfigStorage::CacheDisplayType(int value, std::string* error) {
    if (error != nullptr) error->clear();
    return SetKeyValue("DisplayType", std::to_string(value), error);
}

bool SchoolDaysConfigStorage::CacheTypeMiniNote(int value,
                                                std::string* error) {
    if (error != nullptr) error->clear();
    return SetKeyValue("TypeMiniNote", std::to_string(value), error);
}

bool SchoolDaysConfigStorage::SerializeLogical(std::string* out,
                                               std::string* error) const {
    if (out == nullptr || !loaded_) return Fail(error, "Config.DAT cache is not loaded");
    out->clear();
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i != 0u) out->push_back('\n');
        out->append(lines_[i]);
    }
    if (trailingLf_) out->push_back('\n');
    return !out->empty() || Fail(error, "Config.DAT logical cache is empty");
}

bool SchoolDaysConfigStorage::Flush(std::string* error) {
    if (error != nullptr) error->clear();
    if (!initialized_ || !loaded_) return Fail(error, "Config.DAT storage is not loaded");
    if (!dirty_) return true;
    std::string logical;
    std::vector<uint8_t> encoded;
    if (!SerializeLogical(&logical, error) || !EncodeDocument(logical, &encoded, error))
        return false;
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    if (!file) return Fail(error, "Config.DAT could not be opened for write");
    file.write(reinterpret_cast<const char*>(encoded.data()),
               static_cast<std::streamsize>(encoded.size()));
    if (!file) return Fail(error, "Config.DAT write failed");
    file.close();
    if (!file) return Fail(error, "Config.DAT close failed");
    dirty_ = false;
    return true;
}

} // namespace Kotonoha
