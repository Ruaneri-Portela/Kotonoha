#pragma once
// Mounts a decrypted Vita GRAPH.CPK through Kotonoha's shared AssetProvider API.
// Uses the CPK reader and Vita palette decoder, not the PS2 TIM2 decoder.
#include <Amagami/VitaCpk.hpp>
#include <Amagami/VitaSystemGraphics.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <string>
#include <array>
#include <memory>
#include <mutex>
#include <cstdint>

namespace Kotonoha::Providers {
class VitaGraphicsAssets final : public Kotonoha::AssetProvider {
public:
  // The second argument is an already-unwrapped, plain 32-bit ELF, not a
  // protected SELF executable. Omit it for background/character/font only.
  explicit VitaGraphicsAssets(std::string graphCpkPath,
                              std::string ebootElfPath = {});
  std::string name() const override { return "amagami-vita-graphics"; }
  // Returns a newly allocated surface or null for a missing texture/variant.
  // Consumers can stage these surfaces with AssetProvider::stageTexture.
  Kotonoha::SurfacePtr uiSurface(uint32_t textureId,
                                  uint32_t variant = 0) const;
  // 0: standard font, 1: alternate font ("im"); SJIS is a two-byte
  // Shift-JIS code and palette is 0..3.
  Kotonoha::SurfacePtr fontGlyph(unsigned font, uint16_t shiftJis,
                                 unsigned palette) const;

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
  std::unique_ptr<Amagami::Vita::UiAtlas> ui_;
  mutable std::mutex fontMutex_;
  mutable std::array<std::string, 2> fontData_;
};
} // namespace Kotonoha::Providers
