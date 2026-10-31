#include <Kotonoha/SchoolDaysGpkArchive.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace Kotonoha {
namespace {

constexpr std::array<uint8_t, 16> kPidxXorKey = {
    0x82, 0xEE, 0x1D, 0xB3, 0x57, 0xE9, 0x2C, 0xC2,
    0x2F, 0x54, 0x7B, 0x10, 0x4C, 0x9A, 0x75, 0x49,
};
constexpr char kPidxSignature[] = "STKFile0PIDX";
constexpr char kPackSignature[] = "STKFile0PACKFILE";
constexpr size_t kTrailerSize = 32u;

bool Fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

uint32_t ReadLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
}

bool InflateExact(const uint8_t* input, size_t inputSize, uint32_t expectedSize,
                  std::vector<uint8_t>* output, std::string* error,
                  const char* label) {
    if (output == nullptr) return Fail(error, "School Days GPK inflate output is null");
    output->clear();
    if (input == nullptr || inputSize == 0u ||
        inputSize > static_cast<size_t>(std::numeric_limits<uInt>::max())) {
        return Fail(error, std::string("School Days GPK ") + label +
                              " zlib stream is invalid or too large");
    }

    try {
        output->resize(static_cast<size_t>(expectedSize));
    } catch (...) {
        output->clear();
        return Fail(error, std::string("School Days GPK ") + label +
                              " output allocation failed");
    }

    uint8_t emptyOutput = 0u;
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input));
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = expectedSize == 0u
        ? reinterpret_cast<Bytef*>(&emptyOutput)
        : reinterpret_cast<Bytef*>(output->data());
    stream.avail_out = expectedSize == 0u ? 1u : static_cast<uInt>(expectedSize);

    const int init = inflateInit(&stream);
    if (init != Z_OK) {
        output->clear();
        return Fail(error, std::string("School Days GPK ") + label +
                              " zlib initialization failed");
    }
    const int result = inflate(&stream, Z_FINISH);
    const bool exact = result == Z_STREAM_END &&
                       stream.total_in == static_cast<uLong>(inputSize) &&
                       stream.total_out == static_cast<uLong>(expectedSize);
    const int endResult = inflateEnd(&stream);
    if (!exact || endResult != Z_OK) {
        output->clear();
        return Fail(error, std::string("School Days GPK ") + label +
                              " zlib stream does not match its declared size");
    }
    return true;
}

bool Utf16ToUtf8(const std::u16string& input, std::string* output) {
    if (output == nullptr) return false;
    output->clear();
    for (size_t i = 0; i < input.size(); ++i) {
        uint32_t cp = static_cast<uint16_t>(input[i]);
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            if (i + 1u >= input.size()) return false;
            const uint32_t low = static_cast<uint16_t>(input[++i]);
            if (low < 0xDC00u || low > 0xDFFFu) return false;
            cp = 0x10000u + ((cp - 0xD800u) << 10u) + (low - 0xDC00u);
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return false;
        }

        if (cp <= 0x7Fu) {
            output->push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            output->push_back(static_cast<char>(0xC0u | (cp >> 6u)));
            output->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            output->push_back(static_cast<char>(0xE0u | (cp >> 12u)));
            output->push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            output->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            output->push_back(static_cast<char>(0xF0u | (cp >> 18u)));
            output->push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            output->push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            output->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return true;
}

bool NormalizeLogicalPath(std::string_view input, std::string* canonical,
                          std::string* folded) {
    if (folded == nullptr) return false;
    if (canonical != nullptr) canonical->clear();
    folded->clear();
    if (input.empty() || input.front() == '/' || input.front() == '\\') return false;
    if (input.size() >= 2u &&
        ((input[0] >= 'A' && input[0] <= 'Z') ||
         (input[0] >= 'a' && input[0] <= 'z')) && input[1] == ':') {
        return false;
    }

    size_t pos = 0u;
    bool haveComponent = false;
    while (pos < input.size()) {
        while (pos < input.size() && (input[pos] == '/' || input[pos] == '\\')) ++pos;
        if (pos >= input.size()) break;
        const size_t begin = pos;
        while (pos < input.size() && input[pos] != '/' && input[pos] != '\\') ++pos;
        const std::string_view part = input.substr(begin, pos - begin);
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find(':') != std::string_view::npos) return false;
        if (haveComponent) {
            folded->push_back('/');
            if (canonical != nullptr) canonical->push_back('/');
        }
        if (canonical != nullptr) canonical->append(part.data(), part.size());
        for (unsigned char ch : part) {
            if (ch >= static_cast<unsigned char>('A') &&
                ch <= static_cast<unsigned char>('Z')) {
                ch = static_cast<unsigned char>(ch - static_cast<unsigned char>('A') +
                                                static_cast<unsigned char>('a'));
            }
            folded->push_back(static_cast<char>(ch));
        }
        haveComponent = true;
    }
    return haveComponent;
}

