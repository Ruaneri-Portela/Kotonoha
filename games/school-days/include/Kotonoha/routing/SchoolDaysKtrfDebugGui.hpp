#pragma once

struct Kotonoha_Game;

namespace Kotonoha {
class Gameplay;
class SchoolDaysKtrfAppController;
class KtrfSceneTransaction;
}

bool Kotonoha_SchoolDaysKtrfDebugGuiRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha::Kotonoha* game,
    Kotonoha::Gameplay* play,
    Kotonoha::KtrfSceneTransaction* transaction,
    Kotonoha_Game& context);
