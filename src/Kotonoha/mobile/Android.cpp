#if defined(__ANDROID__) || defined(ANDROID)

#include <Kotonoha/mobile/AndroidDisplay.hpp>

#include <SDL3/SDL_system.h>

#include <jni.h>

#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

extern "C" void KotonohaMobileSetupShared(const char *resourcesDir,
                                          const char *documentsDir,
                                          const char *platformName,
                                          bool redirect);

extern "C" void KotonohaMobileSetup(void) {
  // /data/data/<package>/files
  // Diretório privado interno: use como working directory e para dados
  // que devem permanecer privados ao app.
  const char *internalStoragePath = SDL_GetAndroidInternalStoragePath();

  // /storage/emulated/0/Android/data/<package>/files
  // Área externa do app: use para logs, downloads, cache e arquivos
  // potencialmente grandes/inspecionáveis.
  const char *externalStoragePath = SDL_GetAndroidExternalStoragePath();

  const std::string internalPath =
      internalStoragePath != nullptr ? internalStoragePath : ".";

  const std::string externalPath =
      externalStoragePath != nullptr ? externalStoragePath : internalPath;

  KotonohaMobileSetupShared(
      internalPath.c_str(), // documentos/dados privados
      externalPath.c_str(), // logs em Android/data/<package>/files
      "Android", true);
}

namespace Kotonoha {
namespace {

bool AndroidJniReady(JNIEnv **outEnv, jobject *outActivity, jclass *outClass) {
  if (outEnv == nullptr || outActivity == nullptr || outClass == nullptr)
    return false;
  auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) return false;
  jclass cls = env->GetObjectClass(activity);
  if (cls == nullptr) return false;
  *outEnv = env;
  *outActivity = activity;
  *outClass = cls;
  return true;
}

void ClearPendingJniException(JNIEnv *env) {
  if (env != nullptr && env->ExceptionCheck()) env->ExceptionClear();
}

} // namespace

std::vector<AndroidDisplayMode> AndroidEnumerateDisplayModes() {
  std::vector<AndroidDisplayMode> out;
  JNIEnv *env = nullptr;
  jobject activity = nullptr;
  jclass cls = nullptr;
  if (!AndroidJniReady(&env, &activity, &cls)) return out;

  const jmethodID method =
      env->GetMethodID(cls, "getKotonohaDisplayModes", "()[I");
  if (method == nullptr) {
    ClearPendingJniException(env);
    env->DeleteLocalRef(cls);
    return out;
  }
  auto packed = static_cast<jintArray>(env->CallObjectMethod(activity, method));
  if (env->ExceptionCheck() || packed == nullptr) {
    ClearPendingJniException(env);
    env->DeleteLocalRef(cls);
    return out;
  }

  const jsize count = env->GetArrayLength(packed);
  if (count >= 4 && (count % 4) == 0) {
    std::vector<jint> values(static_cast<size_t>(count));
    env->GetIntArrayRegion(packed, 0, count, values.data());
    if (!env->ExceptionCheck()) {
      out.reserve(static_cast<size_t>(count / 4));
      for (jsize i = 0; i < count; i += 4) {
        int width = values[static_cast<size_t>(i + 1)];
        int height = values[static_cast<size_t>(i + 2)];
        if (height > width) std::swap(width, height);
        if (values[static_cast<size_t>(i)] <= 0 || width <= 0 || height <= 0)
          continue;
        AndroidDisplayMode mode;
        mode.modeId = values[static_cast<size_t>(i)];
        mode.width = width;
        mode.height = height;
        mode.refreshRate =
            static_cast<float>(values[static_cast<size_t>(i + 3)]) / 1000.0f;
        out.push_back(mode);
      }
    } else {
      ClearPendingJniException(env);
    }
  }

  env->DeleteLocalRef(packed);
  env->DeleteLocalRef(cls);
  std::sort(out.begin(), out.end(), [](const AndroidDisplayMode &a,
                                       const AndroidDisplayMode &b) {
    const long long areaA = static_cast<long long>(a.width) * a.height;
    const long long areaB = static_cast<long long>(b.width) * b.height;
    if (areaA != areaB) return areaA < areaB;
    if (a.width != b.width) return a.width < b.width;
    if (a.height != b.height) return a.height < b.height;
    return a.refreshRate < b.refreshRate;
  });
  return out;
}

int AndroidCurrentDisplayModeId() {
  JNIEnv *env = nullptr;
  jobject activity = nullptr;
  jclass cls = nullptr;
  if (!AndroidJniReady(&env, &activity, &cls)) return 0;
  const jmethodID method =
      env->GetMethodID(cls, "getKotonohaCurrentDisplayModeId", "()I");
  if (method == nullptr) {
    ClearPendingJniException(env);
    env->DeleteLocalRef(cls);
    return 0;
  }
  const jint result = env->CallIntMethod(activity, method);
  if (env->ExceptionCheck()) {
    ClearPendingJniException(env);
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
  if (!AndroidJniReady(&env, &activity, &cls)) return false;
  const jmethodID method =
      env->GetMethodID(cls, "setKotonohaPreferredDisplayMode", "(I)Z");
  if (method == nullptr) {
    ClearPendingJniException(env);
    env->DeleteLocalRef(cls);
    return false;
  }
  const jboolean result =
      env->CallBooleanMethod(activity, method, static_cast<jint>(modeId));
  if (env->ExceptionCheck()) {
    ClearPendingJniException(env);
    env->DeleteLocalRef(cls);
    return false;
  }
  env->DeleteLocalRef(cls);
  return result == JNI_TRUE;
}

} // namespace Kotonoha

#endif
