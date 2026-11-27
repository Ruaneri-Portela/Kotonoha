#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

// Pure parser for the already XOR-decoded/zlib-expanded v1.02 STKFile0PIDX
// payload.  Envelope/XOR/zlib handling intentionally stays outside this type so
// the framing contract adds no desktop compression dependency by itself.
class SchoolDaysGpkIndex final {
public:
    struct Entry {
        std::u16string filename;
        uint16_t subVersion = 0;
        uint16_t version = 0;
        uint16_t zero = 0;
        uint32_t offset = 0;
        uint32_t storedLength = 0;
        std::array<char, 4> magic{};
        uint32_t uncompressedLength = 0;
        std::vector<uint8_t> inlinePrefix;

        size_t PhysicalBodyLength() const {
            return storedLength >= inlinePrefix.size()
                       ? static_cast<size_t>(storedLength) - inlinePrefix.size()
                       : 0u;
        }
        bool IsDeflate() const {
            return magic == std::array<char,4>{'D','F','L','T'};
        }
        bool IsRaw() const {
            return magic == std::array<char,4>{' ',' ',' ',' '};
        }
    };

    bool ParseDecoded(const uint8_t* bytes, size_t size,
                      std::string* error = nullptr);
    void Clear();

    const std::vector<Entry>& Entries() const { return entries_; }

    // Reconstruct the exact stored logical stream before optional DFLT/zlib
    // decode. physicalBody must be exactly storedLength-inlinePrefix bytes.
    static bool ComposeStoredStream(const Entry& entry,
                                    const uint8_t* physicalBody,
                                    size_t physicalBodySize,
                                    std::vector<uint8_t>* out,
                                    std::string* error = nullptr);

private:
    std::vector<Entry> entries_;
};

} // namespace Kotonoha
