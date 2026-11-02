#pragma once

#include <Kotonoha/core/Scene.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace Kotonoha {

// Presents one splash scene at a time. A successor starts only after the active
// scene reports that it is finished, so chained splash media never overlap.
class SplashChainloader final : public Scene {
public:
    explicit SplashChainloader(Engine &engine,
                               std::vector<std::unique_ptr<Scene>> scenes)
        : Scene(engine), scenes(std::move(scenes)) {
        this->scenes.erase(
            std::remove(this->scenes.begin(), this->scenes.end(), nullptr),
            this->scenes.end());
        restart();
    }

    bool finished() const override { return current >= scenes.size(); }

    void restart() override {
        current = 0;
        for (const auto &scene : scenes)
            scene->restart();
        advanceFinishedScenes();
    }

    bool capturesInput() const override {
        return !finished() && scenes[current]->capturesInput();
    }

    RenderSpace renderSpace() const override { return RenderSpace::Screen; }

    bool handleEvent(EventRef event) override {
        if (finished())
            return false;
        return scenes[current]->handleEvent(event);
    }

    void update() override {
        if (finished())
            return;
        scenes[current]->update();
        advanceFinishedScenes();
    }

    void render(RendererRef renderer, float scale) override {
        if (!finished())
            scenes[current]->render(renderer, scale);
    }

private:
    std::vector<std::unique_ptr<Scene>> scenes;
    std::size_t current = 0;

    void advanceFinishedScenes() {
        while (!finished() && scenes[current]->finished())
            ++current;
    }
};

} // namespace Kotonoha
