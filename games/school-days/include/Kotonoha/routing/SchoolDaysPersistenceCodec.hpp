#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

struct SchoolDaysFlgHEntry {
    std::u16string key;
    int32_t identifier = -1;
    int32_t type = 3;
    int32_t integer = 0;
    uint32_t floatBits = 0u;
    std::u16string stringValue;
};

struct SchoolDaysFlgH {
    std::vector<SchoolDaysFlgHEntry> entries;
};

struct SchoolDaysSLogRecord {
    int32_t tag = 0;
    std::u16string name;

    // Tag 1: exact IEEE-754 bits for the saved route/script version.
    uint32_t floatBits = 0u;

    // Tag 3: secondary name + integer + FlgH snapshot.
    std::u16string secondary;
    int32_t integer = 0;

    // Tags 1 and 3.
    SchoolDaysFlgH flags;
};

struct SchoolDaysSLog {
    std::vector<SchoolDaysSLogRecord> records;
};

class SchoolDaysPersistenceCodec {
public:
    static bool ParseFlgH(const std::vector<uint8_t>& bytes,
                          SchoolDaysFlgH* out, std::string* error);
    static bool SerializeFlgH(const SchoolDaysFlgH& node,
                              std::vector<uint8_t>* out, std::string* error);

    static bool ParseSLog(const std::vector<uint8_t>& bytes,
                          SchoolDaysSLog* out, std::string* error);
    static bool SerializeSLog(const SchoolDaysSLog& node,
                              std::vector<uint8_t>* out, std::string* error);
};

} // namespace Kotonoha
