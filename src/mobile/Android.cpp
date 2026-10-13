#if defined(ANDROID)

#include <SDL3/SDL_system.h>

#include <string>


extern "C" void Kotonoha_MobileSetupShared(
    const char* resourcesDir,
    const char* documentsDir,
    const char* platformName,
    bool redirect
);


extern "C" void Kotonoha_MobileSetup(void)
{
    // Diretório do app na área externa:
    // /storage/emulated/0/Android/data/<package>/files
    //
    // Use para logs, downloads, cache persistente e arquivos grandes.
    const char* externalStoragePath =
        SDL_GetAndroidExternalStoragePath();

    // Fallback para a área interna se armazenamento externo não estiver
    // disponível/montado.
    const char* internalStoragePath =
        SDL_GetAndroidInternalStoragePath();

    const std::string dataPath =
        externalStoragePath != nullptr
            ? externalStoragePath
            : (internalStoragePath != nullptr
                ? internalStoragePath
                : ".");

    // Assets permanecem no APK. Não use dataPath como working directory,
    // pois isso faria paths relativos procurar primeiro em Android/data.
    //
    // APIs SDL podem acessar assets empacotados usando "assets://...".
    const std::string resourcesPath = ".";

    Kotonoha_MobileSetupShared(
        resourcesPath.c_str(),
        dataPath.c_str(),
        "Android",
        true
    );
}

#endif