bool FoldLogicalPath(std::string_view input, std::string* folded) {
    return NormalizeLogicalPath(input, nullptr, folded);
}

bool ReadRange(std::ifstream* file, uint64_t offset, size_t size,
               std::vector<uint8_t>* out) {
    if (file == nullptr || out == nullptr ||
        size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()) ||
        offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    try {
        out->assign(size, 0u);
    } catch (...) {
        out->clear();
        return false;
    }
    file->clear();
    file->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!*file) return false;
    if (size != 0u) {
        file->read(reinterpret_cast<char*>(out->data()),
                   static_cast<std::streamsize>(size));
        if (file->gcount() != static_cast<std::streamsize>(size)) return false;
    }
    return true;
}

std::string LookupFailureMessage(SchoolDaysGpkLookupStatus status) {
    switch (status) {
    case SchoolDaysGpkLookupStatus::Missing:
        return "School Days GPK asset is missing";
    case SchoolDaysGpkLookupStatus::Collision:
        return "School Days GPK asset has a case-insensitive collision";
    case SchoolDaysGpkLookupStatus::Invalid:
        return "School Days GPK asset path is invalid";
    case SchoolDaysGpkLookupStatus::Present:
        break;
    }
    return "School Days GPK asset lookup failed";
}

} // namespace

const std::array<SchoolDaysRetailGpkSpec, 29>& SchoolDaysRetailGpkSpecs() {
    static const std::array<SchoolDaysRetailGpkSpec, 29> specs = {{
        {"BGM.GPK", "BGM"},
        {"Event00.GPK", "Event00"}, {"Event01.GPK", "Event01"},
        {"Event02.GPK", "Event02"}, {"Event03.GPK", "Event03"},
        {"Event04.GPK", "Event04"}, {"Event05.GPK", "Event05"},
        {"Ini.GPK", "Ini"},
        {"Movie00.GPK", "Movie00"}, {"Movie01.GPK", "Movie01"},
        {"Movie02.GPK", "Movie02"}, {"Movie03.GPK", "Movie03"},
        {"Movie04.GPK", "Movie04"}, {"Movie05.GPK", "Movie05"},
        {"Script.GPK", "Script"},
        {"Se00.GPK", "Se00"}, {"Se01.GPK", "Se01"},
        {"Se02.GPK", "Se02"}, {"Se03.GPK", "Se03"},
        {"Se04.GPK", "Se04"}, {"Se05.GPK", "Se05"},
        {"SysSe.GPK", "SysSe"}, {"System.GPK", "System"},
        {"Voice00.GPK", "Voice00"}, {"Voice01.GPK", "Voice01"},
        {"Voice02.GPK", "Voice02"}, {"Voice03.GPK", "Voice03"},
        {"Voice04.GPK", "Voice04"}, {"Voice05.GPK", "Voice05"},
    }};
    return specs;
}

void SchoolDaysGpkArchive::Close() {
    path_.clear();
    pidxOffset_ = 0u;
    index_.Clear();
    canonicalEntries_.clear();
    foldedEntries_.clear();
    foldedCollisions_.clear();
}

