#include <Kotonoha/AssetProvider.hpp>

#if defined(KOTONOHA_RUNTIME_SHARED)
#include <atomic>

namespace Kotonoha {
namespace {
std::atomic<const AssetProvider*> gAssetProvider{nullptr};
}

void SetAssetProvider(const AssetProvider* provider) {
  gAssetProvider.store(provider, std::memory_order_release);
}

const AssetProvider* GetAssetProvider() {
  return gAssetProvider.load(std::memory_order_acquire);
}
} // namespace Kotonoha
#endif
