#pragma once
#include <Kotonoha/core/AssetProvider.hpp>
#include <SummerRadish/Pack.hpp>
#include <memory>
namespace SummerRadish {
class SummerAssets final : public Kotonoha::AssetProvider {
  std::map<std::string, std::unique_ptr<Pack>> packs;

public:
  explicit SummerAssets(std::filesystem::path root);
  std::string name() const override { return "Summer Radish CMP"; }
  bool contains(const std::string &) const override;
  Bytes read(const std::string &) const override;
  std::vector<std::string> list(const std::string &prefix = {}) const override;
  Kotonoha::SurfacePtr loadSurface(const std::string &) const override;
  Image picture(const std::string &) const;
};
} // namespace SummerRadish