bool SchoolDaysGpkArchive::Open(const std::filesystem::path& path,
                                std::string* error) {
    if (error != nullptr) error->clear();
    Close();

    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(error, "School Days GPK could not be opened");
    file.seekg(0, std::ios::end);
    const std::streamoff fileEnd = file.tellg();
    if (fileEnd < static_cast<std::streamoff>(kTrailerSize))
        return Fail(error, "School Days GPK is too short for the retail trailer");
    const uint64_t fileSize = static_cast<uint64_t>(fileEnd);

    std::vector<uint8_t> trailer;
    if (!ReadRange(&file, fileSize - kTrailerSize, kTrailerSize, &trailer))
        return Fail(error, "School Days GPK trailer could not be read");
    if (std::memcmp(trailer.data(), kPidxSignature, 12u) != 0 ||
        std::memcmp(trailer.data() + 16u, kPackSignature, 16u) != 0) {
        return Fail(error, "School Days GPK retail trailer signature mismatch");
    }
    const uint32_t pidxLength = ReadLe32(trailer.data() + 12u);
    if (pidxLength <= 4u || static_cast<uint64_t>(pidxLength) > fileSize - kTrailerSize)
        return Fail(error, "School Days GPK PIDX length is invalid");
    const uint64_t pidxOffset = fileSize - kTrailerSize - pidxLength;

    std::vector<uint8_t> encoded;
    if (!ReadRange(&file, pidxOffset, pidxLength, &encoded))
        return Fail(error, "School Days GPK PIDX could not be read");
    for (size_t i = 0u; i < encoded.size(); ++i)
        encoded[i] ^= kPidxXorKey[i & 15u];
    const uint32_t decodedSize = ReadLe32(encoded.data());

    std::vector<uint8_t> decoded;
    if (!InflateExact(encoded.data() + 4u, encoded.size() - 4u, decodedSize,
                      &decoded, error, "PIDX")) {
        return false;
    }

    SchoolDaysGpkIndex parsed;
    if (!parsed.ParseDecoded(decoded.data(), decoded.size(), error)) return false;
    if (parsed.Entries().empty())
        return Fail(error, "School Days GPK PIDX contains no entries");

    std::unordered_map<std::string, size_t> foldedEntries;
    std::unordered_set<std::string> foldedCollisions;
    std::vector<std::string> canonicalEntries;
    foldedEntries.reserve(parsed.Entries().size());
    canonicalEntries.reserve(parsed.Entries().size());
    uint64_t lastPhysicalEnd = 0u;
    for (size_t i = 0u; i < parsed.Entries().size(); ++i) {
        const auto& entry = parsed.Entries()[i];
        if (entry.zero != 0u)
            return Fail(error, "School Days GPK PIDX reserved zero field is non-zero");
        const uint64_t bodySize = static_cast<uint64_t>(entry.PhysicalBodyLength());
        const uint64_t physicalEnd = static_cast<uint64_t>(entry.offset) + bodySize;
        if (physicalEnd < entry.offset || physicalEnd > pidxOffset)
            return Fail(error, "School Days GPK entry body exceeds the physical data region");
        lastPhysicalEnd = physicalEnd;

        std::string utf8;
        std::string canonical;
        std::string folded;
        if (!Utf16ToUtf8(entry.filename, &utf8) ||
            !NormalizeLogicalPath(utf8, &canonical, &folded))
            return Fail(error, "School Days GPK PIDX contains an invalid asset path");
        const auto inserted = foldedEntries.emplace(folded, i);
        if (!inserted.second) foldedCollisions.insert(std::move(folded));
        canonicalEntries.push_back(std::move(canonical));
    }
    if (lastPhysicalEnd != pidxOffset)
        return Fail(error, "School Days GPK final body does not meet the PIDX boundary");

    path_ = path;
    pidxOffset_ = pidxOffset;
    index_ = std::move(parsed);
    canonicalEntries_ = std::move(canonicalEntries);
    foldedEntries_ = std::move(foldedEntries);
    foldedCollisions_ = std::move(foldedCollisions);
    return true;
}

