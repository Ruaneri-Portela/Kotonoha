#include "SchoolDaysEmbeddedAssets.hpp"

#include <android/log.h>
#include <jni.h>

#include <exception>
#include <limits>
#include <string_view>

extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_kotonoha_android_SchoolDaysEmbeddedAssetsBridge_nativeRead(
    JNIEnv *env, jclass, jstring logicalPath) {
  if (!logicalPath)
    return nullptr;
  const char *path = env->GetStringUTFChars(logicalPath, nullptr);
  if (!path)
    return nullptr; // JNI already has a pending exception.
  std::vector<unsigned char> bytes;
  try {
    // The table lookup is read-only and independent of SDL initialization.
    // No temporary files or Android storage permission are needed here.
    bytes = SchoolDays::ReadEmbeddedAsset(std::string_view(path));
  } catch (const std::exception &error) {
    env->ReleaseStringUTFChars(logicalPath, path);
    __android_log_write(ANDROID_LOG_ERROR, "SchoolDaysEmbeddedAssets",
                        error.what());
    return nullptr;
  } catch (...) {
    env->ReleaseStringUTFChars(logicalPath, path);
    __android_log_write(ANDROID_LOG_ERROR, "SchoolDaysEmbeddedAssets",
                        "Unknown embedded resource error");
    return nullptr;
  }
  env->ReleaseStringUTFChars(logicalPath, path);
  if (bytes.empty() ||
      bytes.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
    return nullptr;
  const auto size = static_cast<jsize>(bytes.size());
  jbyteArray result = env->NewByteArray(size);
  if (!result)
    return nullptr;
  env->SetByteArrayRegion(result, 0, size,
                          reinterpret_cast<const jbyte *>(bytes.data()));
  return env->ExceptionCheck() ? nullptr : result;
}
