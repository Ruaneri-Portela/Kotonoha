#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/Gameplay.hpp>

#include <new>

namespace Kotonoha {
namespace {

Gameplay* CreateRealGameplay(const char* orsPath, Kotonoha_Game* gameContext,
                             void*) {
    if (orsPath == nullptr || *orsPath == '\0' || gameContext == nullptr) {
        return nullptr;
    }
    try {
        return new Gameplay(orsPath, gameContext);
    }
    catch (...) {
        return nullptr;
    }
}

void DestroyRealGameplay(Gameplay* gameplay, void*) {
    delete gameplay;
}

} // namespace

SchoolDaysKtrfGameplayBridge::Factory MakeSchoolDaysRealGameplayFactory() {
    SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &CreateRealGameplay;
    factory.destroy = &DestroyRealGameplay;
    factory.userdata = nullptr;
    return factory;
}

} // namespace Kotonoha