SchoolDaysGpkLookupStatus SchoolDaysGpkArchive::Lookup(
        std::string_view archiveRelativePath, size_t* entryIndex) const {
    if (entryIndex != nullptr) *entryIndex = std::numeric_limits<size_t>::max();
    if (!IsOpen()) return SchoolDaysGpkLookupStatus::Invalid;
    std::string folded;
    if (!FoldLogicalPath(archiveRelativePath, &folded))
        return SchoolDaysGpkLookupStatus::Invalid;
    if (foldedCollisions_.find(folded) != foldedCollisions_.end())
        return SchoolDaysGpkLookupStatus::Collision;
    const auto it = foldedEntries_.find(folded);
    if (it == foldedEntries_.end()) return SchoolDaysGpkLookupStatus::Missing;
    if (entryIndex != nullptr) *entryIndex = it->second;
    return SchoolDaysGpkLookupStatus::Present;
}

SchoolDaysGpkLookupStatus SchoolDaysGpkArchive::ListDirectory(
        std::string_view archiveRelativeDirectory,
        std::vector<std::string>* relativeChildren) const {
    if (relativeChildren == nullptr) return SchoolDaysGpkLookupStatus::Invalid;
    relativeChildren->clear();
    if (!IsOpen()) return SchoolDaysGpkLookupStatus::Invalid;

    std::string requestedFolded;
    if (!archiveRelativeDirectory.empty() &&
        !FoldLogicalPath(archiveRelativeDirectory, &requestedFolded)) {
        return SchoolDaysGpkLookupStatus::Invalid;
    }
    const std::string foldedPrefix = requestedFolded.empty()
        ? std::string() : requestedFolded + '/';

    std::string canonicalDirectory;
    std::unordered_map<std::string, std::string> children;
    for (size_t i = 0u; i < canonicalEntries_.size(); ++i) {
        std::string foldedEntry;
        if (!FoldLogicalPath(canonicalEntries_[i], &foldedEntry))
            return SchoolDaysGpkLookupStatus::Invalid;
        if (!foldedPrefix.empty() &&
            (foldedEntry.size() <= foldedPrefix.size() ||
             foldedEntry.compare(0u, foldedPrefix.size(), foldedPrefix) != 0)) {
            continue;
        }

        const size_t relativeOffset = foldedPrefix.size();
        const std::string_view foldedRemainder(foldedEntry.data() + relativeOffset,
                                               foldedEntry.size() - relativeOffset);
        const size_t childSlash = foldedRemainder.find('/');
        const std::string foldedChild(foldedRemainder.substr(0u, childSlash));

        const std::string& canonicalEntry = canonicalEntries_[i];
        size_t canonicalRelativeOffset = 0u;
        if (!requestedFolded.empty()) {
            size_t componentCount = 1u;
            componentCount += static_cast<size_t>(
                std::count(requestedFolded.begin(), requestedFolded.end(), '/'));
            size_t cursor = 0u;
            for (size_t component = 0u; component < componentCount; ++component) {
                const size_t slash = canonicalEntry.find('/', cursor);
                if (slash == std::string::npos) {
                    canonicalRelativeOffset = canonicalEntry.size();
                    break;
                }
                canonicalRelativeOffset = slash + 1u;
                cursor = slash + 1u;
            }
            if (canonicalRelativeOffset == 0u ||
                canonicalRelativeOffset > canonicalEntry.size()) {
                continue;
            }
            const std::string currentDirectory =
                canonicalEntry.substr(0u, canonicalRelativeOffset - 1u);
            if (canonicalDirectory.empty()) canonicalDirectory = currentDirectory;
            else if (canonicalDirectory != currentDirectory) {
                relativeChildren->clear();
                return SchoolDaysGpkLookupStatus::Collision;
            }
        }

        const std::string canonicalRemainder =
            canonicalEntry.substr(canonicalRelativeOffset);
        const size_t canonicalSlash = canonicalRemainder.find('/');
        const std::string canonicalChild = canonicalSlash == std::string::npos
            ? canonicalRemainder : canonicalRemainder.substr(0u, canonicalSlash);
        const std::string canonicalPath = canonicalDirectory.empty()
            ? canonicalChild : canonicalDirectory + '/' + canonicalChild;
        const auto inserted = children.emplace(foldedChild, canonicalPath);
        if (!inserted.second && inserted.first->second != canonicalPath) {
            relativeChildren->clear();
            return SchoolDaysGpkLookupStatus::Collision;
        }
    }
    if (children.empty()) return SchoolDaysGpkLookupStatus::Missing;
    relativeChildren->reserve(children.size());
    for (const auto& item : children) relativeChildren->push_back(item.second);
    std::sort(relativeChildren->begin(), relativeChildren->end());
    return SchoolDaysGpkLookupStatus::Present;
}

