#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>

#include <stdexcept>

// Objective-C++ on Apple with MetalFX; compiled as C++ elsewhere as a stub.
#if defined(KOTONOHA_HAS_METALFX)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <uuid/uuid.h>

#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace Kotonoha {
namespace {

constexpr size_t kMaxCachedScalers = 8;

struct ScalerKey {
  NSUInteger inputWidth;
  NSUInteger inputHeight;
  NSUInteger outputWidth;
  NSUInteger outputHeight;
  MTLPixelFormat inputFormat;
  MTLPixelFormat outputFormat;

  bool operator==(const ScalerKey &other) const {
    return inputWidth == other.inputWidth && inputHeight == other.inputHeight &&
           outputWidth == other.outputWidth &&
           outputHeight == other.outputHeight &&
           inputFormat == other.inputFormat &&
           outputFormat == other.outputFormat;
  }
};

struct ScalerKeyHash {
  size_t operator()(const ScalerKey &key) const {
    size_t hash = 1469598103934665603ULL;

    const auto mix = [&hash](size_t value) {
      hash ^= value;
      hash *= 1099511628211ULL;
    };

    mix(static_cast<size_t>(key.inputWidth));
    mix(static_cast<size_t>(key.inputHeight));
    mix(static_cast<size_t>(key.outputWidth));
    mix(static_cast<size_t>(key.outputHeight));
    mix(static_cast<size_t>(key.inputFormat));
    mix(static_cast<size_t>(key.outputFormat));

    return hash;
  }
};

struct ScalerEntry {
  id<MTLFXSpatialScaler> scaler = nil;
  id<MTLTexture> workingTexture = nil;
};

std::mutex g_scalerMutex;

std::unordered_map<ScalerKey, ScalerEntry, ScalerKeyHash> g_scalers;

std::deque<ScalerKey> g_scalerOrder;

id<MTLDevice> g_metalDevice = nil;
id<MTLCommandQueue> g_metalQueue = nil;

bool IsMetalFXAvailable(id<MTLDevice> device) {
  if (device == nil) {
    return false;
  }

#if TARGET_OS_IPHONE
  if (@available(iOS 16.0, *)) {
    return [MTLFXSpatialScalerDescriptor supportsDevice:device];
  }
#else
  if (@available(macOS 13.0, *)) {
    return [MTLFXSpatialScalerDescriptor supportsDevice:device];
  }
#endif

  return false;
}

id<MTLTexture> GetMetalTextureFromSDLTexture(SDL_Texture *texture) {
  if (texture == nullptr) {
    return nil;
  }

  const SDL_PropertiesID properties = SDL_GetTextureProperties(texture);

  if (properties == 0) {
    return nil;
  }

  void *opaqueTexture = SDL_GetPointerProperty(
      properties, SDL_PROP_TEXTURE_METAL_TEXTURE_POINTER, nullptr);

  if (opaqueTexture == nullptr) {
    return nil;
  }

  return (__bridge id<MTLTexture>)opaqueTexture;
}

id<MTLTexture> CreateMetalFXWorkingTexture(id<MTLDevice> device,
                                           id<MTLTexture> destinationTexture) {
  if (device == nil || destinationTexture == nil) {
    return nil;
  }

  MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:destinationTexture.pixelFormat
                                   width:destinationTexture.width
                                  height:destinationTexture.height
                               mipmapped:NO];

  descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                     MTLTextureUsageRenderTarget;

  descriptor.storageMode = MTLStorageModePrivate;

  return [device newTextureWithDescriptor:descriptor];
}

void EvictOldScalerEntriesLocked() {
  while (g_scalers.size() > kMaxCachedScalers) {
    if (g_scalerOrder.empty()) {
      g_scalers.clear();
      return;
    }

    const ScalerKey oldestKey = g_scalerOrder.back();

    g_scalerOrder.pop_back();
    g_scalers.erase(oldestKey);
  }
}

void TouchScalerKeyLocked(const ScalerKey &key) {
  for (auto iterator = g_scalerOrder.begin(); iterator != g_scalerOrder.end();
       ++iterator) {
    if (*iterator == key) {
      g_scalerOrder.erase(iterator);
      break;
    }
  }

  g_scalerOrder.push_front(key);
}

