// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Link-time registration of game runtimes. No game main.cpp/SDL entrypoint is
// linked into the hub, and no child processes are launched.
#include <KotonohaLauncher/UnifiedLauncher.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <filesystem>
#include <memory>
#include <utility>

#if defined(KOTONOHA_LAUNCH_AMAGAMI)
#include "AmagamiRuntime.hpp"
#include <Amagami/Assets.hpp>
#include <Amagami/Source.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_ZERO)
#include "ZeroRuntime.hpp"
#include <ZeroNoTsukaima/DiscAssetProvider.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_SNOW)
#include "SnowRuntime.hpp"
#include <SnowRadish/SnowAssets.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_SUMMER) || defined(KOTONOHA_LAUNCH_MEO)
#include "SummerRuntime.hpp"
#include <SummerRadish/SummerAssets.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_DDLC) || defined(KOTONOHA_LAUNCH_KATAWA)
#include "DdlcRuntime.hpp"
#include <Ddlc/DdlcAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
#include "SchoolDaysRuntime.hpp"
#include <SchoolDays/SchoolDaysAssets.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_REALLIVE)
#include "RealLiveRuntime.hpp"
#include <RealLive/RealLiveAssets.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_KIRIKIRI)
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/KirikiriRuntime.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_LUCA_SYSTEM)
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_PMAIL)
#include "LargePonPonRuntime.hpp"
#include "PmailRuntime.hpp"
#include <Pmail/GcPack.hpp>
#include <Pmail/LargePonPonAssets.hpp>
#include <SnowRadish/SnowAssets.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_N2SYSTEM)
#include <N2System/N2ArchiveAssets.hpp>
#include <N2System/N2Runtime.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_PONSCRIPTER)
#include <Ponscripter/PonscripterAssets.hpp>
#include <Ponscripter/PonscripterRuntime.hpp>
#endif
#if defined(KOTONOHA_LAUNCH_ARTEMIS)
#include <Artemis/ArtemisAssets.hpp>
#include <Artemis/ArtemisRuntime.hpp>
#endif