bool SchoolDaysGpkArchive::Read(std::string_view archiveRelativePath,
                                std::vector<uint8_t>* out,
                                std::string* error) const {
    if (out == nullptr) return Fail(error, "School Days GPK output is null");
    out->clear();
    size_t entryIndex = 0u;
    const auto status = Lookup(archiveRelativePath, &entryIndex);
    if (status != SchoolDaysGpkLookupStatus::Present)
        return Fail(error, LookupFailureMessage(status));
    return ReadEntry(entryIndex, out, error);
}

bool SchoolDaysGpkArchive::ReadEntry(size_t entryIndex, std::vector<uint8_t>* out,
                                     std::string* error) const {
    if (out == nullptr) return Fail(error, "School Days GPK output is null");
    out->clear();
    if (!IsOpen() || entryIndex >= index_.Entries().size())
        return Fail(error, "School Days GPK entry index is invalid");
    const auto& entry = index_.Entries()[entryIndex];
    const size_t bodySize = entry.PhysicalBodyLength();
    const uint64_t physicalEnd = static_cast<uint64_t>(entry.offset) + bodySize;
    if (physicalEnd < entry.offset || physicalEnd > pidxOffset_)
        return Fail(error, "School Days GPK entry body is outside the data region");

    std::ifstream file(path_, std::ios::binary);
    if (!file) return Fail(error, "School Days GPK could not be reopened for reading");
    std::vector<uint8_t> body;
    if (!ReadRange(&file, entry.offset, bodySize, &body))
        return Fail(error, "School Days GPK entry body could not be read");

    std::vector<uint8_t> stored;
    try {
        if (!SchoolDaysGpkIndex::ComposeStoredStream(
                entry, body.empty() ? nullptr : body.data(), body.size(), &stored, error)) {
            return false;
        }
    } catch (...) {
        return Fail(error, "School Days GPK stored-stream allocation failed");
    }
    if (entry.IsRaw()) {
        *out = std::move(stored);
        return true;
    }
    if (!entry.IsDeflate())
        return Fail(error, "School Days GPK entry compression is unsupported");
    return InflateExact(stored.data(), stored.size(), entry.uncompressedLength,
                        out, error, "entry");
}

void SchoolDaysGpkAssetStore::Close() {
    packDirectory_.clear();
    archives_.clear();
    foldedPrefixes_.clear();
    entryCount_ = 0u;
}

bool SchoolDaysGpkAssetStore::OpenRetailDirectory(
        const std::filesystem::path& packDirectory, std::string* error) {
    if (error != nullptr) error->clear();
    Close();
    std::error_code ec;
    if (!std::filesystem::is_directory(packDirectory, ec) || ec)
        return Fail(error, "School Days GPK Packs directory is unavailable");

    std::vector<SchoolDaysGpkArchive> archives;
    std::unordered_map<std::string, size_t> prefixes;
    archives.reserve(SchoolDaysRetailGpkSpecs().size());
    size_t entryCount = 0u;
    for (const auto& spec : SchoolDaysRetailGpkSpecs()) {
        SchoolDaysGpkArchive archive;
        std::string archiveError;
        const std::filesystem::path archivePath = packDirectory / spec.filename;
        if (!archive.Open(archivePath, &archiveError)) {
            return Fail(error, std::string("School Days GPK ") + spec.filename +
                                   " failed validation: " + archiveError);
        }
        std::string foldedPrefix;
        if (!FoldLogicalPath(spec.logicalPrefix, &foldedPrefix) ||
            !prefixes.emplace(foldedPrefix, archives.size()).second) {
            return Fail(error, "School Days GPK retail prefix table is ambiguous");
        }
        if (entryCount > std::numeric_limits<size_t>::max() - archive.EntryCount())
            return Fail(error, "School Days GPK entry census overflows size_t");
        entryCount += archive.EntryCount();
        archives.push_back(std::move(archive));
    }

    packDirectory_ = packDirectory;
    archives_ = std::move(archives);
    foldedPrefixes_ = std::move(prefixes);
    entryCount_ = entryCount;
    return true;
}

