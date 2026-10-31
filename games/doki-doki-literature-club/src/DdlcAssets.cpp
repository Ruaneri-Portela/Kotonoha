#include "DdlcAssets.hpp"

#include <Kotonoha/core/FileSystem.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace Ddlc {

bool hasOriginalCharacters(const std::filesystem::path& gameDirectory, std::error_code& error) {
    const auto game = std::filesystem::absolute(gameDirectory, error);
    if (error) return false;
    for (const char* name : kCharacterFiles) {
        const auto status = std::filesystem::status(game.parent_path() / "characters" / name, error);
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory) {
            error.clear();
            return false;
        }
        if (error || !std::filesystem::is_regular_file(status)) return false;
    }
    return true;
}

std::filesystem::path userDirectory() {
    if (const char* configured = SDL_getenv("DDLC_SAVE_DIR"); configured && *configured) {
        std::filesystem::path directory(configured);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) throw std::runtime_error("Cannot create DDLC save directory: " + error.message());
        return std::filesystem::absolute(directory);
    }
    return Kotonoha::preferredDocumentsDirectory("doki-doki-literature-club");
}

bool seedCharacters(const std::filesystem::path& user, const std::filesystem::path& gameDirectory,
                    const Assets* archives) {
    const auto target = charactersDirectory(user);
    std::error_code error;
    if (std::filesystem::exists(target, error)) return false;
    if (error) throw std::runtime_error("Cannot inspect " + target.string() + ": " + error.message());
    const auto staging = user / "characters.seeding";
    std::filesystem::remove_all(staging, error);
    std::filesystem::create_directories(staging, error);
    if (error) throw std::runtime_error("Cannot create " + staging.string() + ": " + error.message());
    const auto originals = std::filesystem::absolute(gameDirectory, error).parent_path() / "characters";
    for (const char* name : kCharacterFiles) {
        const auto source = originals / name;
        if (std::filesystem::is_regular_file(source, error)) {
            std::filesystem::copy_file(source, staging / name, error);
            if (error)
                throw std::runtime_error("Cannot copy " + source.string() + ": " + error.message());
            continue;
        }
        if (archives && archives->has(name)) {
            const auto bytes = archives->read(name);
            std::ofstream out(staging / name, std::ios::binary);
            if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())))
                throw std::runtime_error("Cannot write " + (staging / name).string());
            continue;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DDLC: no original %s to place in %s", name,
                    target.string().c_str());
    }
    std::filesystem::rename(staging, target, error);
    if (error) throw std::runtime_error("Cannot create " + target.string() + ": " + error.message());
    SDL_Log("DDLC: character files placed in %s", target.string().c_str());
    return true;
}

GameAssets::GameAssets(const std::filesystem::path& directory) {
    if (!assets_.open(std::filesystem::absolute(directory).lexically_normal().string()))
        throw std::runtime_error("Cannot open Ren'Py archives in " + directory.string());
    std::string error;
    if (!script_.load(assets_, &error))
        throw std::runtime_error("Cannot load DDLC scripts" + (error.empty() ? std::string() : ": " + error));
    if (!script_.hasLabel("start") || !script_.hasLabel("splashscreen"))
        throw std::runtime_error("The game directory does not contain DDLC's start and splash scripts");
    std::error_code pathError;
    if (!hasOriginalCharacters(assets_.dir(), pathError)) {
        if (pathError)
            throw std::runtime_error("Cannot inspect DDLC character files: " + pathError.message());
        SDL_Log("DDLC: no sibling characters directory; the .chr files come from scripts.rpa");
    }
}

std::string GameAssets::name() const {
    return "Doki Doki Literature Club! (" + assets_.dir() + ")";
}

bool GameAssets::contains(const std::string& id) const {
    return assets_.has(id);
}

std::vector<Uint8> GameAssets::read(const std::string& id) const {
    if (!contains(id)) throw std::runtime_error("DDLC asset not found: " + id);
    return assets_.read(id);
}

std::vector<std::string> GameAssets::list(const std::string& prefix) const {
    auto result = assets_.names();
    result.erase(std::remove_if(result.begin(), result.end(), [&](const std::string& id) {
        return id.compare(0, prefix.size(), prefix) != 0;
    }), result.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace Ddlc