bool GetOrCreateScalerEntry(id<MTLDevice> device, id<MTLTexture> inputTexture,
                            id<MTLTexture> outputTexture, ScalerEntry &entry) {
  if (device == nil || inputTexture == nil || outputTexture == nil) {
    return false;
  }

  const ScalerKey key{inputTexture.width,       inputTexture.height,
                      outputTexture.width,      outputTexture.height,
                      inputTexture.pixelFormat, outputTexture.pixelFormat};

  {
    std::lock_guard<std::mutex> lock(g_scalerMutex);

    const auto iterator = g_scalers.find(key);

    if (iterator != g_scalers.end()) {
      entry = iterator->second;
      TouchScalerKeyLocked(key);

      return entry.scaler != nil && entry.workingTexture != nil;
    }
  }

  MTLFXSpatialScalerDescriptor *descriptor = [MTLFXSpatialScalerDescriptor new];

  descriptor.inputWidth = inputTexture.width;
  descriptor.inputHeight = inputTexture.height;
  descriptor.outputWidth = outputTexture.width;
  descriptor.outputHeight = outputTexture.height;
  descriptor.colorTextureFormat = inputTexture.pixelFormat;
  descriptor.outputTextureFormat = outputTexture.pixelFormat;
  descriptor.colorProcessingMode =
      MTLFXSpatialScalerColorProcessingModePerceptual;

  id<MTLFXSpatialScaler> scaler =
      [descriptor newSpatialScalerWithDevice:device];

  if (scaler == nil) {
    SDL_LogError(LogUpscaler,
                 "Failed to create scaler "
                 "(input format=%lu, output format=%lu)",
                 (unsigned long)inputTexture.pixelFormat,
                 (unsigned long)outputTexture.pixelFormat);
    return false;
  }

  id<MTLTexture> workingTexture =
      CreateMetalFXWorkingTexture(device, outputTexture);

  if (workingTexture == nil) {
    SDL_LogError(LogUpscaler, "Failed to create workingTexture");
    return false;
  }

  ScalerEntry newEntry;
  newEntry.scaler = scaler;
  newEntry.workingTexture = workingTexture;

  {
    std::lock_guard<std::mutex> lock(g_scalerMutex);

    const auto iterator = g_scalers.find(key);

    if (iterator != g_scalers.end()) {
      entry = iterator->second;
      TouchScalerKeyLocked(key);

      return entry.scaler != nil && entry.workingTexture != nil;
    }

    g_scalers.emplace(key, newEntry);
    g_scalerOrder.push_front(key);

    EvictOldScalerEntriesLocked();

    entry = newEntry;
  }

  return true;
}

bool BlitTexture(id<MTLCommandBuffer> commandBuffer,
                 id<MTLTexture> sourceTexture,
                 id<MTLTexture> destinationTexture) {
  if (commandBuffer == nil || sourceTexture == nil ||
      destinationTexture == nil) {
    return false;
  }

  if (sourceTexture.width != destinationTexture.width ||
      sourceTexture.height != destinationTexture.height ||
      sourceTexture.pixelFormat != destinationTexture.pixelFormat) {
    return false;
  }

  id<MTLBlitCommandEncoder> encoder = [commandBuffer blitCommandEncoder];

  if (encoder == nil) {
    return false;
  }

  [encoder copyFromTexture:sourceTexture
               sourceSlice:0
               sourceLevel:0
              sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(sourceTexture.width,
                                       sourceTexture.height, 1)
                 toTexture:destinationTexture
          destinationSlice:0
          destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];

  [encoder endEncoding];

  return true;
}

