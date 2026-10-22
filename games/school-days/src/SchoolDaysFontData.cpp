#include <Kotonoha/routing/SchoolDaysFontData.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <limits>

namespace Kotonoha {
namespace {

bool Fail(std::string* error, const char* text) {
    if (error != nullptr) *error = text != nullptr ? text : "School Days FONTDATA error";
    return false;
}

uint32_t ReadU32Le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

bool FindRecordEnd(const std::vector<uint8_t>& bytes, uint32_t start,
                   uint32_t limit, uint32_t* outEnd, std::string* error) {
    if (outEnd == nullptr || start >= limit || limit > bytes.size()) {
        return Fail(error, "School Days FONTDATA glyph record bounds are invalid");
    }
    uint32_t cursor = 0u;
    uint32_t pos = start;
    while (pos < limit) {
        const uint8_t control = bytes[pos++];
        if (control == 0u) {
            *outEnd = pos;
            return true;
        }
        if (control <= 0x7fu) {
            if (cursor + control >
                static_cast<uint32_t>(SchoolDaysFontData::kGlyphPixels)) {
                return Fail(error,
                            "School Days FONTDATA transparent run exceeds glyph bounds");
            }
            cursor += control;
            continue;
        }
        if (pos >= limit) {
            return Fail(error, "School Days FONTDATA visible run lacks descriptor");
        }
        const uint8_t descriptor = bytes[pos++];
        const uint32_t run = static_cast<uint32_t>(descriptor >> 4u) + 1u;
        if (cursor + run >
            static_cast<uint32_t>(SchoolDaysFontData::kGlyphPixels)) {
            return Fail(error,
                        "School Days FONTDATA visible run exceeds glyph bounds");
        }
        cursor += run;
    }
    return Fail(error, "School Days FONTDATA glyph record lacks final terminator");
}

} // namespace

void SchoolDaysFontData::Clear() {
    bytes_.clear();
    offsets_.clear();
    recordEnds_.clear();
    nonzeroGlyphs_ = 0u;
}

bool SchoolDaysFontData::LoadFile(const std::string& path, std::string* error) {
    if (error != nullptr) error->clear();
    std::vector<uint8_t> bytes;
    if (!ReadSchoolDaysAssetReference(path, &bytes, error)) return false;
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
        return Fail(error, "School Days FONTDATA size is outside original offset range");
    return Parse(std::move(bytes), error);
}

bool SchoolDaysFontData::Parse(std::vector<uint8_t> bytes, std::string* error) {
    if (error != nullptr) error->clear();
    Clear();
    if (bytes.size() < kTableBytes ||
        bytes.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return Fail(error, "School Days FONTDATA lacks the complete offset table");
    }
    offsets_.assign(kGlyphCount, 0u);
    recordEnds_.assign(kGlyphCount, 0u);

    uint32_t previous = 0u;
    std::vector<uint32_t> present;
    present.reserve(kGlyphCount);
    for (uint32_t glyph = 0u; glyph < kGlyphCount; ++glyph) {
        const uint32_t offset = ReadU32Le(bytes.data() + glyph * 4u);
        offsets_[glyph] = offset;
        if (offset == 0u) continue;
        if (offset < kTableBytes || offset >= bytes.size()) {
            Clear();
            return Fail(error, "School Days FONTDATA glyph offset is outside payload");
        }
        if (previous != 0u && offset <= previous) {
            Clear();
            return Fail(error, "School Days FONTDATA nonzero offsets are not strictly increasing");
        }
        previous = offset;
        present.push_back(glyph);
    }

    for (size_t i = 0u; i < present.size(); ++i) {
        const uint32_t glyph = present[i];
        const uint32_t start = offsets_[glyph];
        const uint32_t limit = i + 1u < present.size()
            ? offsets_[present[i + 1u]]
            : static_cast<uint32_t>(bytes.size());
        uint32_t end = 0u;
        if (!FindRecordEnd(bytes, start, limit, &end, error)) {
            Clear();
            return false;
        }
        // Every indexed record except the final one consumes exactly the span
        // up to the next nonzero offset.  Some historical extracted corpus
        // copies carry a tiny trailer after the final glyph; the native decoder
        // stops at that glyph's zero terminator, so those bytes are not part of
        // the logical FONTDATA resource.
        if (i + 1u < present.size() && end != limit) {
            Clear();
            return Fail(error,
                        "School Days FONTDATA glyph terminates before next offset");
        }
        recordEnds_[glyph] = end;
    }

    if (!present.empty()) {
        const uint32_t logicalEnd = recordEnds_[present.back()];
        if (logicalEnd < bytes.size()) bytes.resize(logicalEnd);
    }

    bytes_ = std::move(bytes);
    nonzeroGlyphs_ = present.size();

    // Validate every shipped record once.  The native decoder trusts its data;
    // Kotonoha rejects malformed assets rather than allowing an RLE overrun.
    Glyph scratch;
    for (uint32_t glyph : present) {
        if (!DecodeRecord(offsets_[glyph], recordEnds_[glyph], &scratch, error)) {
            Clear();
            return false;
        }
    }
    return true;
}

bool SchoolDaysFontData::DecodeRecord(uint32_t start, uint32_t end, Glyph* out,
                                      std::string* error) const {
    if (out == nullptr || start < kTableBytes || end <= start || end > bytes_.size()) {
        return Fail(error, "School Days FONTDATA glyph record arguments are invalid");
    }
    out->gray.fill(0u);
    out->alpha.fill(0u);
    uint32_t cursor = 0u;
    uint32_t pos = start;
    while (pos < end) {
        const uint8_t control = bytes_[pos++];
        if (control == 0u) {
            if (pos != end) {
                return Fail(error, "School Days FONTDATA glyph terminates before record end");
            }
            return true;
        }
        if (control <= 0x7fu) {
            if (cursor + control > static_cast<uint32_t>(kGlyphPixels)) {
                return Fail(error, "School Days FONTDATA transparent run exceeds glyph bounds");
            }
            cursor += control;
            continue;
        }
        if (pos >= end) {
            return Fail(error, "School Days FONTDATA visible run lacks descriptor");
        }
        const uint8_t descriptor = bytes_[pos++];
        const uint32_t run = static_cast<uint32_t>(descriptor >> 4u) + 1u;
        if (cursor + run > static_cast<uint32_t>(kGlyphPixels)) {
            return Fail(error, "School Days FONTDATA visible run exceeds glyph bounds");
        }
        const uint8_t gray = static_cast<uint8_t>((control - 0x80u) * 2u);
        const uint8_t alpha = static_cast<uint8_t>((descriptor & 0x0fu) * 0x11u);
        for (uint32_t i = 0u; i < run; ++i) {
            out->gray[cursor] = gray;
            out->alpha[cursor] = alpha;
            ++cursor;
        }
    }
    return Fail(error, "School Days FONTDATA glyph record lacks terminator");
}

bool SchoolDaysFontData::DecodeGlyph(uint16_t glyph, Glyph* out,
                                     std::string* error) const {
    if (error != nullptr) error->clear();
    if (out == nullptr || bytes_.empty()) {
        return Fail(error, "School Days FONTDATA decoder is not ready");
    }
    const uint32_t start = offsets_[glyph];
    if (start == 0u) {
        out->gray.fill(0u);
        out->alpha.fill(0u);
        return true;
    }
    return DecodeRecord(start, recordEnds_[glyph], out, error);
}

} // namespace Kotonoha
