#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysConfigStorageTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) {
    if (!value) Fail(message);
}
std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}
bool Read(const fs::path& path, std::vector<uint8_t>* bytes) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto end = file.tellg();
    if (end < 0) return false;
    bytes->resize(static_cast<size_t>(end));
    file.seekg(0);
    return bytes->empty() ||
           static_cast<bool>(file.read(reinterpret_cast<char*>(bytes->data()),
                                       static_cast<std::streamsize>(bytes->size())));
}
} // namespace

int main(int argc, char** argv) {
    using Storage = Kotonoha::SchoolDaysConfigStorage;
    using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
    std::string error;

    const std::string logical =
        "< Config.dat >\n"
        "[DisplayType]=\"1\"\n"
        "[TypeMiniNote]=\"0\"\n"
        "[WindowMode]=\"1\"\n"
        "[MasterVolume]=\"-7.000000\"\n"
        "[VoiceVolume]=\"8\"\n"
        "[BgmVolume]=\"5\"\n"
        "\n"
        "SeVolume]=\"9\"\n"
        "[TextView]=\"-1\"\n"
        "MenVoice]=\"0\"\n"
        "[Mute]=\"0\"\n"
        "[Skip]=\"-2\"\n"
        "[AutoDraw]=\"-1\"\n"
        "SuperSkip]=\"1\"\n"
        "[UseSOM]=\"0\"\n"
        "[MenVoice]=\"-1\"\n"
        "[SuperSkip]=\"0\"\n"
        "[SeVolume]=\"5\"\n"
        "[OpaqueFuture]=\"keep-me\"\n";

    std::vector<uint8_t> encoded;
    Check(Storage::EncodeDocument(logical, &encoded, &error) &&
              encoded.size() > 4u && encoded[0] == 'D' && encoded[1] == 'F' &&
              encoded[2] == 'L' && encoded[3] == 'T',
          "DFLT encode failed");
    std::string decoded;
    Check(Storage::DecodeDocument(encoded, &decoded, &error) && decoded == logical,
          "DFLT logical round-trip failed");
    Check(Storage::DecodeDocument(Bytes(logical), &decoded, &error) &&
              decoded == logical,
          "plain-text Config.DAT decode failed");

    const fs::path root = fs::temp_directory_path() / "kotonoha_sd_config_storage";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path path = root / "Config.DAT";
    { std::ofstream out(path, std::ios::binary); out.write(
          reinterpret_cast<const char*>(encoded.data()),
          static_cast<std::streamsize>(encoded.size())); }

    Storage storage;
    Storage::LoadResult loaded;
    Check(storage.Initialize(root, &error) && storage.Load(&loaded, &error) &&
              loaded.present && storage.IsLoaded() && !storage.IsDirty(),
          "Config.DAT load failed");
    Check(loaded.state.voiceVolume == 8 && loaded.state.bgmVolume == 5 &&
              loaded.state.seVolume == 5 && loaded.state.textView &&
              loaded.state.menVoice && !loaded.state.mute && loaded.state.skip &&
              loaded.state.autoDraw && !loaded.state.superSkip,
          "typed Config.DAT projection/default truthiness failed");
    Check(loaded.state.masterVolume == -1.0f,
          "unproven persisted MasterVolume was incorrectly restored");
    Check(loaded.display.hasDisplayType && loaded.display.displayType == 1 &&
              loaded.display.hasTypeMiniNote && loaded.display.typeMiniNote == 0 &&
              loaded.display.hasWindowMode && loaded.display.windowMode == 1,
          "startup display keys were not projected");

    auto state = loaded.state;
    state.voiceVolume = 2;
    state.bgmVolume = 9;
    state.seVolume = 4;
    state.textView = false;
    state.mute = true;
    state.skip = false;
    state.superSkip = true;
    Check(storage.CacheState(state, &error) && storage.IsDirty() &&
              storage.CacheDisplayType(0, &error) &&
              storage.CacheTypeMiniNote(1, &error),
          "cache mutation failed");

    // Native physical persistence is deferred: bytes must not change until
    // orderly Flush.
    std::vector<uint8_t> beforeFlush;
    Check(Read(path, &beforeFlush) && beforeFlush == encoded,
          "cache mutation wrote Config.DAT before Flush");
    Check(storage.Flush(&error) && !storage.IsDirty(), "Config.DAT flush failed");

    std::vector<uint8_t> afterFlush;
    Check(Read(path, &afterFlush) && afterFlush != beforeFlush &&
              Storage::DecodeDocument(afterFlush, &decoded, &error),
          "flushed Config.DAT could not be decoded");
    Check(decoded.find("SeVolume]=\"9\"") != std::string::npos &&
              decoded.find("MenVoice]=\"0\"") != std::string::npos &&
              decoded.find("SuperSkip]=\"1\"") != std::string::npos &&
              decoded.find("[OpaqueFuture]=\"keep-me\"") != std::string::npos,
          "opaque/malformed Config.DAT lines were not preserved");
    Check(decoded.find("[VoiceVolume]=\"2\"") != std::string::npos &&
              decoded.find("[BgmVolume]=\"9\"") != std::string::npos &&
              decoded.find("[SeVolume]=\"4\"") != std::string::npos &&
              decoded.find("[MasterVolume]=\"-7.000000\"") != std::string::npos &&
              decoded.find("[TextView]=\"0\"") != std::string::npos &&
              decoded.find("[Mute]=\"1\"") != std::string::npos &&
              decoded.find("[Skip]=\"0\"") != std::string::npos &&
              decoded.find("[SuperSkip]=\"1\"") != std::string::npos &&
              decoded.find("[DisplayType]=\"0\"") != std::string::npos &&
              decoded.find("[TypeMiniNote]=\"1\"") != std::string::npos &&
              decoded.find("[WindowMode]=\"1\"") != std::string::npos,
          "known Config.DAT cache rewrite failed");

    Storage reload;
    Storage::LoadResult reloaded;
    Check(reload.Initialize(root, &error) && reload.Load(&reloaded, &error) &&
              reloaded.state.voiceVolume == 2 && reloaded.state.bgmVolume == 9 &&
              reloaded.state.seVolume == 4 && !reloaded.state.textView &&
              reloaded.state.mute && !reloaded.state.skip &&
              reloaded.state.superSkip && reloaded.display.displayType == 0 &&
              reloaded.display.typeMiniNote == 1 && reloaded.display.windowMode == 1,
          "flushed Config.DAT reload failed");

    // Duplicate valid keys fail closed; opaque malformed lookalikes above do not.
    const std::string duplicate =
        "< Config.dat >\n[Skip]=\"0\"\n[Skip]=\"1\"\n";
    Check(Storage::EncodeDocument(duplicate, &encoded, &error),
          "duplicate fixture encode failed");
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(
          reinterpret_cast<const char*>(encoded.data()),
          static_cast<std::streamsize>(encoded.size())); }
    Storage dup;
    Check(dup.Initialize(root, &error) && !dup.Load(&reloaded, &error),
          "duplicate valid Config.DAT key did not fail closed");

    // Missing Config.DAT is a first-run default and stays absent until a cache
    // mutation followed by explicit Flush.
    fs::remove(path, ec);
    Storage fresh;
    Storage::LoadResult firstRun;
    Check(fresh.Initialize(root, &error) && fresh.Load(&firstRun, &error) &&
              !firstRun.present && Policy::Validate(firstRun.state) &&
              !fresh.IsDirty() && !fs::exists(path),
          "missing Config.DAT first-run semantics failed");
    Check(fresh.Flush(&error) && !fresh.IsDirty() && !fs::exists(path),
          "clean first-run flush unexpectedly created Config.DAT");
    Check(fresh.CacheState(firstRun.state, &error) && fresh.IsDirty() &&
              !fs::exists(path) && fresh.Flush(&error) && fs::exists(path),
          "first-run deferred Config.DAT creation failed");

    // Optional real retail file: read/decode/project only; never mutate it.
    if (argc >= 2 && argv[1] != nullptr && *argv[1] != '\0') {
        const fs::path retail(argv[1]);
        const fs::path copy = root / "retail" / "Config.DAT";
        fs::create_directories(copy.parent_path());
        fs::copy_file(retail, copy, fs::copy_options::overwrite_existing);
        Storage corpus;
        Storage::LoadResult corpusLoad;
        Check(corpus.Initialize(copy.parent_path(), &error) &&
                  corpus.Load(&corpusLoad, &error) && corpusLoad.present &&
                  corpusLoad.state.voiceVolume == 8 &&
                  corpusLoad.state.bgmVolume == 5 &&
                  corpusLoad.state.seVolume == 5 && corpusLoad.state.textView &&
                  corpusLoad.state.menVoice && !corpusLoad.state.mute &&
                  !corpusLoad.state.skip && !corpusLoad.state.superSkip,
              "real retail Config.DAT projection failed");
    }

    fs::remove_all(root, ec);
    std::cout << "SCHOOL DAYS CONFIG STORAGE GATE PASS\n"
              << "plain_and_dflt_decode=PASS\n"
              << "logical_zlib_roundtrip=PASS\n"
              << "truthy_int_projection=PASS\n"
              << "mastervolume_preserved_not_loaded=PASS\n"
              << "mastervolume_preserved_on_cache=PASS\n"
              << "display_b_m_w_projection=PASS\n"
              << "cache_before_flush=PASS\n"
              << "opaque_line_preservation=PASS\n"
              << "duplicate_valid_key_fail_closed=PASS\n"
              << "clean_first_run_flush_no_create=PASS\n"
              << "missing_first_run_deferred_create=PASS\n";
    return 0;
}