id<MTLTexture> MetalFXUpscaleTextureInto(id<MTLTexture> inputTexture,
                                         id<MTLTexture> outputTexture,
                                         id<MTLCommandBuffer> commandBuffer) {
  if (inputTexture == nil || outputTexture == nil || commandBuffer == nil) {
    return nil;
  }

  if (inputTexture.device != outputTexture.device) {
    SDL_LogError(LogUpscaler, "Input and output use different devices");
    return nil;
  }

  if (outputTexture.width <= inputTexture.width ||
      outputTexture.height <= inputTexture.height) {
    SDL_LogError(LogUpscaler, "Output must be larger than input");
    return nil;
  }

  id<MTLDevice> device = inputTexture.device;

  if (!IsMetalFXAvailable(device)) {
    SDL_LogError(LogUpscaler, "MetalFX is not available on this device");
    return nil;
  }

  ScalerEntry entry;

  if (!GetOrCreateScalerEntry(device, inputTexture, outputTexture, entry)) {
    return nil;
  }

  id<MTLFXSpatialScaler> scaler = entry.scaler;
  id<MTLTexture> workingTexture = entry.workingTexture;

  if (scaler == nil || workingTexture == nil) {
    return nil;
  }

  scaler.colorTexture = inputTexture;
  scaler.outputTexture = workingTexture;
  scaler.inputContentWidth = inputTexture.width;
  scaler.inputContentHeight = inputTexture.height;

  [scaler encodeToCommandBuffer:commandBuffer];

  scaler.colorTexture = nil;
  scaler.outputTexture = nil;

  if (!BlitTexture(commandBuffer, workingTexture, outputTexture)) {
    SDL_LogError(LogUpscaler, "Failed to copy into SDL_Texture");
    return nil;
  }

  return outputTexture;
}

bool KotonohaMetalFXIsAvailable(SDL_Renderer *renderer) {
  @autoreleasepool {
    const char *name = renderer ? SDL_GetRendererName(renderer) : nullptr;
    if (!name || SDL_strcmp(name, "metal") != 0)
      return false;
    SDL_Texture *probe = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                                           SDL_TEXTUREACCESS_STATIC, 1, 1);
    if (!probe) {
      SDL_LogError(LogUpscaler, "Cannot probe MetalFX availability: %s",
                   SDL_GetError());
      return false;
    }
    id<MTLTexture> texture = GetMetalTextureFromSDLTexture(probe);
    const bool available = texture != nil && IsMetalFXAvailable(texture.device);
    SDL_DestroyTexture(probe);
    return available;
  }
}

bool KotonohaMetalFXInit(SDL_Renderer *renderer) {
  @autoreleasepool {
    if (renderer == nullptr) {
      SDL_SetError("KotonohaMetalFXInit: renderer is null");
      return false;
    }

    SDL_Texture *probe = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                                           SDL_TEXTUREACCESS_STATIC, 1, 1);

    if (probe == nullptr) {
      SDL_SetError("KotonohaMetalFXInit: "
                   "could not create probe texture");
      return false;
    }

    id<MTLTexture> metalTexture = GetMetalTextureFromSDLTexture(probe);

    if (metalTexture == nil) {
      SDL_SetError(
          "KotonohaMetalFXInit: "
          "renderer does not expose an MTLTexture; use the Metal backend");
      SDL_DestroyTexture(probe);
      return false;
    }

    g_metalDevice = metalTexture.device;

    if (g_metalDevice == nil) {
      SDL_SetError("KotonohaMetalFXInit: MTLDevice indisponivel");
      SDL_DestroyTexture(probe);
      return false;
    }

    SDL_DestroyTexture(probe);

    if (!IsMetalFXAvailable(g_metalDevice)) {
      SDL_SetError("KotonohaMetalFXInit: "
                   "MetalFX is not available on this device");
      g_metalDevice = nil;
      return false;
    }

    g_metalQueue = [g_metalDevice newCommandQueue];

    if (g_metalQueue == nil) {
      SDL_SetError("KotonohaMetalFXInit: "
                   "could not create MTLCommandQueue");
      g_metalDevice = nil;
      return false;
    }

    return true;
  }
}

void KotonohaMetalFXShutdown(void) {
  @autoreleasepool {
    {
      std::lock_guard<std::mutex> lock(g_scalerMutex);
      g_scalers.clear();
      g_scalerOrder.clear();
    }

    g_metalQueue = nil;
    g_metalDevice = nil;
  }
}

