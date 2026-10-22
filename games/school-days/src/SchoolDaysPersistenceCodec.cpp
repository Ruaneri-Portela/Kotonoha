#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>

#include <cstring>
#include <limits>

namespace Kotonoha {
namespace {

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool Take(size_t count, const uint8_t** data, std::string* error) {
        if (data == nullptr || count > bytes_.size() - offset_) {
            return Fail(error, "truncated School Days persistence input");
        }
        *data = bytes_.data() + offset_;
        offset_ += count;
        return true;
    }

    size_t Offset() const { return offset_; }
    size_t Size() const { return bytes_.size(); }

private:
    static bool Fail(std::string* error, const char* message) {
        if (error != nullptr) *error = message;
        return false;
    }

    const std::vector<uint8_t>& bytes_;
    size_t offset_ = 0u;
};

bool Fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
    return false;
}

bool ReadVarint(Reader* reader, int32_t* out, std::string* error) {
    if (reader == nullptr || out == nullptr) {
        return Fail(error, "invalid School Days varint argument");
    }
    const uint8_t* raw = nullptr;
    if (!reader->Take(1u, &raw, error)) return false;
    uint8_t current = raw[0];
    const bool negative = (current & 0x40u) != 0u;
    uint32_t value = current & 0x3fu;
    unsigned groups = 1u;
    while ((current & 0x80u) != 0u) {
        if (groups >= 5u) {
            return Fail(error, "School Days varint exceeds canonical 5-byte domain");
        }
        if (!reader->Take(1u, &raw, error)) return false;
        current = raw[0];
        value = (value << 7u) | static_cast<uint32_t>(current & 0x7fu);
        ++groups;
    }

    if (negative) {
        *out = static_cast<int32_t>(~value);
        return true;
    }
    if (value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return Fail(error, "positive School Days varint exceeds signed 32-bit range");
    }
    *out = static_cast<int32_t>(value);
    return true;
}

bool WriteVarint(int32_t value, std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr) return Fail(error, "School Days varint output is null");
    const bool negative = value < 0;
    const uint32_t raw = static_cast<uint32_t>(value);
    const uint32_t magnitude = negative ? ~raw : raw;
    const uint8_t sign = negative ? 0x40u : 0u;

    if (magnitude < 0x40u) {
        out->push_back(static_cast<uint8_t>((magnitude & 0x3fu) | sign));
    }
    else if (magnitude < 0x2000u) {
        out->push_back(static_cast<uint8_t>(((magnitude >> 7u) & 0x3fu) | 0x80u | sign));
        out->push_back(static_cast<uint8_t>(magnitude & 0x7fu));
    }
    else if (magnitude < 0x100000u) {
        out->push_back(static_cast<uint8_t>(((magnitude >> 14u) & 0x3fu) | 0x80u | sign));
        out->push_back(static_cast<uint8_t>(((magnitude >> 7u) & 0x7fu) | 0x80u));
        out->push_back(static_cast<uint8_t>(magnitude & 0x7fu));
    }
    else if (magnitude < 0x8000000u) {
        out->push_back(static_cast<uint8_t>(((magnitude >> 21u) & 0x3fu) | 0x80u | sign));
        out->push_back(static_cast<uint8_t>(((magnitude >> 14u) & 0x7fu) | 0x80u));
        out->push_back(static_cast<uint8_t>(((magnitude >> 7u) & 0x7fu) | 0x80u));
        out->push_back(static_cast<uint8_t>(magnitude & 0x7fu));
    }
    else {
        out->push_back(static_cast<uint8_t>(((magnitude >> 28u) & 0x3fu) | 0x80u | sign));
        out->push_back(static_cast<uint8_t>(((magnitude >> 21u) & 0x7fu) | 0x80u));
        out->push_back(static_cast<uint8_t>(((magnitude >> 14u) & 0x7fu) | 0x80u));
        out->push_back(static_cast<uint8_t>(((magnitude >> 7u) & 0x7fu) | 0x80u));
        out->push_back(static_cast<uint8_t>(magnitude & 0x7fu));
    }
    return true;
}

bool ReadU32LE(Reader* reader, uint32_t* out, std::string* error) {
    const uint8_t* raw = nullptr;
    if (reader == nullptr || out == nullptr || !reader->Take(4u, &raw, error)) {
        return false;
    }
    *out = static_cast<uint32_t>(raw[0]) |
           (static_cast<uint32_t>(raw[1]) << 8u) |
           (static_cast<uint32_t>(raw[2]) << 16u) |
           (static_cast<uint32_t>(raw[3]) << 24u);
    return true;
}

