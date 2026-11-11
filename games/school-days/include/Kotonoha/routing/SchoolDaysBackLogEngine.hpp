#pragma once

#include <SchoolDays/SchoolDaysAssets.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysCmap.hpp>

#include <memory>
#include <string>

namespace Kotonoha {

class Engine;

class SchoolDaysBackLogEngine final : public VNBacklog {
public:
  SchoolDaysBackLogEngine(Engine &engine, Kotonoha::Providers::SchoolDaysAssets &assets);
  ~SchoolDaysBackLogEngine() override;

  void open() override;
  void close() override;
  bool isVisible() const override { return isOpen_ || VNBacklog::isVisible(); }
  void addEntry(const std::string &speaker, const std::string &text) override;
  void clear() override;
  bool onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;

  // Estado do modelo nativo (exposto para o Runtime e ferramentas).
  bool modelOpen() const { return backlogModel_.IsOpen(); }
  SchoolDaysBackLogModel &model() { return backlogModel_; }
  const SchoolDaysBackLogStore &store() const { return backlogStore_; }

private:
  Engine &engine_;
  Kotonoha::Providers::SchoolDaysAssets &assets_;
  SchoolDaysBackLogStore backlogStore_;
  SchoolDaysBackLogModel backlogModel_;
  SchoolDaysBackLogPresenter backlogPresenter_;
  bool isOpen_ = false;
};

} // namespace Kotonoha
