#pragma once
// CMP asset provider for the Summer Radish Vacation!! discs.
#include <Kotonoha/core/AssetProvider.hpp>
#include <SummerRadish/Pack.hpp>
#include <memory>
namespace Kotonoha::Providers {
class SummerAssets final : public Kotonoha::AssetProvider {
  std::map<std::string, std::unique_ptr<SummerRadish::Pack>> packs;
  std::filesystem::path discDirectory;

public:
  explicit SummerAssets(std::filesystem::path root);
  std::string name() const override { return "Summer Radish CMP"; }
  bool containsAsset(const std::string &) const override;
  SummerRadish::Bytes readAsset(const std::string &) const override;
  // Lê o asset se existir; retorna nullopt quando ausente (útil para ativos
  // opcionais que diferem entre instalações do jogo).
  std::optional<SummerRadish::Bytes> tryReadAsset(const std::string &) const;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &) const override;
  SummerRadish::Image picture(const std::string &) const;
  // Native movie files are loose on DISC1 rather than inside a CMP archive.
  std::filesystem::path moviePath(const std::string &filename) const;
};
} // namespace Kotonoha::Providers
