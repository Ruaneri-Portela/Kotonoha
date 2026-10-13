#if defined(ANDROID)

#include <SDL3/SDL_system.h>

#include <unistd.h>

#include <string>


extern "C" void Kotonoha_MobileSetupShared(
        const char* resourcesDir,
        const char* documentsDir,
        const char* platformName,
        bool redirect
);


extern "C" void Kotonoha_MobileSetup(void)
{
    // /data/data/<package>/files
    // Diretório privado interno: use como working directory e para dados
    // que devem permanecer privados ao app.
    const char* internalStoragePath =
            SDL_GetAndroidInternalStoragePath();

    // /storage/emulated/0/Android/data/<package>/files
    // Área externa do app: use para logs, downloads, cache e arquivos
    // potencialmente grandes/inspecionáveis.
    const char* externalStoragePath =
            SDL_GetAndroidExternalStoragePath();

    const std::string internalPath =
            internalStoragePath != nullptr ? internalStoragePath : ".";

    const std::string externalPath =
            externalStoragePath != nullptr ? externalStoragePath : internalPath;

    Kotonoha_MobileSetupShared(
            internalPath.c_str(),   // documentos/dados privados
            externalPath.c_str(),   // logs em Android/data/<package>/files
            "Android",
            true
    );
}

#endif