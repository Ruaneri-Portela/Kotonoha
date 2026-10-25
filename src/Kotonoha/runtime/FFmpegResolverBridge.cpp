#include <Kotonoha/utils/FFmpeg.h>

#if defined(KOTONOHA_RUNTIME_SHARED)
extern "C" void KotonohaRuntimeSetFFmpegInputResolver(
    Kotonoha_FFmpegInputResolverFn resolver) {
  KotonohaFFmpegSetInputResolver(resolver);
}
#endif
