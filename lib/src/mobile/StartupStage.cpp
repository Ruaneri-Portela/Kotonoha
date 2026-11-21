#ifdef __ANDROID__
#include <Kotonoha/mobile/StartupStage.hpp>
#include <SDL3/SDL_system.h>
#include <jni.h>

namespace Kotonoha::Mobile {
void setStartupStage(StartupStage stage) noexcept {
  auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  if (!env || env->ExceptionCheck())
    return;
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!activity)
    return;
  jclass type = env->GetObjectClass(activity);
  auto method = type ? env->GetMethodID(type, "setKotonohaStartupStage", "(I)V")
                     : nullptr;
  if (method)
    env->CallVoidMethod(activity, method, static_cast<jint>(stage));
  if (env->ExceptionCheck())
    env->ExceptionClear();
  if (type)
    env->DeleteLocalRef(type);
  env->DeleteLocalRef(activity);
}
} // namespace Kotonoha::Mobile
#endif
