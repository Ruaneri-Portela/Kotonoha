#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace Kotonoha {

class AssetProvider {
public:
  virtual ~AssetProvider() = default;
  virtual bool IsVirtualReference(const std::string& reference) const = 0;
  virtual bool ReadFile(const std::string& reference,
                        std::vector<uint8_t>* bytes,
                        std::string* error) const = 0;
  virtual bool ListDirectory(const std::string& reference,
                            std::vector<std::string>* children,
                            std::string* error) const = 0;
};

#if defined(KOTONOHA_RUNTIME_SHARED)
#if defined(_WIN32)
#if defined(KOTONOHA_RUNTIME_EXPORTS)
#define KOTONOHA_ASSET_PROVIDER_API __declspec(dllexport)
#else
#define KOTONOHA_ASSET_PROVIDER_API __declspec(dllimport)
#endif
#else
#define KOTONOHA_ASSET_PROVIDER_API
#endif

KOTONOHA_ASSET_PROVIDER_API void SetAssetProvider(const AssetProvider* provider);
KOTONOHA_ASSET_PROVIDER_API const AssetProvider* GetAssetProvider();

#undef KOTONOHA_ASSET_PROVIDER_API
#else
inline std::atomic<const AssetProvider*> assetProvider{nullptr};

inline void SetAssetProvider(const AssetProvider* provider) {
  assetProvider.store(provider, std::memory_order_release);
}

inline const AssetProvider* GetAssetProvider() {
  return assetProvider.load(std::memory_order_acquire);
}
#endif

} // namespace Kotonoha