void WriteU32LE(uint32_t value, std::vector<uint8_t>* out) {
    out->push_back(static_cast<uint8_t>(value & 0xffu));
    out->push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    out->push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
    out->push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

bool ReadXorString(Reader* reader, size_t capacity, std::u16string* out,
                   std::string* error) {
    if (reader == nullptr || out == nullptr) {
        return Fail(error, "invalid School Days string argument");
    }
    int32_t length = 0;
    if (!ReadVarint(reader, &length, error)) return false;
    if (length < 0 || static_cast<size_t>(length) >= capacity) {
        return Fail(error, "School Days string length exceeds reader capacity");
    }
    const uint8_t* raw = nullptr;
    const size_t units = static_cast<size_t>(length);
    if (!reader->Take(units * 2u, &raw, error)) return false;
    out->clear();
    out->reserve(units);
    for (size_t i = 0u; i < units; ++i) {
        const uint16_t encoded = static_cast<uint16_t>(raw[i * 2u]) |
                                 static_cast<uint16_t>(raw[i * 2u + 1u] << 8u);
        out->push_back(static_cast<char16_t>(encoded ^ static_cast<uint16_t>(i)));
    }
    return true;
}

bool WriteXorString(const std::u16string& value, size_t capacity,
                    std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr) return Fail(error, "School Days string output is null");
    if (value.size() >= capacity ||
        value.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return Fail(error, "School Days string exceeds writer capacity");
    }
    if (!WriteVarint(static_cast<int32_t>(value.size()), out, error)) return false;
    for (size_t i = 0u; i < value.size(); ++i) {
        const uint16_t encoded = static_cast<uint16_t>(value[i]) ^
                                 static_cast<uint16_t>(i);
        out->push_back(static_cast<uint8_t>(encoded & 0xffu));
        out->push_back(static_cast<uint8_t>((encoded >> 8u) & 0xffu));
    }
    return true;
}

bool ReadMagic(Reader* reader, const char expected[5], std::string* error) {
    const uint8_t* raw = nullptr;
    if (!reader->Take(4u, &raw, error)) return false;
    if (std::memcmp(raw, expected, 4u) != 0) {
        return Fail(error, "School Days persistence magic mismatch");
    }
    return true;
}

void WriteMagic(const char magic[5], std::vector<uint8_t>* out) {
    out->insert(out->end(), magic, magic + 4u);
}

bool ParseFlgHNode(Reader* reader, SchoolDaysFlgH* out, std::string* error) {
    if (reader == nullptr || out == nullptr) return Fail(error, "invalid FlgH argument");
    if (!ReadMagic(reader, "FlgH", error)) return false;
    int32_t count = 0;
    if (!ReadVarint(reader, &count, error)) return false;
    if (count < 0) return Fail(error, "negative FlgH entry count");

    out->entries.clear();
    out->entries.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        SchoolDaysFlgHEntry entry;
        if (!ReadXorString(reader, 0x400u, &entry.key, error) ||
            !ReadVarint(reader, &entry.identifier, error) ||
            !ReadVarint(reader, &entry.type, error)) {
            return false;
        }
        switch (entry.type) {
        case 3:
        case 11:
            if (!ReadVarint(reader, &entry.integer, error)) return false;
            break;
        case 4:
            if (!ReadU32LE(reader, &entry.floatBits, error)) return false;
            break;
        case 8:
            if (!ReadXorString(reader, 0x400u, &entry.stringValue, error)) return false;
            break;
        default:
            return Fail(error, "unsupported FlgH value type");
        }
        out->entries.push_back(std::move(entry));
    }
    return true;
}

bool SerializeFlgHNode(const SchoolDaysFlgH& node, std::vector<uint8_t>* out,
                       std::string* error) {
    if (out == nullptr) return Fail(error, "FlgH output is null");
    if (node.entries.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return Fail(error, "FlgH entry count exceeds writer range");
    }
    WriteMagic("FlgH", out);
    if (!WriteVarint(static_cast<int32_t>(node.entries.size()), out, error)) return false;
    for (const SchoolDaysFlgHEntry& entry : node.entries) {
        const int32_t serializedType =
            (entry.type == 3 || entry.type == 4 || entry.type == 8 ||
             entry.type == 11)
                ? entry.type
                : 3;
        if (!WriteXorString(entry.key, 0x400u, out, error) ||
            !WriteVarint(entry.identifier, out, error) ||
            !WriteVarint(serializedType, out, error)) {
            return false;
        }
        switch (serializedType) {
        case 3:
            if (!WriteVarint(entry.type == 3 ? entry.integer : 0, out, error))
                return false;
            break;
        case 11: {
            // EXE 0x0045C890 reads the stored VT_BOOL payload as an int16,
            // sign-extends it, then serializes the promoted value.  Preserve
            // that writer behavior for programmatic values outside int16.
            const int32_t promoted = static_cast<int32_t>(
                static_cast<int16_t>(entry.integer));
            if (!WriteVarint(promoted, out, error)) return false;
            break;
        }
        case 4:
            WriteU32LE(entry.floatBits, out);
            break;
        case 8:
            if (!WriteXorString(entry.stringValue, 0x400u, out, error)) return false;
            break;
        }
    }
    return true;
}

} // namespace

