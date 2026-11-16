#pragma once
// Mounts a decrypted Vita GRAPH.CPK through Kotonoha's shared AssetProvider API.
// Uses the CPK reader and Vita palette decoder, not the PS2 TIM2 decoder.
#include <Amagami/VitaCpk.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <string>

namespace Kotonoha::Providers {
class VitaGraphicsAssets final : public Kotonoha::AssetProvider {
public:
  explicit VitaGraphicsAssets(std::string graphCpkPath);
  std::string name() const override { return "amagami-vita-graphics"; }

protected:
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string>
  listAsset(const std::string &prefix) const override;
  Kotonoha::SurfacePtr
  loadSurfaceAsset(const std::string &id) const override;

private:
  std::string cpkPath_;
  Amagami::Vita::CpkIndex index_;
};
} // namespace Kotonoha::Providers