SchoolDaysGpkLookupStatus SchoolDaysGpkAssetStore::Lookup(
        std::string_view logicalPath, size_t* archiveIndex, size_t* entryIndex) const {
    if (archiveIndex != nullptr) *archiveIndex = std::numeric_limits<size_t>::max();
    if (entryIndex != nullptr) *entryIndex = std::numeric_limits<size_t>::max();
    if (!IsOpen()) return SchoolDaysGpkLookupStatus::Invalid;

    std::string folded;
    if (!FoldLogicalPath(logicalPath, &folded)) return SchoolDaysGpkLookupStatus::Invalid;
    const size_t slash = folded.find('/');
    if (slash == std::string::npos || slash + 1u >= folded.size())
        return SchoolDaysGpkLookupStatus::Invalid;
    const auto prefix = foldedPrefixes_.find(folded.substr(0u, slash));
    if (prefix == foldedPrefixes_.end()) return SchoolDaysGpkLookupStatus::Missing;
    const size_t archive = prefix->second;
    size_t entry = 0u;
    const auto status = archives_[archive].Lookup(folded.substr(slash + 1u), &entry);
    if (status != SchoolDaysGpkLookupStatus::Present) return status;
    if (archiveIndex != nullptr) *archiveIndex = archive;
    if (entryIndex != nullptr) *entryIndex = entry;
    return SchoolDaysGpkLookupStatus::Present;
}

SchoolDaysGpkLookupStatus SchoolDaysGpkAssetStore::ListDirectory(
        std::string_view logicalDirectory,
        std::vector<std::string>* logicalChildren) const {
    if (logicalChildren == nullptr) return SchoolDaysGpkLookupStatus::Invalid;
    logicalChildren->clear();
    if (!IsOpen()) return SchoolDaysGpkLookupStatus::Invalid;

    std::string canonical;
    std::string folded;
    if (!NormalizeLogicalPath(logicalDirectory, &canonical, &folded))
        return SchoolDaysGpkLookupStatus::Invalid;
    const size_t slash = folded.find('/');
    const std::string foldedPack = slash == std::string::npos
        ? folded : folded.substr(0u, slash);
    const auto prefix = foldedPrefixes_.find(foldedPack);
    if (prefix == foldedPrefixes_.end()) return SchoolDaysGpkLookupStatus::Missing;

    const size_t archiveIndex = prefix->second;
    const std::string_view relative = slash == std::string::npos
        ? std::string_view() : std::string_view(canonical).substr(slash + 1u);
    std::vector<std::string> relativeChildren;
    const auto status = archives_[archiveIndex].ListDirectory(relative, &relativeChildren);
    if (status != SchoolDaysGpkLookupStatus::Present) return status;

    const auto& spec = SchoolDaysRetailGpkSpecs()[archiveIndex];
    logicalChildren->reserve(relativeChildren.size());
    for (std::string& child : relativeChildren)
        logicalChildren->push_back(std::string(spec.logicalPrefix) + '/' + child);
    return SchoolDaysGpkLookupStatus::Present;
}

bool SchoolDaysGpkAssetStore::Read(std::string_view logicalPath,
                                   std::vector<uint8_t>* out,
                                   std::string* error) const {
    if (out == nullptr) return Fail(error, "School Days GPK output is null");
    out->clear();
    size_t archive = 0u;
    size_t entry = 0u;
    const auto status = Lookup(logicalPath, &archive, &entry);
    if (status != SchoolDaysGpkLookupStatus::Present)
        return Fail(error, LookupFailureMessage(status));
    return archives_[archive].ReadEntry(entry, out, error);
}

} // namespace Kotonoha