bool SchoolDaysPersistenceCodec::ParseFlgH(const std::vector<uint8_t>& bytes,
                                           SchoolDaysFlgH* out,
                                           std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) return Fail(error, "FlgH output is null");
    Reader reader(bytes);
    SchoolDaysFlgH parsed;
    if (!ParseFlgHNode(&reader, &parsed, error)) return false;
    if (reader.Offset() != reader.Size()) {
        return Fail(error, "trailing bytes after FlgH payload");
    }
    *out = std::move(parsed);
    return true;
}

bool SchoolDaysPersistenceCodec::SerializeFlgH(const SchoolDaysFlgH& node,
                                               std::vector<uint8_t>* out,
                                               std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) return Fail(error, "FlgH output is null");
    std::vector<uint8_t> encoded;
    if (!SerializeFlgHNode(node, &encoded, error)) return false;
    *out = std::move(encoded);
    return true;
}

bool SchoolDaysPersistenceCodec::ParseSLog(const std::vector<uint8_t>& bytes,
                                           SchoolDaysSLog* out,
                                           std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) return Fail(error, "SLog output is null");
    Reader reader(bytes);
    if (!ReadMagic(&reader, "SLog", error)) return false;

    SchoolDaysSLog parsed;
    while (true) {
        int32_t tag = 0;
        if (!ReadVarint(&reader, &tag, error)) return false;
        if (tag == 0) break;

        SchoolDaysSLogRecord record;
        record.tag = tag;
        switch (tag) {
        case 1:
            if (!ReadXorString(&reader, 0x400u, &record.name, error) ||
                !ReadU32LE(&reader, &record.floatBits, error) ||
                !ParseFlgHNode(&reader, &record.flags, error)) {
                return false;
            }
            break;
        case 3:
            if (!ReadXorString(&reader, 0x400u, &record.name, error) ||
                !ReadXorString(&reader, 0x40u, &record.secondary, error) ||
                !ReadVarint(&reader, &record.integer, error) ||
                !ParseFlgHNode(&reader, &record.flags, error)) {
                return false;
            }
            break;
        case 4:
            if (!ReadXorString(&reader, 0x400u, &record.name, error) ||
                !ReadVarint(&reader, &record.integer, error)) {
                return false;
            }
            break;
        default:
            return Fail(error, "undefined SLog tag");
        }
        parsed.records.push_back(std::move(record));
    }
    if (reader.Offset() != reader.Size()) {
        return Fail(error, "trailing bytes after SLog terminator");
    }
    *out = std::move(parsed);
    return true;
}

bool SchoolDaysPersistenceCodec::SerializeSLog(const SchoolDaysSLog& node,
                                               std::vector<uint8_t>* out,
                                               std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) return Fail(error, "SLog output is null");
    std::vector<uint8_t> encoded;
    WriteMagic("SLog", &encoded);
    for (const SchoolDaysSLogRecord& record : node.records) {
        if (!WriteVarint(record.tag, &encoded, error)) return false;
        switch (record.tag) {
        case 1:
            if (!WriteXorString(record.name, 0x400u, &encoded, error)) return false;
            WriteU32LE(record.floatBits, &encoded);
            if (!SerializeFlgHNode(record.flags, &encoded, error)) return false;
            break;
        case 3:
            if (!WriteXorString(record.name, 0x400u, &encoded, error) ||
                !WriteXorString(record.secondary, 0x40u, &encoded, error) ||
                !WriteVarint(record.integer, &encoded, error) ||
                !SerializeFlgHNode(record.flags, &encoded, error)) {
                return false;
            }
            break;
        case 4:
            if (!WriteXorString(record.name, 0x400u, &encoded, error) ||
                !WriteVarint(record.integer, &encoded, error)) {
                return false;
            }
            break;
        default:
            return Fail(error, "undefined SLog tag");
        }
    }
    if (!WriteVarint(0, &encoded, error)) return false;
    *out = std::move(encoded);
    return true;
}

} // namespace Kotonoha