bool KotonohaMetalFXUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture) {
  @autoreleasepool {
    if (renderer == nullptr || inputTexture == nullptr ||
        outputTexture == nullptr) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: "
                   "argumentos invalidos");
      return false;
    }

    if (g_metalDevice == nil || g_metalQueue == nil) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: "
                   "call KotonohaMetalFXInit before using the upscaler");
      return false;
    }

    float inputWidth = 0.0f;
    float inputHeight = 0.0f;
    float outputWidth = 0.0f;
    float outputHeight = 0.0f;

    if (!SDL_GetTextureSize(inputTexture, &inputWidth, &inputHeight) ||
        !SDL_GetTextureSize(outputTexture, &outputWidth, &outputHeight)) {
      return false;
    }

    if (outputWidth <= inputWidth || outputHeight <= inputHeight) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: "
                   "output texture must be larger than input texture");
      return false;
    }

    id<MTLTexture> inputMetalTexture =
        GetMetalTextureFromSDLTexture(inputTexture);

    id<MTLTexture> outputMetalTexture =
        GetMetalTextureFromSDLTexture(outputTexture);

    if (inputMetalTexture == nil || outputMetalTexture == nil) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: "
                   "SDL_Texture does not expose a native MTLTexture");
      return false;
    }

    if (inputMetalTexture.device != g_metalDevice ||
        outputMetalTexture.device != g_metalDevice) {
      SDL_SetError(
          "KotonohaMetalFXUpscaleSDLTextureInto: "
          "texture uses a different MTLDevice than the initial device");
      return false;
    }

    // SDL exposes its encoder, but not its command buffer. Read one pixel to
    // commit and finish SDL writes before consuming them on our Metal queue.
    SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
    if (!SDL_SetRenderTarget(renderer, inputTexture))
      return false;
    SDL_Rect pixel{0, 0, 1, 1};
    SDL_Surface *barrier = SDL_RenderReadPixels(renderer, &pixel);
    const bool restored = SDL_SetRenderTarget(renderer, previousTarget);
    if (barrier == nullptr || !restored) {
      SDL_DestroySurface(barrier);
      return false;
    }
    SDL_DestroySurface(barrier);

    id<MTLCommandBuffer> commandBuffer = [g_metalQueue commandBuffer];

    if (commandBuffer == nil) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: "
                   "could not create MTLCommandBuffer");
      return false;
    }

    id<MTLTexture> result = MetalFXUpscaleTextureInto(
        inputMetalTexture, outputMetalTexture, commandBuffer);

    if (result == nil) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: "
                   "MetalFX failed or texture is not supported");
      return false;
    }

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    if (commandBuffer.error != nil) {
      SDL_SetError("KotonohaMetalFXUpscaleSDLTextureInto: Metal error: %s",
                   commandBuffer.error.localizedDescription.UTF8String);
      return false;
    }

    return true;
  }
}

void KotonohaMetalFXClearScalerCache(void) {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(g_scalerMutex);
    g_scalers.clear();
    g_scalerOrder.clear();
  }
}

} // namespace
} // namespace Kotonoha

#endif // KOTONOHA_HAS_METALFX

namespace Kotonoha {
MetalFXUpscaler::MetalFXUpscaler(RendererRef value) : Upscaler(value) {
#if defined(KOTONOHA_HAS_METALFX)
  if (!isAvaliable())
    throw std::invalid_argument(
        "MetalFXUpscaler requires an SDL Metal renderer");
  if (!KotonohaMetalFXInit(&renderer))
    throw std::runtime_error(SDL_GetError());
#else
  throw std::runtime_error("MetalFX is unavailable in this build");
#endif
}

MetalFXUpscaler::~MetalFXUpscaler() {
#if defined(KOTONOHA_HAS_METALFX)
  KotonohaMetalFXShutdown();
#endif
}

UpscaleBackend MetalFXUpscaler::backend() const {
  return UpscaleBackend::MetalFX;
}
bool MetalFXUpscaler::isAvaliable() const { return isAvaliable(renderer); }
bool MetalFXUpscaler::isAvaliable(RendererRef renderer) {
#if defined(KOTONOHA_HAS_METALFX)
  return KotonohaMetalFXIsAvailable(&renderer);
#else
  (void)renderer;
  return false;
#endif
}

SDL_PixelFormat MetalFXUpscaler::workingFormat() const {
  return SDL_PIXELFORMAT_BGRA32;
}

void MetalFXUpscaler::configureBackend(const UpscaleSettings &) {}

bool MetalFXUpscaler::renderInto(TextureRef input, TextureRef output) {
#if defined(KOTONOHA_HAS_METALFX)
  return KotonohaMetalFXUpscaleSDLTextureInto(&renderer, &input, &output);
#else
  (void)input;
  (void)output;
  return SDL_SetError("MetalFX is unavailable in this build");
#endif
}

void MetalFXUpscaler::clearBackendCache() {
#if defined(KOTONOHA_HAS_METALFX)
  KotonohaMetalFXClearScalerCache();
#endif
}
} // namespace Kotonoha