namespace KotonohaLauncher {
namespace {
using Kotonoha::Filesystem::EngineKind;

// Use an on-disk subdirectory only for directory sources. For ISO/BIN
// sources, retain the disc path and let the provider's virtual mount resolve
// dataPrefix. Never append a directory suffix to an ISO pathname.
std::string selectedSourcePath(const DiscoveredGameEntry &entry) {
  const std::filesystem::path original = std::filesystem::u8path(entry.path);
  if (entry.engine.dataPrefix.empty())
    return entry.path;
  std::error_code error;
  if (!std::filesystem::is_directory(original, error))
    return entry.path;
  // Ren'Py providers resolve their own game/ directory, CMP providers
  // recognize the retail disc layout from its root, and SchoolDaysAssets
  // requires the root containing both Packs/ and Game00.ini. Do not strip their
  // game roots when the launcher selects a subfolder.
  if (entry.engine.kind == EngineKind::RenPy ||
      entry.engine.kind == EngineKind::SummerRadish ||
      entry.engine.kind == EngineKind::SchoolDays ||
      entry.engine.kind == EngineKind::PureMail)
    return entry.path;
  const auto nested = original / entry.engine.dataPrefix;
  if (std::filesystem::is_directory(nested, error))
    return nested.u8string();
  return entry.path;
}

void bindFactory(EngineLibraryRegistry &registry, EngineKind kind,
                 EngineLibraryFactory factory) {
  // Multiple standalone ports can share one engine family (MEO/Summer,
  // Katawa/DDLC). Binding only find(kind) left the other identity unlinked.
  std::vector<EngineLibraryDescriptor> matching;
  for (const auto &descriptor : registry.all())
    if (descriptor.kind == kind)
      matching.push_back(descriptor);
  for (auto &descriptor : matching) {
    descriptor.factory = factory;
    registry.registerLibrary(std::move(descriptor));
  }
}

template <class RuntimeType, class ProviderType>
MountedEngineInstance
mount(std::unique_ptr<RuntimeType> runtime,
      std::shared_ptr<ProviderType> provider,
      std::function<void(Kotonoha::Runtime &)> shutdown = {}) {
  MountedEngineInstance instance;
  instance.runtime = std::move(runtime);
  instance.assets = std::move(provider);
  instance.shutdown = std::move(shutdown);
  return instance;
}

template <class RuntimeType>
std::function<void(Kotonoha::Runtime &)> shutdownRuntime() {
  return [](Kotonoha::Runtime &runtime) {
    static_cast<RuntimeType &>(runtime).shutdown();
  };
}
} // namespace

void registerLinkedEngineFactories(EngineLibraryRegistry &registry) {
#if defined(KOTONOHA_LAUNCH_AMAGAMI)
  bindFactory(
      registry, EngineKind::Amagami,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto source = std::make_shared<Amagami::Source>(entry.path);
        auto assets =
            std::make_shared<Kotonoha::Providers::AmagamiAssets>(source);
        assets->setOverlay(opts.assetOverlayDirectory());
        return mount(std::make_unique<Amagami::AmagamiRuntime>(assets), assets,
                     shutdownRuntime<Amagami::AmagamiRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_ZERO)
  bindFactory(
      registry, EngineKind::ZeroNoTsukaima,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets =
            std::make_shared<Kotonoha::Providers::ZeroDiscAssets>(entry.path);
        assets->setOverlay(opts.assetOverlayDirectory());
        return mount(
            std::make_unique<ZeroNoTsukaima::ZeroRuntime>(assets), assets,
            [](Kotonoha::Runtime &runtime) {
              static_cast<ZeroNoTsukaima::ZeroRuntime &>(runtime).Shutdown();
            });
      });
#endif
#if defined(KOTONOHA_LAUNCH_SNOW)
  bindFactory(
      registry, EngineKind::SnowRadish,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(
            selectedSourcePath(entry));
        assets->setOverlay(opts.assetOverlayDirectory());
        return mount(
            std::make_unique<SnowRadish::SnowRuntime>(assets), assets,
            [](Kotonoha::Runtime &runtime) {
              static_cast<SnowRadish::SnowRuntime &>(runtime).Shutdown();
            });
      });
#endif
#if defined(KOTONOHA_LAUNCH_SUMMER) || defined(KOTONOHA_LAUNCH_MEO)
  bindFactory(
      registry, EngineKind::SummerRadish,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets = std::make_shared<Kotonoha::Providers::SummerAssets>(
            selectedSourcePath(entry), opts.assetOverlayDirectory());
        assets->setOverlay(opts.assetOverlayDirectory());
        // Shared CMP engine, but each port keeps its own window and save
        // identity.
        const bool isMeo = entry.engine.slug == "meo";
        const bool isLostM = assets->isLostM();
        auto runtime =
            isMeo
                ? std::make_unique<SummerRadish::SummerRuntime>(
                      assets, "Miss Each Other", "me.hirameki.kotonoha.meo")
                : (isLostM
                       ? std::make_unique<SummerRadish::SummerRuntime>(
                             assets, "Lost M", "me.hirameki.kotonoha.lostm")
                       : std::make_unique<SummerRadish::SummerRuntime>(assets));
        return mount(std::move(runtime), assets,
                     shutdownRuntime<SummerRadish::SummerRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_DDLC) || defined(KOTONOHA_LAUNCH_KATAWA)
  bindFactory(
      registry, EngineKind::RenPy,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        const auto sourcePath = selectedSourcePath(entry);
        std::shared_ptr<Kotonoha::Providers::DdlcAssets> assets;
        std::error_code ec;
        if (std::filesystem::is_regular_file(sourcePath, ec)) {
          auto source = Kotonoha::Filesystem::Filesystem::mount(sourcePath);
          if (!opts.assetOverlayDirectory().empty())
            source->addOverlay(opts.assetOverlayDirectory());
          assets = std::make_shared<Kotonoha::Providers::DdlcAssets>(
              source, opts.assetOverlayDirectory());
        } else {
          assets = std::make_shared<Kotonoha::Providers::DdlcAssets>(
              std::filesystem::path(sourcePath), opts.assetOverlayDirectory());
        }
        // Preserve the installation location for per-game persistent state
        // when the launcher mounts a virtual archive instead of a directory.
        assets->setSourceIdentity(sourcePath);
        return mount(std::make_unique<Ddlc::GameRuntime>(assets), assets,
                     shutdownRuntime<Ddlc::GameRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
  bindFactory(
      registry, EngineKind::SchoolDays,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        const std::string overlayOrScripts = !entry.params.scriptsDir.empty()
                                                 ? entry.params.scriptsDir
                                                 : opts.assetOverlayDirectory();
        auto assets = std::make_shared<Kotonoha::Providers::SchoolDaysAssets>(
            Kotonoha::gameDataDirectory(selectedSourcePath(entry)),
            overlayOrScripts);
        std::string route = entry.params.ktrfPath;
        if (route.empty()) {
          route =
              Kotonoha::Providers::SchoolDaysAssets::defaultRoute().string();
        }
        std::filesystem::path docs =
            entry.params.customDocumentsDir.empty()
                ? std::filesystem::path{}
                : std::filesystem::path(entry.params.customDocumentsDir);
        if (opts.subtitleStylePath.empty()) {
          if (const auto styles = Kotonoha::FileResolver::findGameAsset(
                  "school-days", "styles.ass")) {
            opts.subtitleStylePath = styles->string();
          }
        }
        return mount(std::make_unique<SchoolDays::Runtime>(assets, route, docs),
                     assets, shutdownRuntime<SchoolDays::Runtime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_REALLIVE)
  bindFactory(
      registry, EngineKind::RealLive,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets = std::make_shared<Kotonoha::Providers::RealLiveAssets>(
            selectedSourcePath(entry), opts.assetOverlayDirectory());
        return mount(std::make_unique<RealLive::RealLiveRuntime>(assets),
                     assets, shutdownRuntime<RealLive::RealLiveRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_KIRIKIRI)
  bindFactory(
      registry, EngineKind::Kirikiri,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets = std::make_shared<Kotonoha::Providers::KirikiriAssets>(
            selectedSourcePath(entry), opts.assetOverlayDirectory());
        assets->mountAllXp3();
        return mount(std::make_unique<Kirikiri::KirikiriRuntime>(assets),
                     assets);
      });
#endif
#if defined(KOTONOHA_LAUNCH_LUCA_SYSTEM)
  bindFactory(
      registry, EngineKind::LucaSystem,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets = std::make_shared<Kotonoha::Providers::LucaSystemAssets>(
            selectedSourcePath(entry), opts.assetOverlayDirectory());
        return mount(std::make_unique<LucaSystem::LucaSystemRuntime>(assets),
                     assets, shutdownRuntime<LucaSystem::LucaSystemRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_PMAIL)
  bindFactory(
      registry, EngineKind::PureMail,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        const auto path = selectedSourcePath(entry);
        if (Kotonoha::Providers::isLargePonPonDirectory(path)) {
          auto assets =
              std::make_shared<Kotonoha::Providers::LargePonPonAssets>(path);
          assets->setOverlay(opts.assetOverlayDirectory());
          return mount(
              std::make_unique<Kotonoha::PmailPort::LargePonPonRuntime>(path,
                                                                        assets),
              assets,
              shutdownRuntime<Kotonoha::PmailPort::LargePonPonRuntime>());
        }
        auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(path);
        assets->setOverlay(opts.assetOverlayDirectory());
        return mount(
            std::make_unique<Kotonoha::PmailPort::PmailRuntime>(path, assets),
            assets, shutdownRuntime<Kotonoha::PmailPort::PmailRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_N2SYSTEM)
  bindFactory(
      registry, EngineKind::N2System,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        auto assets = std::make_shared<N2System::N2ArchiveAssets>(
            selectedSourcePath(entry), opts.assetOverlayDirectory());
        return mount(std::make_unique<N2System::N2Runtime>(assets), assets,
                     shutdownRuntime<N2System::N2Runtime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_PONSCRIPTER)
  bindFactory(
      registry, EngineKind::ONScripter,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        const auto path = selectedSourcePath(entry);
        auto assets = std::make_shared<Ponscripter::PonscripterAssets>(
            path, opts.assetOverlayDirectory());
        return mount(std::make_unique<Ponscripter::PonscripterRuntime>(assets),
                     assets,
                     shutdownRuntime<Ponscripter::PonscripterRuntime>());
      });
#endif
#if defined(KOTONOHA_LAUNCH_ARTEMIS)
  bindFactory(
      registry, EngineKind::Artemis,
      [](const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &opts) {
        const auto path = selectedSourcePath(entry);
        auto assets = std::make_shared<Artemis::ArtemisAssets>(
            path, opts.assetOverlayDirectory());
        return mount(std::make_unique<Artemis::ArtemisRuntime>(assets), assets,
                     shutdownRuntime<Artemis::ArtemisRuntime>());
      });
#endif
  // All variants were bound above by engine kind; no post-registration
  // descriptor copies or dependence on registry insertion order.
}

} // namespace KotonohaLauncher
