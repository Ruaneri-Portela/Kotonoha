#pragma once

#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Kotonoha {

// Filesystem/cache owner for the authenticated v1.02 [ConfigFile]="/Config.DAT"
// provider.  Native UI writes mutate the provider cache immediately; the
// physical DFLT file is flushed later during orderly application shutdown.
class SchoolDaysConfigStorage {
public:
    struct StartupDisplay {
        bool hasDisplayType = false;
        int displayType = 0;
        bool hasTypeMiniNote = false;
        int typeMiniNote = 0;
        bool hasWindowMode = false;
        int windowMode = 0;
    };

    struct LoadResult {
        SchoolDaysRetailConfigPolicy::State state =
            SchoolDaysRetailConfigPolicy::Defaults();
        StartupDisplay display;
        bool present = false;
    };

    bool Initialize(const std::filesystem::path& executableDirectory,
                    std::string* error = nullptr);
    bool Load(LoadResult* out, std::string* error = nullptr);

    // Mirrors authenticated SysMenu cache mutation for fields whose live/load
    // semantics are closed. MasterVolume remains opaque until its load-back
    // and mutation contract is authenticated. No disk I/O occurs.
    bool CacheState(const SchoolDaysRetailConfigPolicy::State& state,
                    std::string* error = nullptr);

    // DisplayType and TypeMiniNote have authenticated native config writers.
    // WindowMode is deliberately absent: static v1.02 evidence has a reader but
    // no writer for that key, so Full remains session-only at this boundary.
    bool CacheDisplayType(int value, std::string* error = nullptr);
    bool CacheTypeMiniNote(int value, std::string* error = nullptr);

    // Writes DFLT + zlib only when the cache is dirty.  Unknown/malformed lines
    // from a loaded document are preserved in order.
    bool Flush(std::string* error = nullptr);

    bool IsInitialized() const { return initialized_; }
    bool IsLoaded() const { return loaded_; }
    bool IsDirty() const { return dirty_; }
    const std::filesystem::path& Path() const { return path_; }

    // Test/research helpers for the pure DFLT payload boundary.
    static bool DecodeDocument(const std::vector<uint8_t>& bytes,
                               std::string* text,
                               std::string* error = nullptr);
    static bool EncodeDocument(const std::string& text,
                               std::vector<uint8_t>* bytes,
                               std::string* error = nullptr);

private:
    bool ParseLoadedDocument(const std::string& text, LoadResult* out,
                             std::string* error);
    bool SetKeyValue(const std::string& key, const std::string& value,
                     std::string* error);
    bool SerializeLogical(std::string* out, std::string* error) const;

    std::filesystem::path path_;
    std::vector<std::string> lines_;
    bool trailingLf_ = true;
    bool initialized_ = false;
    bool loaded_ = false;
    bool dirty_ = false;
};

} // namespace Kotonoha
