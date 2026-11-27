#if defined(__ANDROID__) || defined(ANDROID)

#include <Kotonoha/mobile/AndroidDisplay.hpp>

#include <SDL3/SDL_system.h>

#include <jni.h>

#include <algorithm>
#include <vector>

namespace Kotonoha {
namespace {

bool JniReady(JNIEnv **outEnv, jobject *outActivity, jclass *outClass) {
  if (!outEnv || !outActivity || !outClass) return false;
  auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity) return false;
  jclass cls = env->GetObjectClass(activity);
  if (!cls) return false;
  *outEnv = env;
  *outActivity = activity;
  *outClass = cls;
  return true;
}

void ClearException(JNIEnv *env) {
  if (env && env->ExceptionCheck()) env->ExceptionClear();
}

} // namespace

std::vector<AndroidDisplayMode> AndroidEnumerateDisplayModes() {
  std::vector<AndroidDisplayMode> result;
  JNIEnv *env = nullptr;
  jobject activity = nullptr;
  jclass cls = nullptr;
  if (!JniReady(&env, &activity, &cls)) return result;

  const jmethodID method = env->GetMethodID(cls, "getKotonohaDisplayModes", "()[I");
  if (!method) {
    ClearException(env);
    env->DeleteLocalRef(cls);
    return result;
  }
  auto packed = static_cast<jintArray>(env->CallObjectMethod(activity, method));
  if (env->ExceptionCheck() || !packed) {
    ClearException(env);
    env->DeleteLocalRef(cls);
    return result;
  }

  const jsize count = env->GetArrayLength(packed);
  if (count >= 4 && count % 4 == 0) {
    std::vector<jint> values(static_cast<size_t>(count));
    env->GetIntArrayRegion(packed, 0, count, values.data());
    if (!env->ExceptionCheck()) {
      result.reserve(static_cast<size_t>(count / 4));
      for (jsize i = 0; i < count; i += 4) {
        int width = values[static_cast<size_t>(i + 1)];
        int height = values[static_cast<size_t>(i + 2)];
        if (height > width) std::swap(width, height);
        if (values[static_cast<size_t>(i)] <= 0 || width <= 0 || height <= 0)
          continue;
        result.push_back({values[static_cast<size_t>(i)], width, height,
                          static_cast<float>(values[static_cast<size_t>(i + 3)]) /
                              1000.0f});
      }
    } else {
      ClearException(env);
    }
  }
  env->DeleteLocalRef(packed);
  env->DeleteLocalRef(cls);
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    const long long areaA = static_cast<long long>(a.width) * a.height;
    const long long areaB = static_cast<long long>(b.width) * b.height;
    if (areaA != areaB) return areaA < areaB;
    if (a.width != b.width) return a.width < b.width;
    if (a.height != b.height) return a.height < b.height;
    return a.refreshRate < b.refreshRate;
  });
  return result;
}

int AndroidCurrentDisplayModeId() {
  JNIEnv *env = nullptr;
  jobject activity = nullptr;
  jclass cls = nullptr;
  if (!JniReady(&env, &activity, &cls)) return 0;
  const jmethodID method =
      env->GetMethodID(cls, "getKotonohaCurrentDisplayModeId", "()I");
  if (!method) {
    ClearException(env);
    env->DeleteLocalRef(cls);
    return 0;
  }
  const jint result = env->CallIntMethod(activity, method);
  if (env->ExceptionCheck()) {
    ClearException(env);
    env->DeleteLocalRef(cls);
    return 0;
  }
  env->DeleteLocalRef(cls);
  return static_cast<int>(result);
}

bool AndroidRequestDisplayMode(int modeId) {
  if (modeId <= 0) return false;
  JNIEnv *env = nullptr;
  jobject activity = nullptr;
  jclass cls = nullptr;
  if (!JniReady(&env, &activity, &cls)) return false;
  const jmethodID method =
      env->GetMethodID(cls, "setKotonohaPreferredDisplayMode", "(I)Z");
  if (!method) {
    ClearException(env);
    env->DeleteLocalRef(cls);
    return false;
  }
  const jboolean result =
      env->CallBooleanMethod(activity, method, static_cast<jint>(modeId));
  if (env->ExceptionCheck()) {
    ClearException(env);
    env->DeleteLocalRef(cls);
    return false;
  }
  env->DeleteLocalRef(cls);
  return result == JNI_TRUE;
}

} // namespace Kotonoha

#endif
