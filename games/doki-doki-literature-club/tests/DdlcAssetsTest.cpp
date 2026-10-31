#include "DdlcAssets.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("kotonoha-ddlc-assets-" + std::to_string(SDL_GetPerformanceCounter()));

    TemporaryDirectory() {
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("Cannot create isolated test directory");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        const auto game = temporary.path / "game";
        const auto characters = temporary.path / "characters";
        std::filesystem::create_directory(game);
        std::error_code error;
        require(!Ddlc::hasOriginalCharacters(game, error) && !error,
                "Missing character directory must be reported");

        std::filesystem::create_directory(characters);
        auto writeCharacter = [&](const char* name) {
            std::ofstream file(characters / name);
            if (!file.put('x')) throw std::runtime_error("Cannot write test character file");
        };
        for (const char* name : {"monika.chr", "natsuki.chr", "sayori.chr"})
            writeCharacter(name);
        require(!Ddlc::hasOriginalCharacters(game, error) && !error,
                "Missing character file must be reported");

        writeCharacter("yuri.chr");
        require(Ddlc::hasOriginalCharacters(game, error) && !error,
                "All four character files must be recognized");

        std::filesystem::remove(characters / "monika.chr");
        std::filesystem::create_directory(characters / "monika.chr");
        require(!Ddlc::hasOriginalCharacters(game, error) && !error,
                "Character files must be regular files");

        // The live characters folder in the user directory is seeded once
        // from the installation's sibling characters/ folder.
        const auto seed = std::filesystem::absolute("ddlc-assets-seed-test");
        std::filesystem::remove_all(seed);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{seed};
        const auto install = seed / "install";
        const auto user = seed / "user";
        std::filesystem::create_directories(install / "game");
        std::filesystem::create_directories(install / "characters");
        std::filesystem::create_directories(user);
        for (const char* name : Ddlc::kCharacterFiles) {
            std::ofstream file(install / "characters" / name, std::ios::binary);
            if (!(file << "original " << name)) throw std::runtime_error("Cannot write test character file");
        }
        require(Ddlc::seedCharacters(user, install / "game"), "A missing characters folder must be seeded");
        const auto live = Ddlc::charactersDirectory(user);
        for (const char* name : Ddlc::kCharacterFiles) {
            std::ifstream file(live / name, std::ios::binary);
            const std::string content{std::istreambuf_iterator<char>(file), {}};
            require(content == std::string("original ") + name, "Seeded files must copy the originals");
        }
        require(!std::filesystem::exists(user / "characters.seeding"), "The staging folder must not remain");

        std::filesystem::remove(live / "sayori.chr");
        require(!Ddlc::seedCharacters(user, install / "game"), "An existing characters folder must be kept");
        require(!std::filesystem::exists(live / "sayori.chr"), "Deleted characters must stay deleted");

        const auto bare = seed / "bare-user";
        std::filesystem::create_directories(bare);
        std::filesystem::remove_all(install / "characters");
        require(Ddlc::seedCharacters(bare, install / "game"), "The folder is created without originals too");
        require(std::filesystem::is_directory(Ddlc::charactersDirectory(bare)) &&
                    std::filesystem::is_empty(Ddlc::charactersDirectory(bare)),
                "Without originals the characters folder starts empty");

        std::cout << "DdlcAssetsTest: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DdlcAssetsTest: " << error.what() << '\n';
        return 1;
    }
}
