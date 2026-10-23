#include <Kotonoha/SchoolDaysGpkIndex.hpp>

#include <limits>
#include <utility>

namespace Kotonoha {
namespace {

bool Fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
    return false;
}

bool Read16(const uint8_t* bytes, size_t size, size_t* cursor,
            uint16_t* out) {
    if (bytes == nullptr || cursor == nullptr || out == nullptr ||
        *cursor > size || size - *cursor < 2u) return false;
    const size_t p = *cursor;
    *out = static_cast<uint16_t>(bytes[p]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[p + 1u]) << 8u);
    *cursor += 2u;
    return true;
}

bool Read32(const uint8_t* bytes, size_t size, size_t* cursor,
            uint32_t* out) {
    if (bytes == nullptr || cursor == nullptr || out == nullptr ||
        *cursor > size || size - *cursor < 4u) return false;
    const size_t p = *cursor;
    *out = static_cast<uint32_t>(bytes[p]) |
           (static_cast<uint32_t>(bytes[p + 1u]) << 8u) |
           (static_cast<uint32_t>(bytes[p + 2u]) << 16u) |
           (static_cast<uint32_t>(bytes[p + 3u]) << 24u);
    *cursor += 4u;
    return true;
}

} // namespace

void SchoolDaysGpkIndex::Clear() { entries_.clear(); }

bool SchoolDaysGpkIndex::ParseDecoded(const uint8_t* bytes, size_t size,
                                      std::string* error) {
    Clear();
    if (error != nullptr) error->clear();
    if (bytes == nullptr || size < 4u)
        return Fail(error, "School Days GPK PIDX is empty or truncated");

    size_t cursor = 0u;
    bool sawTerminator = false;
    while (cursor < size) {
        uint16_t nameUnits = 0;
        if (!Read16(bytes, size, &cursor, &nameUnits)) {
            Clear();
            return Fail(error, "School Days GPK PIDX filename length is truncated");
        }
        if (nameUnits == 0u) {
            // All 29 shipped v1.02 indexes end with uint16(0), uint16(0).
            if (size - cursor != 2u || bytes[cursor] != 0u ||
                bytes[cursor + 1u] != 0u) {
                Clear();
                return Fail(error, "School Days GPK PIDX terminator is malformed");
            }
            cursor += 2u;
            sawTerminator = true;
            break;
        }

        const size_t nameBytes = static_cast<size_t>(nameUnits) * 2u;
        if (nameBytes / 2u != nameUnits || cursor > size ||
            size - cursor < nameBytes + 23u) {
            Clear();
            return Fail(error, "School Days GPK PIDX entry is truncated");
        }

        Entry entry;
        entry.filename.reserve(nameUnits);
        for (uint16_t i = 0; i < nameUnits; ++i) {
            uint16_t unit = 0;
            if (!Read16(bytes, size, &cursor, &unit)) {
                Clear();
                return Fail(error, "School Days GPK PIDX UTF-16 filename is truncated");
            }
            entry.filename.push_back(static_cast<char16_t>(unit));
        }

        if (!Read16(bytes, size, &cursor, &entry.subVersion) ||
            !Read16(bytes, size, &cursor, &entry.version) ||
            !Read16(bytes, size, &cursor, &entry.zero) ||
            !Read32(bytes, size, &cursor, &entry.offset) ||
            !Read32(bytes, size, &cursor, &entry.storedLength)) {
            Clear();
            return Fail(error, "School Days GPK PIDX numeric header is truncated");
        }
        if (size - cursor < 4u) {
            Clear();
            return Fail(error, "School Days GPK PIDX magic is truncated");
        }
        for (size_t i = 0; i < 4u; ++i)
            entry.magic[i] = static_cast<char>(bytes[cursor + i]);
        cursor += 4u;
        if (!Read32(bytes, size, &cursor, &entry.uncompressedLength) ||
            cursor >= size) {
            Clear();
            return Fail(error, "School Days GPK PIDX length/prefix field is truncated");
        }

        const uint8_t prefixLength = bytes[cursor++];
        if (entry.storedLength < prefixLength || size - cursor < prefixLength) {
            Clear();
            return Fail(error, "School Days GPK PIDX inline prefix exceeds entry bounds");
        }
        if (!entry.IsRaw() && !entry.IsDeflate()) {
            Clear();
            return Fail(error, "School Days GPK PIDX entry magic is unsupported");
        }
        entry.inlinePrefix.assign(bytes + cursor, bytes + cursor + prefixLength);
        cursor += prefixLength;

        if (!entries_.empty()) {
            const Entry& previous = entries_.back();
            const uint64_t expected = static_cast<uint64_t>(previous.offset) +
                                      previous.PhysicalBodyLength();
            if (expected > std::numeric_limits<uint32_t>::max() ||
                entry.offset != static_cast<uint32_t>(expected)) {
                Clear();
                return Fail(error, "School Days GPK PIDX physical offset chain is inconsistent");
            }
        }
        entries_.push_back(std::move(entry));
    }

    if (!sawTerminator || cursor != size) {
        Clear();
        return Fail(error, "School Days GPK PIDX terminator is missing");
    }
    return true;
}

bool SchoolDaysGpkIndex::ComposeStoredStream(
    const Entry& entry, const uint8_t* physicalBody, size_t physicalBodySize,
    std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr) return Fail(error, "School Days GPK output is null");
    out->clear();
    const size_t expected = entry.PhysicalBodyLength();
    if (physicalBodySize != expected || (expected != 0u && physicalBody == nullptr))
        return Fail(error, "School Days GPK physical body length disagrees with PIDX");
    out->reserve(static_cast<size_t>(entry.storedLength));
    out->insert(out->end(), entry.inlinePrefix.begin(), entry.inlinePrefix.end());
    if (expected != 0u)
        out->insert(out->end(), physicalBody, physicalBody + expected);
    if (out->size() != entry.storedLength) {
        out->clear();
        return Fail(error, "School Days GPK logical stored stream length drifted");
    }
    return true;
}

} // namespace Kotonoha
