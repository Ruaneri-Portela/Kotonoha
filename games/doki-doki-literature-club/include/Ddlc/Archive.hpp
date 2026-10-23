#pragma once
// Ren'Py 6.99 containers: RPA-3.0 archives, pickled data and compiled scripts (.rpyc).
#include "Ddlc/Value.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace Ddlc {

// Python pickle (protocol 2) to Value. Objects keep their identity (memo).
Value unpickle(const uint8_t* data, size_t size);
inline Value unpickle(const std::vector<uint8_t>& d) { return unpickle(d.data(), d.size()); }

bool inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

class Archive {
public:
    Archive() = default;
    ~Archive();
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool open(const std::string& path);
    bool has(const std::string& name) const;
    std::vector<uint8_t> read(const std::string& name) const;
    std::vector<std::string> names() const;

private:
    struct Entry {
        uint64_t offset = 0, length = 0;
        std::string prefix;
    };
    std::FILE* file_ = nullptr;
    std::map<std::string, Entry> index_; // lower-case name
};

// Every archive of a game directory plus its loose files, looked up the way
// Ren'Py does (case-insensitive, "images/" prefix for image files).
class Assets {
public:
    bool open(const std::string& gameDir);
    const std::string& dir() const { return dir_; }
    bool has(const std::string& name) const;
    std::vector<uint8_t> read(const std::string& name) const;
    // Image lookup: name, then images/name.
    std::string resolveImage(const std::string& name) const;
    std::vector<std::string> names() const;

private:
    std::string dir_;
    std::vector<std::unique_ptr<Archive>> archives_;
};

// .rpyc: RENPY RPC2 slot 1 holds (data, [statements]).
bool loadRpyc(const std::vector<uint8_t>& file, Value& out);

} // namespace Ddlc
